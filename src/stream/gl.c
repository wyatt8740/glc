/**
 * \file src/stream/gl.c
 * \brief OpenGL stuff
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* gl.c -- OpenGL stuff
 * Copyright (C) 2007 Pyry Haulos
 * For conditions of distribution and use, see copyright notice in glc.h
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glext.h>
#include <GL/glu.h>
#include <unistd.h>
#include <packetstream.h>
#include <pthread.h>
#include <dlfcn.h>
#include <errno.h>

#include "../common/glc.h"
#include "../common/util.h"
#include "../common/thread.h"
#include "gl.h"

/**
 * \addtogroup stream
 *  \{
 */

/**
 * \defgroup gl OpenGL stuff
 *  \{
 */

typedef void (*FuncPtr)(void);
typedef FuncPtr (*GLXGetProcAddressProc)(const GLubyte *procName);
typedef void (*glGenBuffersProc)(GLsizei n,
                                 GLuint *buffers);
typedef void (*glDeleteBuffersProc)(GLsizei n,
                                    const GLuint *buffers);
typedef void (*glBufferDataProc)(GLenum target,
                                 GLsizeiptr size,
                                 const GLvoid *data,
                                 GLenum usage);
typedef void (*glBindBufferProc)(GLenum target,
                                 GLuint buffer);
typedef GLvoid *(*glMapBufferProc)(GLenum target,
                                   GLenum access);
typedef GLboolean (*glUnmapBufferProc)(GLenum target);

struct gl_ctx_s {
	Display *dpy;
	GLXDrawable drawable;
	GLXContext ctx;
	unsigned int w, h;
	ps_packet_t packet;
	glc_ctx_i ctx_i;
	glc_utime_t last, pbo_timestamp;
	int unsupported;
	int created;
	float zoom;

	int indicator_list;
	
	struct gl_ctx_s *next;
	
	char name[100];

	GLuint pbo;
	int pbo_active;
};

struct gl_private_s {
	glc_t *glc;
	Display *dpy;
	GLenum capture_buffer;
	glc_utime_t fps;
	
	pthread_rwlock_t ctxlist_lock;
	struct gl_ctx_s *ctx;
	glc_ctx_i ctx_c;
	glc_ctx_i last_ctx;
	
	ps_buffer_t *to;
	
	glc_thread_t show_thread;

	int try_pbo;
	int use_pbo;
	unsigned int bpp;
	GLenum format;
	
	void *libGL_handle;
	GLXGetProcAddressProc glXGetProcAddress;
	glGenBuffersProc glGenBuffers;
	glDeleteBuffersProc glDeleteBuffers;
	glBufferDataProc glBufferData;
	glBindBufferProc glBindBuffer;
	glMapBufferProc glMapBuffer;
	glUnmapBufferProc glUnmapBuffer;
};

int gl_show_read_callback(glc_thread_state_t *state);
void gl_show_finish_callback(void *ptr, int err);

int gl_set_ctx(struct gl_private_s *gl, struct gl_ctx_s *ctx);
int gl_show_create_ctx(struct gl_private_s *gl, struct gl_ctx_s *ctx);
int gl_show_update_ctx(struct gl_private_s *gl, struct gl_ctx_s *ctx);
int gl_get_ctx_capture(struct gl_private_s *gl, struct gl_ctx_s **ctx, Display *dpy, GLXDrawable drawable);
int gl_get_ctx_show(struct gl_private_s *gl, struct gl_ctx_s **ctx, glc_ctx_i ctx_i);
void free_ctx_list(struct gl_private_s *gl);

int gl_get_geometry(struct gl_private_s *gl, Display *dpy, GLXDrawable drawable, unsigned int *w, unsigned int *h);
int gl_get_pixels(struct gl_private_s *gl, struct gl_ctx_s *ctx, char *to);
int gl_put_pixels(struct gl_private_s *gl, struct gl_ctx_s *ctx, char *from);
int gl_gen_indicator_list(struct gl_private_s *gl, struct gl_ctx_s *ctx);

int gl_init_pbo(struct gl_private_s *gl);
int gl_create_pbo(struct gl_private_s *gl, struct gl_ctx_s *ctx);
int gl_destroy_pbo(struct gl_private_s *gl, struct gl_ctx_s *ctx);
int gl_start_pbo(struct gl_private_s *gl, struct gl_ctx_s *ctx);
int gl_read_pbo(struct gl_private_s *gl, struct gl_ctx_s *ctx, char *to);

int gl_handle_xevents(struct gl_private_s *gl);

void *gl_capture_init(glc_t *glc, ps_buffer_t *to)
{
	struct gl_private_s *gl = (struct gl_private_s *) malloc(sizeof(struct gl_private_s));
	memset(gl, 0, sizeof(struct gl_private_s));
	
	gl->glc = glc;
	gl->fps = 1000000 / gl->glc->fps;
	gl->to = to;
	
	if (gl->glc->flags & GLC_TRY_PBO)
		gl->try_pbo = 1;

	if (gl->glc->flags & GLC_CAPTURE_BGRA) {
		gl->format = GL_BGRA;
		gl->bpp = 4;
	} else {
		gl->format = GL_BGR;
		gl->bpp = 3;
	}
	
	if (gl->glc->flags & GLC_CAPTURE_FRONT)
		gl->capture_buffer = GL_FRONT;
	else
		gl->capture_buffer = GL_BACK;
	
	pthread_rwlock_init(&gl->ctxlist_lock, NULL);
	
	return (void *) gl;
}

void gl_capture_close(void *glpriv)
{
	struct gl_private_s *gl = (struct gl_private_s *) glpriv;

	free_ctx_list(gl);
	
	sem_post(&gl->glc->signal[GLC_SIGNAL_GL_FINISHED]);
	if (gl->libGL_handle)
		dlclose(gl->libGL_handle);
	free(gl);
}

int gl_show_init(glc_t *glc, ps_buffer_t *from)
{
	struct gl_private_s *gl = (struct gl_private_s *) malloc(sizeof(struct gl_private_s));
	memset(gl, 0, sizeof(struct gl_private_s));
	
	gl->glc = glc;
	gl->last_ctx = -1;
	
	gl->show_thread.flags = GLC_THREAD_READ;
	gl->show_thread.ptr = gl;
	gl->show_thread.read_callback = &gl_show_read_callback;
	gl->show_thread.finish_callback = &gl_show_finish_callback;
	gl->show_thread.threads = 1;
	
	gl->dpy = XOpenDisplay(NULL);
	
	if (!gl->dpy) {
		fprintf(stderr, "can't open display\n");
		return 1;
	}
	
	return glc_thread_create(glc, &gl->show_thread, from, NULL);
}

void gl_show_finish_callback(void *ptr, int err)
{
	struct gl_private_s *gl = (struct gl_private_s *) ptr;
	
	if (err)
		fprintf(stderr, "gl failed: %s (%d)\n", strerror(err), err);
	
	struct gl_ctx_s *ctx = gl->ctx;
	while (ctx != NULL) {
		if (ctx->dpy != NULL) {
			glXDestroyContext(ctx->dpy, ctx->ctx);
			XUnmapWindow(ctx->dpy, ctx->drawable);
			XDestroyWindow(ctx->dpy, ctx->drawable);
		}
		ctx = ctx->next;
	}
	
	XCloseDisplay(gl->dpy);
	
	free_ctx_list(gl);
	
	sem_post(&gl->glc->signal[GLC_SIGNAL_GL_FINISHED]);
	free(gl);
}

void free_ctx_list(struct gl_private_s *gl)
{
	struct gl_ctx_s *del;
	while (gl->ctx != NULL) {
		del = gl->ctx;
		gl->ctx = gl->ctx->next;
		
		/* we might be in wrong thread */
		if (del->indicator_list)
			glDeleteLists(del->indicator_list, 1);

		if (del->pbo)
			gl_destroy_pbo(gl, del);
		
		free(del);
	}
}

int gl_get_geometry(struct gl_private_s *gl, Display *dpy, GLXDrawable drawable,
                    unsigned int *w, unsigned int *h)
{
	Window rootWindow;
	int unused;
	
	XGetGeometry(dpy, drawable, &rootWindow, &unused, &unused, w, h,
	             (unsigned int *) &unused, (unsigned int *) &unused);
	
	return 0;
}

int gl_get_pixels(struct gl_private_s *gl, struct gl_ctx_s *ctx, char *to)
{
	glPushAttrib(GL_PIXEL_MODE_BIT);
	glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
	glReadBuffer(gl->capture_buffer);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, ctx->w, ctx->h, gl->format, GL_UNSIGNED_BYTE, to);
	glPopClientAttrib();
	glPopAttrib();
	
	return 0;
}

int gl_put_pixels(struct gl_private_s *gl, struct gl_ctx_s *ctx, char *from)
{
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glBitmap(0, 0, 0, 0, 0, 0, NULL);
	
	if (ctx->zoom != 1.0)
		glPixelZoom(ctx->zoom, ctx->zoom);
	
	glDrawPixels(ctx->w, ctx->h, GL_BGR, GL_UNSIGNED_BYTE, from);
	glXSwapBuffers(ctx->dpy, ctx->drawable);

	return 0;
}

int gl_gen_indicator_list(struct gl_private_s *gl, struct gl_ctx_s *ctx)
{
	int size;
	if (!ctx->indicator_list)
		ctx->indicator_list = glGenLists(1);
	
	glNewList(ctx->indicator_list, GL_COMPILE);
	
	size = ctx->h / 50;
	if (size < 10)
		size = 10;

	glPushAttrib(GL_ALL_ATTRIB_BITS);

	glViewport(0, 0, ctx->w, ctx->h);
	glEnable(GL_SCISSOR_TEST);
	glScissor(size / 2 - 1, ctx->h - size - size / 2 - 1, size + 2, size + 2);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glScissor(size / 2, ctx->h - size - size / 2, size, size);
	glClearColor(1.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glDisable(GL_SCISSOR_TEST);

	glPopAttrib();

	glEndList();
	return 0;
}

int gl_init_pbo(struct gl_private_s *gl)
{
	const char *gl_extensions = (const char *) glGetString(GL_EXTENSIONS);

	if (gl_extensions == NULL)
		return EINVAL;
	
	if (!strstr(gl_extensions, "GL_ARB_pixel_buffer_object"))
		return ENOTSUP;
	
	gl->libGL_handle = dlopen("libGL.so", RTLD_LAZY);
	if (!gl->libGL_handle)
		return ENOTSUP;
	gl->glXGetProcAddress =
		(GLXGetProcAddressProc)
		dlsym(gl->libGL_handle, "glXGetProcAddressARB");
	if (!gl->glXGetProcAddress)
		return ENOTSUP;
	
	gl->glGenBuffers =
		(glGenBuffersProc)
		gl->glXGetProcAddress((const GLubyte *) "glGenBuffersARB");
	if (!gl->glGenBuffers)
		return ENOTSUP;
	gl->glDeleteBuffers =
		(glDeleteBuffersProc)
		gl->glXGetProcAddress((const GLubyte *) "glDeleteBuffersARB");
	if (!gl->glDeleteBuffers)
		return ENOTSUP;
	gl->glBufferData =
		(glBufferDataProc)
		gl->glXGetProcAddress((const GLubyte *) "glBufferDataARB");
	if (!gl->glBufferData)
		return ENOTSUP;
	gl->glBindBuffer =
		(glBindBufferProc)
		gl->glXGetProcAddress((const GLubyte *) "glBindBufferARB");
	if (!gl->glBindBuffer)
		return ENOTSUP;
	gl->glMapBuffer =
		(glMapBufferProc)
		gl->glXGetProcAddress((const GLubyte *) "glMapBufferARB");
	if (!gl->glMapBuffer)
		return ENOTSUP;
	gl->glUnmapBuffer =
		(glUnmapBufferProc)
		gl->glXGetProcAddress((const GLubyte *) "glUnmapBufferARB");
	if (!gl->glUnmapBuffer)
		return ENOTSUP;

	return 0;
}

int gl_create_pbo(struct gl_private_s *gl, struct gl_ctx_s *ctx)
{
	glPushAttrib(GL_ALL_ATTRIB_BITS);

	gl->glGenBuffers(1, &ctx->pbo);
	gl->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, ctx->pbo);
	gl->glBufferData(GL_PIXEL_PACK_BUFFER_ARB, ctx->w * ctx->h * gl->bpp,
		         NULL, GL_STREAM_READ);

	glPopAttrib();
	return 0;
}

int gl_destroy_pbo(struct gl_private_s *gl, struct gl_ctx_s *ctx)
{
	gl->glDeleteBuffers(1, &ctx->pbo);
	return 0;
}

int gl_start_pbo(struct gl_private_s *gl, struct gl_ctx_s *ctx)
{
	GLint binding;
	if (ctx->pbo_active)
		return EAGAIN;

	glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING_ARB, &binding);
	glPushAttrib(GL_PIXEL_MODE_BIT);
	glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);

	gl->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, ctx->pbo);

	glReadBuffer(gl->capture_buffer);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	/* to = ((char *)NULL + (offset)) */
	glReadPixels(0, 0, ctx->w, ctx->h, gl->format, GL_UNSIGNED_BYTE, NULL);

	ctx->pbo_active = 1;

	glPopClientAttrib();
	glPopAttrib();
	gl->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, binding);
	return 0;
}

int gl_read_pbo(struct gl_private_s *gl, struct gl_ctx_s *ctx, char *to)
{
	GLvoid *buf;
	GLint binding;
	
	if (!ctx->pbo_active)
		return EAGAIN;

	glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING_ARB, &binding);

	gl->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, ctx->pbo);
	buf = gl->glMapBuffer(GL_PIXEL_PACK_BUFFER_ARB, GL_READ_ONLY);
	if (!buf)
		return EINVAL;

	memcpy(to, buf, ctx->w * ctx->h * gl->bpp);
	gl->glUnmapBuffer(GL_PIXEL_PACK_BUFFER_ARB);

	ctx->pbo_active = 0;
	
	gl->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, binding);
	return 0;
}

int gl_get_ctx_capture(struct gl_private_s *gl, struct gl_ctx_s **ctx, Display *dpy, GLXDrawable drawable)
{
	glc_message_header_t msg;
	glc_ctx_message_t ctx_msg;
	struct gl_ctx_s *fctx;
	unsigned int w, h;
	
	pthread_rwlock_rdlock(&gl->ctxlist_lock);
	fctx = gl->ctx;
	while (fctx != NULL) {
		if (fctx->drawable == drawable)
			break;
		
		fctx = fctx->next;
	}
	pthread_rwlock_unlock(&gl->ctxlist_lock);
	
	if (fctx == NULL) {
		fctx = (struct gl_ctx_s *) malloc(sizeof(struct gl_ctx_s));
		memset(fctx, 0, sizeof(struct gl_ctx_s));
		
		fctx->drawable = drawable;
		
		ps_packet_init(&fctx->packet, gl->to);
		
		pthread_rwlock_wrlock(&gl->ctxlist_lock);
		fctx->next = gl->ctx;
		fctx->ctx_i = ++gl->ctx_c;
		gl->ctx = fctx;

		if (gl->try_pbo) {
			if (!gl_init_pbo(gl))
				gl->use_pbo = 1;
			gl->try_pbo = 0;
		}
		pthread_rwlock_unlock(&gl->ctxlist_lock);
		
		gl_get_geometry(gl, dpy, drawable, &fctx->w, &fctx->h);
		
		msg.type = GLC_MESSAGE_CTX;
		ctx_msg.flags = GLC_CTX_CREATE;
		if (gl->glc->flags & GLC_CAPTURE_BGRA)
			ctx_msg.flags |= GLC_CTX_BGRA;
		else
			ctx_msg.flags |= GLC_CTX_BGR;
		ctx_msg.ctx = fctx->ctx_i;
		ctx_msg.w = fctx->w;
		ctx_msg.h = fctx->h;
		
		ps_packet_open(&fctx->packet, PS_PACKET_WRITE);
		ps_packet_write(&fctx->packet, &msg, GLC_MESSAGE_HEADER_SIZE);
		ps_packet_write(&fctx->packet, &ctx_msg, GLC_CTX_MESSAGE_SIZE);
		ps_packet_close(&fctx->packet);

		if (gl->use_pbo)
			gl_create_pbo(gl, fctx);
		gl_gen_indicator_list(gl, fctx);
	} else {
		gl_get_geometry(gl, dpy, drawable, &w, &h);
		
		if ((w != fctx->w) | (h != fctx->h)) {
			fctx->w = w;
			fctx->h = h;
			
			msg.type = GLC_MESSAGE_CTX;
			ctx_msg.flags = GLC_CTX_UPDATE;
			if (gl->glc->flags & GLC_CAPTURE_BGRA)
				ctx_msg.flags |= GLC_CTX_BGRA;
			else
				ctx_msg.flags |= GLC_CTX_BGR;
			ctx_msg.ctx = fctx->ctx_i;
			ctx_msg.w = fctx->w;
			ctx_msg.h = fctx->h;
			
			ps_packet_open(&fctx->packet, PS_PACKET_WRITE);
			ps_packet_write(&fctx->packet, &msg, GLC_MESSAGE_HEADER_SIZE);
			ps_packet_write(&fctx->packet, &ctx_msg, GLC_CTX_MESSAGE_SIZE);
			ps_packet_close(&fctx->packet);

			gl_gen_indicator_list(gl, fctx);

			if (gl->use_pbo) {
				gl_destroy_pbo(gl, fctx);
				gl_create_pbo(gl, fctx);
			}
		}
	}
	
	*ctx = fctx;
	return 0;
}

int gl_get_ctx_show(struct gl_private_s *gl, struct gl_ctx_s **ctx, glc_ctx_i ctx_i)
{
	struct gl_ctx_s *fctx = gl->ctx;
	
	while (fctx != NULL) {
		if (fctx->ctx_i == ctx_i)
			break;
		fctx = fctx->next;
	}
	
	if (fctx == NULL) {
		fctx = (struct gl_ctx_s *) malloc(sizeof(struct gl_ctx_s));
		memset(fctx, 0, sizeof(struct gl_ctx_s));
		
		fctx->next = gl->ctx;
		gl->ctx = fctx;
		fctx->ctx_i = ctx_i;
	}
	
	*ctx = fctx;
	
	return 0;
}

int gl_show_create_ctx(struct gl_private_s *gl, struct gl_ctx_s *ctx)
{
	int attribs[] = { GLX_RGBA,
			  GLX_RED_SIZE, 1,
			  GLX_GREEN_SIZE, 1,
			  GLX_BLUE_SIZE, 1,
			  GLX_DOUBLEBUFFER,
			  GLX_DEPTH_SIZE, 1,
			  None };
	XVisualInfo *visinfo;
	XSetWindowAttributes winattr;

	ctx->zoom = 1;
	ctx->dpy = gl->dpy;
	visinfo = glXChooseVisual(ctx->dpy, DefaultScreen(ctx->dpy), attribs);
	
	winattr.background_pixel = 0;
	winattr.border_pixel = 0;
	winattr.colormap = XCreateColormap(ctx->dpy, RootWindow(ctx->dpy, DefaultScreen(ctx->dpy)),
	                                   visinfo->visual, AllocNone);
	winattr.event_mask = StructureNotifyMask | ExposureMask | KeyPressMask | KeyReleaseMask;
	winattr.override_redirect = 0;
	ctx->drawable = XCreateWindow(ctx->dpy, RootWindow(ctx->dpy, DefaultScreen(ctx->dpy)),
	                              0, 0, ctx->w, ctx->h, 0, visinfo->depth, InputOutput,
	                              visinfo->visual, CWBackPixel | CWBorderPixel |
	                              CWColormap | CWEventMask | CWOverrideRedirect, &winattr);
	
	ctx->ctx = glXCreateContext(ctx->dpy, visinfo, NULL, True);
	ctx->created = 1;
	
	XFree(visinfo);
	
	return gl_show_update_ctx(gl, ctx);
}

int gl_show_update_ctx(struct gl_private_s *gl, struct gl_ctx_s *ctx)
{
	XSizeHints sizehints;
	
	if (!ctx->created)
		return EINVAL;
	
	snprintf(ctx->name, sizeof(ctx->name) - 1, "glc-play (ctx %d)", ctx->ctx_i);
	
	ctx->zoom = 1;

	XUnmapWindow(ctx->dpy, ctx->drawable);
	
	sizehints.x = 0;
	sizehints.y = 0;
	sizehints.width = ctx->w;
	sizehints.height = ctx->h;
	sizehints.min_aspect.x = ctx->w;
	sizehints.min_aspect.y = ctx->h;
	sizehints.max_aspect.x = ctx->w;
	sizehints.max_aspect.y = ctx->h;
	sizehints.flags = USSize | USPosition | PAspect;
	XSetNormalHints(ctx->dpy, ctx->drawable, &sizehints);
	XSetStandardProperties(ctx->dpy, ctx->drawable, ctx->name, ctx->name, None,
	                       (char **)NULL, 0, &sizehints);
	XResizeWindow(ctx->dpy, ctx->drawable, ctx->w, ctx->h);
	
	XMapWindow(ctx->dpy, ctx->drawable);
	
	glXMakeCurrent(ctx->dpy, ctx->drawable, ctx->ctx);
	glViewport(0, 0, (GLsizei) ctx->w, (GLsizei) ctx->h);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	
	return 0;
}

int gl_set_ctx(struct gl_private_s *gl, struct gl_ctx_s *ctx)
{
	gl->dpy = ctx->dpy;
	glXMakeCurrent(ctx->dpy, ctx->drawable, ctx->ctx);
	
	return 0;
}

int gl_capture(void *glpriv, Display *dpy, GLXDrawable drawable)
{
	struct gl_private_s *gl = (struct gl_private_s *) glpriv;
	struct gl_ctx_s *ctx;
	glc_message_header_t msg;
	glc_picture_header_t pic;
	glc_utime_t now;
	char *dma;
	int ret = 0;
	
	gl_get_ctx_capture(gl, &ctx, dpy, drawable);

	if ((gl->use_pbo) && (!ctx->pbo_active)) {
		ret = gl_start_pbo(gl, ctx);
		ctx->pbo_timestamp = util_timestamp(gl->glc);
		goto finish;
	}
	
	msg.type = GLC_MESSAGE_PICTURE;
	pic.ctx = ctx->ctx_i;

	now = util_timestamp(gl->glc);
	if (gl->use_pbo)
		pic.timestamp = ctx->pbo_timestamp;
	else
		pic.timestamp = now;
	
	if (now - ctx->last >= gl->fps) {
		if (ps_packet_open(&ctx->packet, PS_PACKET_WRITE | PS_PACKET_TRY))
			goto finish;
		if ((ret = ps_packet_write(&ctx->packet, &msg, GLC_MESSAGE_HEADER_SIZE)))
			goto cancel;
		if ((ret = ps_packet_write(&ctx->packet, &pic, GLC_PICTURE_HEADER_SIZE)))
			goto cancel;
		if ((ret = ps_packet_dma(&ctx->packet, (void *) &dma,
					 ctx->w * ctx->h * gl->bpp, PS_ACCEPT_FAKE_DMA)))
			goto cancel;

		if (gl->use_pbo) {
			gl_read_pbo(gl, ctx, dma);
			ret = gl_start_pbo(gl, ctx);
			ctx->pbo_timestamp = now;
		} else
			ret = gl_get_pixels(gl, ctx, dma);
		
		ctx->last += gl->fps;

		ps_packet_close(&ctx->packet);
	}

finish:
	if (gl->glc->flags & GLC_DRAW_INDICATOR)
		glCallList(ctx->indicator_list);
	
	return ret;
cancel:
	if (ret == EBUSY)
		ret = 0;
	ps_packet_cancel(&ctx->packet);
	goto finish;
}

int gl_handle_xevents(struct gl_private_s *gl)
{
	struct gl_ctx_s *ctx;
	
	XEvent event;
	XConfigureEvent *ce;
	int code;

	/* TODO catch DestroyEvent, but how? */
	while (XPending(gl->dpy) > 0) {
		XNextEvent(gl->dpy, &event);
		
		switch (event.type) {
		case KeyPress:
			code = XLookupKeysym(&event.xkey, 0);
			
			if (code == XK_Right)
				util_timediff(gl->glc, -100000);
			break;
		case KeyRelease:
			code = XLookupKeysym(&event.xkey, 0);

			if (code == XK_Escape)
				gl->glc->flags |= GLC_CANCEL;
			break;
		case ConfigureNotify:
			ce = (XConfigureEvent *) &event;
			
			ctx = gl->ctx;
			while (ctx != NULL) {
				if (ctx->drawable == ce->window)
					break;
				ctx = ctx->next;
			}
			
			if (ctx != NULL)
				ctx->zoom = (float) ce->width / (float) ctx->w;
			
			break;
		}
	}
	
	return 0;
}

int gl_show_read_callback(glc_thread_state_t *state)
{
	struct gl_private_s *gl = (struct gl_private_s *) state->ptr;
	
	glc_ctx_message_t *ctx_msg;
	glc_picture_header_t *pic_hdr;
	glc_utime_t time;
	struct gl_ctx_s *ctx;
	
	if (state->header.type == GLC_MESSAGE_CTX) {
		ctx_msg = (glc_ctx_message_t *) state->read_data;
		gl_get_ctx_show(gl, &ctx, ctx_msg->ctx);
		ctx->w = ctx_msg->w;
		ctx->h = ctx_msg->h;
		
		if ((ctx_msg->flags & GLC_CTX_BGR) && (ctx_msg->flags & GLC_CTX_CREATE)) {
			gl_show_create_ctx(gl, ctx);
			gl->last_ctx = ctx_msg->ctx;
		} else if ((ctx_msg->flags & GLC_CTX_BGR) && (ctx_msg->flags & GLC_CTX_UPDATE)) {
			if (gl_show_update_ctx(gl, ctx))
				fprintf(stderr, "broken ctx %d\n", ctx_msg->ctx);
		} else {
			ctx->unsupported = 1;
			printf("ctx %d is in unsupported format\n", ctx_msg->ctx);
		}
	} else if (state->header.type == GLC_MESSAGE_PICTURE) {
		pic_hdr = (glc_picture_header_t *) state->read_data;
		gl_get_ctx_show(gl, &ctx, pic_hdr->ctx);
		
		if (ctx->unsupported)
			return 0;
		
		if (!ctx->created) {
			fprintf(stderr, "picture refers to uninitalized ctx %d\n", pic_hdr->ctx);
			gl->glc->flags |= GLC_CANCEL;
			return EINVAL;
		}
		
		if (gl->last_ctx != pic_hdr->ctx) {
			gl_set_ctx(gl, ctx);
			gl->last_ctx = pic_hdr->ctx;
		}
		
		gl_handle_xevents(gl);
		
		time = util_timestamp(gl->glc);
		
		if (pic_hdr->timestamp > time)
			usleep(pic_hdr->timestamp - time);
		else if (time > pic_hdr->timestamp + gl->fps)
			return 0;

		gl_put_pixels(gl, ctx, &state->read_data[GLC_PICTURE_HEADER_SIZE]);
	}

	return 0;
}

/**  \} */
/**  \} */
