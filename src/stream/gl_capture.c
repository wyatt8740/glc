/**
 * \file src/stream/gl_capture.c
 * \brief OpenGL capture
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* gl_capture.c -- OpenGL capture
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
#include <unistd.h>
#include <packetstream.h>
#include <pthread.h>
#include <dlfcn.h>
#include <errno.h>

#include "../common/glc.h"
#include "../common/util.h"
#include "gl_capture.h"

/**
 * \addtogroup stream
 *  \{
 */

/**
 * \defgroup gl_capture OpenGL capture
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

struct gl_capture_ctx_s {
	glc_flags_t flags;
	Display *dpy;
	GLXDrawable drawable;
	ps_packet_t packet;
	glc_ctx_i ctx_i;
	glc_utime_t last, pbo_timestamp;

	unsigned int w, h;
	unsigned int cw, ch, row, cx, cy;

	int indicator_list;

	struct gl_capture_ctx_s *next;

	GLuint pbo;
	int pbo_active;
	int use_pbo;
};

struct gl_capture_private_s {
	glc_t *glc;
	GLenum capture_buffer;
	glc_utime_t fps;

	pthread_rwlock_t ctxlist_lock;
	struct gl_capture_ctx_s *ctx;
	glc_ctx_i ctx_c;

	ps_buffer_t *to;

	int init_pbo;
	pthread_mutex_t init_pbo_mutex;

	unsigned int bpp;
	GLenum format;
	unsigned int pack_alignment;

	void *libGL_handle;
	GLXGetProcAddressProc glXGetProcAddress;
	glGenBuffersProc glGenBuffers;
	glDeleteBuffersProc glDeleteBuffers;
	glBufferDataProc glBufferData;
	glBindBufferProc glBindBuffer;
	glMapBufferProc glMapBuffer;
	glUnmapBufferProc glUnmapBuffer;
};

int gl_capture_get_ctx(struct gl_capture_private_s *gl_capture,
		       struct gl_capture_ctx_s **ctx, Display *dpy, GLXDrawable drawable);
int gl_capture_update_ctx(struct gl_capture_private_s *gl_capture,
			  struct gl_capture_ctx_s *ctx);

int gl_capture_get_geometry(struct gl_capture_private_s *gl_capture,
			    Display *dpy, GLXDrawable drawable, unsigned int *w, unsigned int *h);
int gl_capture_calc_geometry(struct gl_capture_private_s *gl_capture, struct gl_capture_ctx_s *ctx,
			     unsigned int w, unsigned int h);

int gl_capture_get_pixels(struct gl_capture_private_s *gl_capture, struct gl_capture_ctx_s *ctx, char *to);
int gl_capture_gen_indicator_list(struct gl_capture_private_s *gl_capture, struct gl_capture_ctx_s *ctx);

int gl_capture_init_pbo(struct gl_capture_private_s *gl);
int gl_capture_create_pbo(struct gl_capture_private_s *gl_capture, struct gl_capture_ctx_s *ctx);
int gl_capture_destroy_pbo(struct gl_capture_private_s *gl_capture, struct gl_capture_ctx_s *ctx);
int gl_capture_start_pbo(struct gl_capture_private_s *gl_capture, struct gl_capture_ctx_s *ctx);
int gl_capture_read_pbo(struct gl_capture_private_s *gl_capture, struct gl_capture_ctx_s *ctx);

void *gl_capture_init(glc_t *glc, ps_buffer_t *to)
{
	struct gl_capture_private_s *gl_capture = (struct gl_capture_private_s *) malloc(sizeof(struct gl_capture_private_s));
	memset(gl_capture, 0, sizeof(struct gl_capture_private_s));
	
	gl_capture->glc = glc;
	gl_capture->fps = 1000000 / gl_capture->glc->fps;
	gl_capture->to = to;

	if (gl_capture->glc->flags & GLC_CAPTURE_DWORD_ALIGNED)
		gl_capture->pack_alignment = 8;
	else
		gl_capture->pack_alignment = 1;
	
	if (gl_capture->glc->flags & GLC_TRY_PBO)
		gl_capture->init_pbo = 1;

	if (gl_capture->glc->flags & GLC_CAPTURE_BGRA) {
		gl_capture->format = GL_BGRA;
		gl_capture->bpp = 4;
	} else {
		gl_capture->format = GL_BGR;
		gl_capture->bpp = 3;
	}
	
	if (gl_capture->glc->flags & GLC_CAPTURE_FRONT)
		gl_capture->capture_buffer = GL_FRONT;
	else
		gl_capture->capture_buffer = GL_BACK;

	pthread_mutex_init(&gl_capture->init_pbo_mutex, NULL);
	pthread_rwlock_init(&gl_capture->ctxlist_lock, NULL);
	
	return (void *) gl_capture;
}

void gl_capture_close(void *glpriv)
{
	struct gl_capture_private_s *gl_capture = (struct gl_capture_private_s *) glpriv;
	struct gl_capture_ctx_s *del;

	while (gl_capture->ctx != NULL) {
		del = gl_capture->ctx;
		gl_capture->ctx = gl_capture->ctx->next;
		
		/* we might be in wrong thread */
		if (del->indicator_list)
			glDeleteLists(del->indicator_list, 1);

		if (del->pbo)
			gl_capture_destroy_pbo(gl_capture, del);
		
		free(del);
	}

	sem_post(&gl_capture->glc->signal[GLC_SIGNAL_GL_CAPTURE_FINISHED]);

	pthread_rwlock_destroy(&gl_capture->ctxlist_lock);
	pthread_mutex_destroy(&gl_capture->init_pbo_mutex);

	if (gl_capture->libGL_handle)
		dlclose(gl_capture->libGL_handle);
	free(gl_capture);
}

int gl_capture_get_geometry(struct gl_capture_private_s *gl_capture, Display *dpy, GLXDrawable drawable,
                    unsigned int *w, unsigned int *h)
{
	Window rootWindow;
	int unused;
	
	XGetGeometry(dpy, drawable, &rootWindow, &unused, &unused, w, h,
	             (unsigned int *) &unused, (unsigned int *) &unused);
	
	return 0;
}

int gl_capture_calc_geometry(struct gl_capture_private_s *gl_capture, struct gl_capture_ctx_s *ctx,
			     unsigned int w, unsigned int h)
{
	ctx->w = w;
	ctx->h = h;

	/* calculate image area when cropping */
	if (gl_capture->glc->flags & GLC_CROP) {
		if (gl_capture->glc->crop_x > ctx->w)
			ctx->cx = 0;
		else
			ctx->cx = gl_capture->glc->crop_x;

		if (gl_capture->glc->crop_y > ctx->h)
			ctx->cy = 0;
		else
			ctx->cy = gl_capture->glc->crop_y;

		if (gl_capture->glc->crop_width + ctx->cx > ctx->w)
			ctx->cw = ctx->w - ctx->cx;
		else
			ctx->cw = gl_capture->glc->crop_width;

		if (gl_capture->glc->crop_height + ctx->cy > ctx->h)
			ctx->ch = ctx->h - ctx->cy;
		else
			ctx->ch = gl_capture->glc->crop_height;

		/* we need to recalc y coord for OpenGL */
		ctx->cy = ctx->h - ctx->ch - ctx->cy;
	} else {
		ctx->cw = ctx->w;
		ctx->ch = ctx->h;
		ctx->cx = ctx->cy = 0;
	}

	ctx->row = ctx->cw * gl_capture->bpp;
	if (ctx->row % gl_capture->pack_alignment != 0)
		ctx->row += gl_capture->pack_alignment - ctx->row % gl_capture->pack_alignment;

	return 0;
}

int gl_capture_get_pixels(struct gl_capture_private_s *gl_capture, struct gl_capture_ctx_s *ctx, char *to)
{
	glPushAttrib(GL_PIXEL_MODE_BIT);
	glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);

	glReadBuffer(gl_capture->capture_buffer);
	glPixelStorei(GL_PACK_ALIGNMENT, gl_capture->pack_alignment);
	glReadPixels(ctx->cx, ctx->cy, ctx->cw, ctx->ch, gl_capture->format, GL_UNSIGNED_BYTE, to);

	glPopClientAttrib();
	glPopAttrib();

	return 0;
}

int gl_capture_gen_indicator_list(struct gl_capture_private_s *gl_capture, struct gl_capture_ctx_s *ctx)
{
	int size;
	if (!ctx->indicator_list)
		ctx->indicator_list = glGenLists(1);
	
	glNewList(ctx->indicator_list, GL_COMPILE);
	
	size = ctx->h / 75;
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

int gl_capture_init_pbo(struct gl_capture_private_s *gl_capture)
{
	const char *gl_extensions = (const char *) glGetString(GL_EXTENSIONS);

	if (gl_extensions == NULL)
		return EINVAL;
	
	if (!strstr(gl_extensions, "GL_ARB_pixel_buffer_object"))
		return ENOTSUP;
	
	gl_capture->libGL_handle = dlopen("libGL.so", RTLD_LAZY);
	if (!gl_capture->libGL_handle)
		return ENOTSUP;
	gl_capture->glXGetProcAddress =
		(GLXGetProcAddressProc)
		dlsym(gl_capture->libGL_handle, "glXGetProcAddressARB");
	if (!gl_capture->glXGetProcAddress)
		return ENOTSUP;
	
	gl_capture->glGenBuffers =
		(glGenBuffersProc)
		gl_capture->glXGetProcAddress((const GLubyte *) "glGenBuffersARB");
	if (!gl_capture->glGenBuffers)
		return ENOTSUP;
	gl_capture->glDeleteBuffers =
		(glDeleteBuffersProc)
		gl_capture->glXGetProcAddress((const GLubyte *) "glDeleteBuffersARB");
	if (!gl_capture->glDeleteBuffers)
		return ENOTSUP;
	gl_capture->glBufferData =
		(glBufferDataProc)
		gl_capture->glXGetProcAddress((const GLubyte *) "glBufferDataARB");
	if (!gl_capture->glBufferData)
		return ENOTSUP;
	gl_capture->glBindBuffer =
		(glBindBufferProc)
		gl_capture->glXGetProcAddress((const GLubyte *) "glBindBufferARB");
	if (!gl_capture->glBindBuffer)
		return ENOTSUP;
	gl_capture->glMapBuffer =
		(glMapBufferProc)
		gl_capture->glXGetProcAddress((const GLubyte *) "glMapBufferARB");
	if (!gl_capture->glMapBuffer)
		return ENOTSUP;
	gl_capture->glUnmapBuffer =
		(glUnmapBufferProc)
		gl_capture->glXGetProcAddress((const GLubyte *) "glUnmapBufferARB");
	if (!gl_capture->glUnmapBuffer)
		return ENOTSUP;

	util_log(gl_capture->glc, GLC_INFORMATION, "gl_capture",
		 "using GL_ARB_pixel_buffer_object");

	return 0;
}

int gl_capture_create_pbo(struct gl_capture_private_s *gl_capture, struct gl_capture_ctx_s *ctx)
{
	glPushAttrib(GL_ALL_ATTRIB_BITS);

	gl_capture->glGenBuffers(1, &ctx->pbo);
	gl_capture->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, ctx->pbo);
	gl_capture->glBufferData(GL_PIXEL_PACK_BUFFER_ARB, ctx->row * ctx->ch,
		         NULL, GL_STREAM_READ);

	glPopAttrib();
	return 0;
}

int gl_capture_destroy_pbo(struct gl_capture_private_s *gl_capture, struct gl_capture_ctx_s *ctx)
{
	gl_capture->glDeleteBuffers(1, &ctx->pbo);
	return 0;
}

int gl_capture_start_pbo(struct gl_capture_private_s *gl_capture, struct gl_capture_ctx_s *ctx)
{
	GLint binding;
	if (ctx->pbo_active)
		return EAGAIN;

	glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING_ARB, &binding);
	glPushAttrib(GL_PIXEL_MODE_BIT);
	glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);

	gl_capture->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, ctx->pbo);

	glReadBuffer(gl_capture->capture_buffer);
	glPixelStorei(GL_PACK_ALIGNMENT, gl_capture->pack_alignment);
	/* to = ((char *)NULL + (offset)) */
	glReadPixels(ctx->cx, ctx->cy, ctx->cw, ctx->ch, gl_capture->format, GL_UNSIGNED_BYTE, NULL);

	ctx->pbo_active = 1;

	glPopClientAttrib();
	glPopAttrib();
	gl_capture->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, binding);
	return 0;
}

int gl_capture_read_pbo(struct gl_capture_private_s *gl_capture, struct gl_capture_ctx_s *ctx)
{
	GLvoid *buf;
	GLint binding;
	
	if (!ctx->pbo_active)
		return EAGAIN;

	glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING_ARB, &binding);

	gl_capture->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, ctx->pbo);
	buf = gl_capture->glMapBuffer(GL_PIXEL_PACK_BUFFER_ARB, GL_READ_ONLY);
	if (!buf)
		return EINVAL;

	ps_packet_write(&ctx->packet, buf, ctx->row * ctx->ch);

	gl_capture->glUnmapBuffer(GL_PIXEL_PACK_BUFFER_ARB);

	ctx->pbo_active = 0;
	
	gl_capture->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, binding);
	return 0;
}

int gl_capture_get_ctx(struct gl_capture_private_s *gl_capture, struct gl_capture_ctx_s **ctx, Display *dpy, GLXDrawable drawable)
{
	struct gl_capture_ctx_s *fctx;

	pthread_rwlock_rdlock(&gl_capture->ctxlist_lock);
	fctx = gl_capture->ctx;
	while (fctx != NULL) {
		if ((fctx->drawable == drawable) && (fctx->dpy == dpy))
			break;
		
		fctx = fctx->next;
	}
	pthread_rwlock_unlock(&gl_capture->ctxlist_lock);
	
	if (fctx == NULL) {
		fctx = (struct gl_capture_ctx_s *) malloc(sizeof(struct gl_capture_ctx_s));
		memset(fctx, 0, sizeof(struct gl_capture_ctx_s));

		fctx->dpy = dpy;
		fctx->drawable = drawable;
		ps_packet_init(&fctx->packet, gl_capture->to);

		/* these functions need to be thread-safe */
		pthread_rwlock_wrlock(&gl_capture->ctxlist_lock);

		fctx->next = gl_capture->ctx;
		fctx->ctx_i = ++gl_capture->ctx_c;
		gl_capture->ctx = fctx;

		pthread_rwlock_unlock(&gl_capture->ctxlist_lock);
	}
	
	*ctx = fctx;
	return 0;
}

int gl_capture_update_ctx(struct gl_capture_private_s *gl_capture,
			  struct gl_capture_ctx_s *ctx)
{
	glc_message_header_t msg;
	glc_ctx_message_t ctx_msg;
	unsigned int w, h;

	if (gl_capture->init_pbo) {
		pthread_mutex_lock(&gl_capture->init_pbo_mutex);
		if (!gl_capture_init_pbo(gl_capture))
			ctx->use_pbo = 1;
		gl_capture->init_pbo = 0;
		pthread_mutex_unlock(&gl_capture->init_pbo_mutex);
	}

	gl_capture_get_geometry(gl_capture, ctx->dpy, ctx->drawable, &w, &h);

	if (ctx->flags == 0) {
		ctx->flags |= GLC_CTX_CREATE;

		if (gl_capture->glc->flags & GLC_CAPTURE_BGRA)
			ctx->flags |= GLC_CTX_BGRA;
		else
			ctx->flags |= GLC_CTX_BGR;

		if (gl_capture->glc->flags & GLC_CTX_DWORD_ALIGNED)
			ctx->flags = GLC_CTX_DWORD_ALIGNED;
	} else if (ctx->flags & GLC_CTX_CREATE) {
		ctx->flags &= ~GLC_CTX_CREATE;
		ctx->flags |= GLC_CTX_UPDATE;
	}

	if ((w != ctx->w) | (h != ctx->h) | (ctx->flags & GLC_CTX_CREATE)) {
		gl_capture_calc_geometry(gl_capture, ctx, w, h);
		
		msg.type = GLC_MESSAGE_CTX;
		ctx_msg.flags = ctx->flags;
		ctx_msg.ctx = ctx->ctx_i;
		ctx_msg.w = ctx->cw;
		ctx_msg.h = ctx->ch;

		ps_packet_open(&ctx->packet, PS_PACKET_WRITE);
		ps_packet_write(&ctx->packet, &msg, GLC_MESSAGE_HEADER_SIZE);
		ps_packet_write(&ctx->packet, &ctx_msg, GLC_CTX_MESSAGE_SIZE);
		ps_packet_close(&ctx->packet);

		if (ctx->use_pbo) {
			if (ctx->pbo)
				gl_capture_destroy_pbo(gl_capture, ctx);

			if (!gl_capture_create_pbo(gl_capture, ctx))
				ctx->use_pbo = 0;
		}
	}

	return 0;
}

int gl_capture_frame(void *glpriv, Display *dpy, GLXDrawable drawable)
{
	struct gl_capture_private_s *gl_capture = glpriv;
	struct gl_capture_ctx_s *ctx;

	gl_capture_get_ctx(gl_capture, &ctx, dpy, drawable);

	return 0;
}

int gl_capture(void *glpriv, Display *dpy, GLXDrawable drawable)
{
	struct gl_capture_private_s *gl_capture = (struct gl_capture_private_s *) glpriv;
	struct gl_capture_ctx_s *ctx;
	glc_message_header_t msg;
	glc_picture_header_t pic;
	glc_utime_t now;
	char *dma;
	int ret = 0;
	
	gl_capture_get_ctx(gl_capture, &ctx, dpy, drawable);
	
	msg.type = GLC_MESSAGE_PICTURE;
	pic.ctx = ctx->ctx_i;

	now = util_timestamp(gl_capture->glc);
	if (ctx->use_pbo)
		pic.timestamp = ctx->pbo_timestamp;
	else
		pic.timestamp = now;
	
	if ((now - ctx->last < gl_capture->fps) &&
	    !(gl_capture->glc->flags & GLC_LOCK_FPS))
		goto finish;

	/* not really needed until now */
	gl_capture_update_ctx(gl_capture, ctx);

	if ((ctx->use_pbo) && (!ctx->pbo_active)) {
		ret = gl_capture_start_pbo(gl_capture, ctx);
		ctx->pbo_timestamp = util_timestamp(gl_capture->glc);
		goto finish;
	}

	if (ps_packet_open(&ctx->packet, gl_capture->glc->flags & GLC_LOCK_FPS ?
					 PS_PACKET_WRITE :
					 PS_PACKET_WRITE | PS_PACKET_TRY))
		goto finish;
	if ((ret = ps_packet_write(&ctx->packet, &msg, GLC_MESSAGE_HEADER_SIZE)))
		goto cancel;
	if ((ret = ps_packet_write(&ctx->packet, &pic, GLC_PICTURE_HEADER_SIZE)))
		goto cancel;

	if (ctx->use_pbo) {
		/* is this safe, what happens if this is called simultaneously? */
		if ((ret = ps_packet_setsize(&ctx->packet, ctx->row * ctx->ch
								+ GLC_MESSAGE_HEADER_SIZE
								+ GLC_PICTURE_HEADER_SIZE)))
			goto cancel;

		if ((ret = gl_capture_read_pbo(gl_capture, ctx)))
			goto cancel;

		ret = gl_capture_start_pbo(gl_capture, ctx);
		ctx->pbo_timestamp = now;
	} else {
		if ((ret = ps_packet_dma(&ctx->packet, (void *) &dma,
					ctx->row * ctx->ch, PS_ACCEPT_FAKE_DMA)))
		goto cancel;

		ret = gl_capture_get_pixels(gl_capture, ctx, dma);
	}

	if (gl_capture->glc->flags & GLC_LOCK_FPS) {
		now = util_timestamp(gl_capture->glc);

		if (now - ctx->last < gl_capture->fps)
			usleep(gl_capture->fps + ctx->last - now);
	}

	ctx->last += gl_capture->fps;

	ps_packet_close(&ctx->packet);

finish:
	if (ret != 0)
		util_log(gl_capture->glc, GLC_ERROR, "gl_capture",
			 "%s (%d)", strerror(ret), ret);

	if (gl_capture->glc->flags & GLC_DRAW_INDICATOR) {
		if (!ctx->indicator_list)
			gl_capture_gen_indicator_list(gl_capture, ctx);
		glCallList(ctx->indicator_list);
	}

	return ret;
cancel:
	if (ret == EBUSY)
		ret = 0;
	ps_packet_cancel(&ctx->packet);
	goto finish;
}

/**  \} */
/**  \} */
