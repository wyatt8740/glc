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

struct gl_ctx_s {
	Display *dpy;
	GLXDrawable drawable;
	unsigned int w, h;
	ps_packet_t packet;
	glc_ctx_i ctx_i;
	glc_utime_t last, pbo_timestamp;

	int indicator_list;
	
	struct gl_ctx_s *next;

	GLuint pbo;
	int pbo_active;
};

struct gl_private_s {
	glc_t *glc;
	GLenum capture_buffer;
	glc_utime_t fps;

	pthread_rwlock_t ctxlist_lock;
	struct gl_ctx_s *ctx;
	glc_ctx_i ctx_c;

	ps_buffer_t *to;

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

int gl_capture_get_ctx(struct gl_private_s *gl, struct gl_ctx_s **ctx, Display *dpy, GLXDrawable drawable);

int gl_capture_get_geometry(struct gl_private_s *gl, Display *dpy, GLXDrawable drawable, unsigned int *w, unsigned int *h);

int gl_capture_get_pixels(struct gl_private_s *gl, struct gl_ctx_s *ctx, char *to);
int gl_capture_gen_indicator_list(struct gl_private_s *gl, struct gl_ctx_s *ctx);

int gl_capture_init_pbo(struct gl_private_s *gl);
int gl_capture_create_pbo(struct gl_private_s *gl, struct gl_ctx_s *ctx);
int gl_capture_destroy_pbo(struct gl_private_s *gl, struct gl_ctx_s *ctx);
int gl_capture_start_pbo(struct gl_private_s *gl, struct gl_ctx_s *ctx);
int gl_capture_read_pbo(struct gl_private_s *gl, struct gl_ctx_s *ctx, char *to);

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
	struct gl_ctx_s *del;

	while (gl->ctx != NULL) {
		del = gl->ctx;
		gl->ctx = gl->ctx->next;
		
		/* we might be in wrong thread */
		if (del->indicator_list)
			glDeleteLists(del->indicator_list, 1);

		if (del->pbo)
			gl_capture_destroy_pbo(gl, del);
		
		free(del);
	}

	sem_post(&gl->glc->signal[GLC_SIGNAL_GL_CAPTURE_FINISHED]);
	if (gl->libGL_handle)
		dlclose(gl->libGL_handle);
	free(gl);
}

int gl_capture_get_geometry(struct gl_private_s *gl, Display *dpy, GLXDrawable drawable,
                    unsigned int *w, unsigned int *h)
{
	Window rootWindow;
	int unused;
	
	XGetGeometry(dpy, drawable, &rootWindow, &unused, &unused, w, h,
	             (unsigned int *) &unused, (unsigned int *) &unused);
	
	return 0;
}

int gl_capture_get_pixels(struct gl_private_s *gl, struct gl_ctx_s *ctx, char *to)
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

int gl_capture_gen_indicator_list(struct gl_private_s *gl, struct gl_ctx_s *ctx)
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

int gl_capture_init_pbo(struct gl_private_s *gl)
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

int gl_capture_create_pbo(struct gl_private_s *gl, struct gl_ctx_s *ctx)
{
	glPushAttrib(GL_ALL_ATTRIB_BITS);

	gl->glGenBuffers(1, &ctx->pbo);
	gl->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, ctx->pbo);
	gl->glBufferData(GL_PIXEL_PACK_BUFFER_ARB, ctx->w * ctx->h * gl->bpp,
		         NULL, GL_STREAM_READ);

	glPopAttrib();
	return 0;
}

int gl_capture_destroy_pbo(struct gl_private_s *gl, struct gl_ctx_s *ctx)
{
	gl->glDeleteBuffers(1, &ctx->pbo);
	return 0;
}

int gl_capture_start_pbo(struct gl_private_s *gl, struct gl_ctx_s *ctx)
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

int gl_capture_read_pbo(struct gl_private_s *gl, struct gl_ctx_s *ctx, char *to)
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

int gl_capture_get_ctx(struct gl_private_s *gl, struct gl_ctx_s **ctx, Display *dpy, GLXDrawable drawable)
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
			if (!gl_capture_init_pbo(gl))
				gl->use_pbo = 1;
			gl->try_pbo = 0;
		}
		pthread_rwlock_unlock(&gl->ctxlist_lock);
		
		gl_capture_get_geometry(gl, dpy, drawable, &fctx->w, &fctx->h);
		
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
			gl_capture_create_pbo(gl, fctx);
		gl_capture_gen_indicator_list(gl, fctx);
	} else {
		gl_capture_get_geometry(gl, dpy, drawable, &w, &h);
		
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

			gl_capture_gen_indicator_list(gl, fctx);

			if (gl->use_pbo) {
				gl_capture_destroy_pbo(gl, fctx);
				gl_capture_create_pbo(gl, fctx);
			}
		}
	}
	
	*ctx = fctx;
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
	
	gl_capture_get_ctx(gl, &ctx, dpy, drawable);

	if ((gl->use_pbo) && (!ctx->pbo_active)) {
		ret = gl_capture_start_pbo(gl, ctx);
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
			gl_capture_read_pbo(gl, ctx, dma);
			ret = gl_capture_start_pbo(gl, ctx);
			ctx->pbo_timestamp = now;
		} else
			ret = gl_capture_get_pixels(gl, ctx, dma);
		
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

/**  \} */
/**  \} */
