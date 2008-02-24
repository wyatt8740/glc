/**
 * \file src/capture/gl_capture.c
 * \brief OpenGL capture
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup capture
 *  \{
 * \defgroup gl_capture OpenGL capture
 *  \{
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/extensions/xf86vmode.h>
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

#define GL_CAPTURE_TRY_PBO          0x1
#define GL_CAPTURE_USE_PBO          0x2
#define GL_CAPTURE_CAPTURING        0x4
#define GL_CAPTURE_DRAW_INDICATOR   0x8
#define GL_CAPTURE_CROP            0x10
#define GL_CAPTURE_LOCK_FPS        0x20

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
	int screen;
	GLXDrawable drawable;
	ps_packet_t packet;
	glc_ctx_i ctx_i;
	glc_utime_t last, pbo_timestamp;

	unsigned int w, h;
	unsigned int cw, ch, row, cx, cy;

	float brightness, contrast;
	float gamma_red, gamma_green, gamma_blue;

	int indicator_list;

	struct gl_capture_ctx_s *next;

	GLuint pbo;
	int pbo_active;
};

struct gl_capture_s {
	glc_t *glc;
	glc_flags_t flags;

	GLenum capture_buffer;
	glc_utime_t fps;

	pthread_rwlock_t ctxlist_lock;
	struct gl_capture_ctx_s *ctx;
	glc_ctx_i ctx_c;

	ps_buffer_t *to;

	pthread_mutex_t init_pbo_mutex;

	unsigned int bpp;
	GLenum format;
	GLint pack_alignment;

	unsigned int crop_x, crop_y;
	unsigned int crop_w, crop_h;

	void *libGL_handle;
	GLXGetProcAddressProc glXGetProcAddress;
	glGenBuffersProc glGenBuffers;
	glDeleteBuffersProc glDeleteBuffers;
	glBufferDataProc glBufferData;
	glBindBufferProc glBindBuffer;
	glMapBufferProc glMapBuffer;
	glUnmapBufferProc glUnmapBuffer;
};

int gl_capture_get_ctx(gl_capture_t gl_capture,
		       struct gl_capture_ctx_s **ctx, Display *dpy, GLXDrawable drawable);
int gl_capture_update_ctx(gl_capture_t gl_capture,
			  struct gl_capture_ctx_s *ctx);

int gl_capture_get_geometry(gl_capture_t gl_capture,
			    Display *dpy, GLXDrawable drawable, unsigned int *w, unsigned int *h);
int gl_capture_calc_geometry(gl_capture_t gl_capture, struct gl_capture_ctx_s *ctx,
			     unsigned int w, unsigned int h);
int gl_capture_update_screen(gl_capture_t gl_capture, struct gl_capture_ctx_s *ctx);
int gl_capture_update_color(gl_capture_t gl_capture, struct gl_capture_ctx_s *ctx);

int gl_capture_get_pixels(gl_capture_t gl_capture, struct gl_capture_ctx_s *ctx, char *to);
int gl_capture_gen_indicator_list(gl_capture_t gl_capture, struct gl_capture_ctx_s *ctx);

int gl_capture_init_pbo(gl_capture_t gl);
int gl_capture_create_pbo(gl_capture_t gl_capture, struct gl_capture_ctx_s *ctx);
int gl_capture_destroy_pbo(gl_capture_t gl_capture, struct gl_capture_ctx_s *ctx);
int gl_capture_start_pbo(gl_capture_t gl_capture, struct gl_capture_ctx_s *ctx);
int gl_capture_read_pbo(gl_capture_t gl_capture, struct gl_capture_ctx_s *ctx);

int gl_capture_init(gl_capture_t *gl_capture, glc_t *glc)
{
	*gl_capture = (gl_capture_t) malloc(sizeof(struct gl_capture_s));
	memset(*gl_capture, 0, sizeof(struct gl_capture_s));

	(*gl_capture)->glc = glc;
	(*gl_capture)->fps = 1000000 / 30;		/* default fps is 30 */
	(*gl_capture)->pack_alignment = 8;		/* read as dword aligned by default */
	(*gl_capture)->flags |= GL_CAPTURE_TRY_PBO;	/* try pbo by default */
	(*gl_capture)->format = GL_BGRA;		/* capture as BGRA data by default */

	pthread_mutex_init(&(*gl_capture)->init_pbo_mutex, NULL);
	pthread_rwlock_init(&(*gl_capture)->ctxlist_lock, NULL);

	return 0;
}

int gl_capture_set_buffer(gl_capture_t gl_capture, ps_buffer_t *buffer)
{
	if (gl_capture->to)
		return EALREADY;

	gl_capture->to = buffer;
	return 0;
}

int gl_capture_set_read_buffer(gl_capture_t gl_capture, GLenum buffer)
{
	if (buffer == GL_FRONT)
		util_log(gl_capture->glc, GLC_INFORMATION, "gl_capture",
			 "reading frames from GL_FRONT");
	else if (buffer == GL_BACK)
		util_log(gl_capture->glc, GLC_INFORMATION, "gl_capture",
			 "reading frames from GL_BACK");
	else {
		util_log(gl_capture->glc, GLC_ERROR, "gl_capture",
			 "unknown read buffer 0x%02x", buffer);
		return ENOTSUP;
	}

	gl_capture->capture_buffer = buffer;
	return 0;
}

int gl_capture_set_fps(gl_capture_t gl_capture, double fps)
{
	if (fps <= 0)
		return EINVAL;

	gl_capture->fps = 1000000 / fps;
	util_log(gl_capture->glc, GLC_INFORMATION, "gl_capture",
		 "capturing at %f fps", fps);

	return 0;
}

int gl_capture_set_pack_alignment(gl_capture_t gl_capture, GLint pack_alignment)
{
	if (pack_alignment == 1)
		util_log(gl_capture->glc, GLC_INFORMATION, "gl_capture",
			 "reading data as byte aligned");
	else if (pack_alignment == 8)
		util_log(gl_capture->glc, GLC_INFORMATION, "gl_capture",
			 "reading data as dword aligned");
	else {
		util_log(gl_capture->glc, GLC_ERROR, "gl_capture",
			 "unknown GL_PACK_ALIGNMENT %d", pack_alignment);
		return ENOTSUP;
	}

	gl_capture->pack_alignment = pack_alignment;
	return 0;
}

int gl_capture_try_pbo(gl_capture_t gl_capture, int try_pbo)
{
	if (try_pbo) {
		gl_capture->flags |= GL_CAPTURE_TRY_PBO;
	} else {
		if (gl_capture->flags & GL_CAPTURE_USE_PBO) {
			util_log(gl_capture->glc, GLC_WARNING, "gl_capture",
				 "can't disable PBO; it is in use");
			return EAGAIN;
		}

		util_log(gl_capture->glc, GLC_DEBUG, "gl_capture",
			 "PBO disabled");
		gl_capture->flags &= ~GL_CAPTURE_TRY_PBO;
	}

	return 0;
}

int gl_capture_set_pixel_format(gl_capture_t gl_capture, GLenum format)
{
	if (format == GL_BGRA) {
		util_log(gl_capture->glc, GLC_INFORMATION, "gl_capture",
			 "reading frames in GL_BGRA format");
		gl_capture->bpp = 4;
	} else if (format == GL_BGR) {
		util_log(gl_capture->glc, GLC_INFORMATION, "gl_capture",
			 "reading frames in GL_BGR format");
		gl_capture->bpp = 3;
	} else {
		util_log(gl_capture->glc, GLC_ERROR, "gl_capture",
			 "unsupported pixel format 0x%02x", format);
		return ENOTSUP;
	}

	gl_capture->format = format;
	return 0;
}

int gl_capture_draw_indicator(gl_capture_t gl_capture, int draw_indicator)
{
	if (draw_indicator) {
		gl_capture->flags |= GL_CAPTURE_DRAW_INDICATOR;

		if (gl_capture->capture_buffer == GL_FRONT)
			util_log(gl_capture->glc, GLC_WARNING, "gl_capture",
				 "indicator doesn't work well when capturing from GL_FRONT");
	} else
		gl_capture->flags &= ~GL_CAPTURE_DRAW_INDICATOR;

	return 0;
}

int gl_capture_crop(gl_capture_t gl_capture, unsigned int x, unsigned int y,
			     unsigned int width, unsigned int height)
{
	if ((!x) && (!y) && (!width) && (!height)) {
		gl_capture->flags &= ~GL_CAPTURE_CROP;
		return 0;
	}

	gl_capture->crop_x = x;
	gl_capture->crop_y = y;
	gl_capture->crop_w = width;
	gl_capture->crop_h = height;
	gl_capture->flags |= GL_CAPTURE_CROP;

	return 0;
}

int gl_capture_lock_fps(gl_capture_t gl_capture, int lock_fps)
{
	if (lock_fps)
		gl_capture->flags |= GL_CAPTURE_LOCK_FPS;
	else
		gl_capture->flags &= ~GL_CAPTURE_LOCK_FPS;

	return 0;
}

int gl_capture_start(gl_capture_t gl_capture)
{
	if (!gl_capture->to) {
		util_log(gl_capture->glc, GLC_ERROR, "gl_capture",
			 "no target buffer specified");
		return EAGAIN;
	}

	if (gl_capture->flags & GL_CAPTURE_CAPTURING)
		util_log(gl_capture->glc, GLC_WARNING, "gl_capture",
			 "capturing is already active");
	else
		util_log(gl_capture->glc, GLC_INFORMATION, "gl_capture",
			 "starting capturing");

	gl_capture->flags |= GL_CAPTURE_CAPTURING;
	return 0;
}

int gl_capture_stop(gl_capture_t gl_capture)
{
	if (gl_capture->flags & GL_CAPTURE_CAPTURING)
		util_log(gl_capture->glc, GLC_INFORMATION, "gl_capture",
			 "stopping capturing");
	else
		util_log(gl_capture->glc, GLC_WARNING, "gl_capture",
			 "capturing is already stopped");

	gl_capture->flags &= ~GL_CAPTURE_CAPTURING;
	return 0;
}

int gl_capture_destroy(gl_capture_t gl_capture)
{
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

	pthread_rwlock_destroy(&gl_capture->ctxlist_lock);
	pthread_mutex_destroy(&gl_capture->init_pbo_mutex);

	if (gl_capture->libGL_handle)
		dlclose(gl_capture->libGL_handle);
	free(gl_capture);

	return 0;
}

int gl_capture_get_geometry(gl_capture_t gl_capture, Display *dpy, GLXDrawable drawable,
                    unsigned int *w, unsigned int *h)
{
	Window rootWindow;
	int unused;
	
	XGetGeometry(dpy, drawable, &rootWindow, &unused, &unused, w, h,
	             (unsigned int *) &unused, (unsigned int *) &unused);
	
	return 0;
}

int gl_capture_update_screen(gl_capture_t gl_capture, struct gl_capture_ctx_s *ctx)
{
	/** \todo figure out real screen */
	ctx->screen = DefaultScreen(ctx->dpy);
	return 0;
}

int gl_capture_calc_geometry(gl_capture_t gl_capture, struct gl_capture_ctx_s *ctx,
			     unsigned int w, unsigned int h)
{
	ctx->w = w;
	ctx->h = h;

	/* calculate image area when cropping */
	if (gl_capture->flags & GL_CAPTURE_CROP) {
		if (gl_capture->crop_x > ctx->w)
			ctx->cx = 0;
		else
			ctx->cx = gl_capture->crop_x;

		if (gl_capture->crop_y > ctx->h)
			ctx->cy = 0;
		else
			ctx->cy = gl_capture->crop_y;

		if (gl_capture->crop_w + ctx->cx > ctx->w)
			ctx->cw = ctx->w - ctx->cx;
		else
			ctx->cw = gl_capture->crop_w;

		if (gl_capture->crop_h + ctx->cy > ctx->h)
			ctx->ch = ctx->h - ctx->cy;
		else
			ctx->ch = gl_capture->crop_h;

		/* we need to recalc y coord for OpenGL */
		ctx->cy = ctx->h - ctx->ch - ctx->cy;
	} else {
		ctx->cw = ctx->w;
		ctx->ch = ctx->h;
		ctx->cx = ctx->cy = 0;
	}

	util_log(gl_capture->glc, GLC_DEBUG, "gl_capture",
		 "calculated capture area for ctx %d is %ux%u+%u+%u",
		 ctx->ctx_i, ctx->cw, ctx->ch, ctx->cx, ctx->cy);

	ctx->row = ctx->cw * gl_capture->bpp;
	if (ctx->row % gl_capture->pack_alignment != 0)
		ctx->row += gl_capture->pack_alignment - ctx->row % gl_capture->pack_alignment;

	return 0;
}

int gl_capture_get_pixels(gl_capture_t gl_capture, struct gl_capture_ctx_s *ctx, char *to)
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

int gl_capture_gen_indicator_list(gl_capture_t gl_capture, struct gl_capture_ctx_s *ctx)
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

int gl_capture_init_pbo(gl_capture_t gl_capture)
{
	const char *gl_extensions = (const char *) glGetString(GL_EXTENSIONS);

	if (gl_extensions == NULL)
		return EINVAL;
	
	if (!strstr(gl_extensions, "GL_ARB_pixel_buffer_object"))
		return ENOTSUP;
	
	gl_capture->libGL_handle = dlopen("libGL.so.1", RTLD_LAZY);
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

int gl_capture_create_pbo(gl_capture_t gl_capture, struct gl_capture_ctx_s *ctx)
{
	GLint binding;

	util_log(gl_capture->glc, GLC_DEBUG, "gl_capture", "creating PBO");

	glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING_ARB, &binding);
	glPushAttrib(GL_ALL_ATTRIB_BITS);

	gl_capture->glGenBuffers(1, &ctx->pbo);
	gl_capture->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, ctx->pbo);
	gl_capture->glBufferData(GL_PIXEL_PACK_BUFFER_ARB, ctx->row * ctx->ch,
		         NULL, GL_STREAM_READ);

	glPopAttrib();
	gl_capture->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, binding);
	return 0;
}

int gl_capture_destroy_pbo(gl_capture_t gl_capture, struct gl_capture_ctx_s *ctx)
{
	util_log(gl_capture->glc, GLC_DEBUG, "gl_capture", "destroying PBO");

	gl_capture->glDeleteBuffers(1, &ctx->pbo);
	return 0;
}

int gl_capture_start_pbo(gl_capture_t gl_capture, struct gl_capture_ctx_s *ctx)
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

int gl_capture_read_pbo(gl_capture_t gl_capture, struct gl_capture_ctx_s *ctx)
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

int gl_capture_get_ctx(gl_capture_t gl_capture, struct gl_capture_ctx_s **ctx, Display *dpy, GLXDrawable drawable)
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

int gl_capture_update_ctx(gl_capture_t gl_capture,
			  struct gl_capture_ctx_s *ctx)
{
	glc_message_header_t msg;
	glc_ctx_message_t ctx_msg;
	unsigned int w, h;

	/* initialize PBO if not already done */
	if ((!(gl_capture->flags & GL_CAPTURE_USE_PBO)) &&
	    (gl_capture->flags & GL_CAPTURE_TRY_PBO)) {
		pthread_mutex_lock(&gl_capture->init_pbo_mutex);

		if (!gl_capture_init_pbo(gl_capture))
			gl_capture->flags |= GL_CAPTURE_USE_PBO;
		else
			gl_capture->flags &= ~GL_CAPTURE_TRY_PBO;

		pthread_mutex_unlock(&gl_capture->init_pbo_mutex);
	}

	gl_capture_get_geometry(gl_capture, ctx->dpy, ctx->drawable, &w, &h);

	if (ctx->flags == 0) {
		/* initialize screen information */
		gl_capture_update_screen(gl_capture, ctx);

		/* reset gamma values */
		ctx->gamma_red = ctx->gamma_green = ctx->gamma_blue = 1.0;

		ctx->flags |= GLC_CTX_CREATE;

		if (gl_capture->format == GL_BGRA)
			ctx->flags |= GLC_CTX_BGRA;
		else if (gl_capture->format == GL_BGR)
			ctx->flags |= GLC_CTX_BGR;
		else
			return EINVAL;

		if (gl_capture->pack_alignment == 8)
			ctx->flags |= GLC_CTX_DWORD_ALIGNED;
	} else if (ctx->flags & GLC_CTX_CREATE) {
		ctx->flags &= ~GLC_CTX_CREATE;
		ctx->flags |= GLC_CTX_UPDATE;
	}

	if ((w != ctx->w) | (h != ctx->h) | (ctx->flags & GLC_CTX_CREATE)) {
		gl_capture_calc_geometry(gl_capture, ctx, w, h);

		util_log(gl_capture->glc, GLC_INFORMATION, "gl_capture",
			 "creating/updating configuration for ctx %d", ctx->ctx_i);

		msg.type = GLC_MESSAGE_CTX;
		ctx_msg.flags = ctx->flags;
		ctx_msg.ctx = ctx->ctx_i;
		ctx_msg.w = ctx->cw;
		ctx_msg.h = ctx->ch;

		ps_packet_open(&ctx->packet, PS_PACKET_WRITE);
		ps_packet_write(&ctx->packet, &msg, GLC_MESSAGE_HEADER_SIZE);
		ps_packet_write(&ctx->packet, &ctx_msg, GLC_CTX_MESSAGE_SIZE);
		ps_packet_close(&ctx->packet);

		util_log(gl_capture->glc, GLC_DEBUG, "gl_capture",
			 "ctx %d: %ux%u (%ux%u), 0x%02x flags", ctx->ctx_i,
			 ctx->cw, ctx->ch, ctx->w, ctx->h, ctx->flags);

		/* how about color correction? */
		gl_capture_update_color(gl_capture, ctx);

		if (gl_capture->flags & GL_CAPTURE_USE_PBO) {
			if (ctx->pbo)
				gl_capture_destroy_pbo(gl_capture, ctx);

			if (gl_capture_create_pbo(gl_capture, ctx)) {
				gl_capture->flags &= ~(GL_CAPTURE_TRY_PBO | GL_CAPTURE_USE_PBO);
				/** \todo destroy pbo stuff? */
				/** \todo race condition? */
			}
		}
	}


	if ((gl_capture->flags & GL_CAPTURE_DRAW_INDICATOR) &&
	    (!ctx->indicator_list))
		gl_capture_gen_indicator_list(gl_capture, ctx);

	return 0;
}

int gl_capture_frame(gl_capture_t gl_capture, Display *dpy, GLXDrawable drawable)
{
	struct gl_capture_ctx_s *ctx;
	glc_message_header_t msg;
	glc_picture_header_t pic;
	glc_utime_t now;
	char *dma;
	int ret = 0;

	if (!(gl_capture->flags & GL_CAPTURE_CAPTURING))
		return 0; /* capturing not active */

	gl_capture_get_ctx(gl_capture, &ctx, dpy, drawable);

	msg.type = GLC_MESSAGE_PICTURE;
	pic.ctx = ctx->ctx_i;

	now = util_time(gl_capture->glc);
	if (gl_capture->flags & GL_CAPTURE_USE_PBO)
		pic.timestamp = ctx->pbo_timestamp;
	else
		pic.timestamp = now;

	/* has gl_capture->fps microseconds elapsed since last capture */
	if ((now - ctx->last < gl_capture->fps) &&
	    !(gl_capture->flags & GL_CAPTURE_LOCK_FPS))
		goto finish;

	/* not really needed until now */
	gl_capture_update_ctx(gl_capture, ctx);

	if ((gl_capture->flags & GL_CAPTURE_USE_PBO) && (!ctx->pbo_active)) {
		ret = gl_capture_start_pbo(gl_capture, ctx);
		ctx->pbo_timestamp = util_time(gl_capture->glc);
		goto finish;
	}

	if (ps_packet_open(&ctx->packet, gl_capture->flags & GL_CAPTURE_LOCK_FPS ?
					 PS_PACKET_WRITE :
					 PS_PACKET_WRITE | PS_PACKET_TRY))
		goto finish;
	if ((ret = ps_packet_write(&ctx->packet, &msg, GLC_MESSAGE_HEADER_SIZE)))
		goto cancel;
	if ((ret = ps_packet_write(&ctx->packet, &pic, GLC_PICTURE_HEADER_SIZE)))
		goto cancel;

	if (gl_capture->flags & GL_CAPTURE_USE_PBO) {
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

	if (gl_capture->flags & GL_CAPTURE_LOCK_FPS) {
		now = util_time(gl_capture->glc);

		if (now - ctx->last < gl_capture->fps)
			usleep(gl_capture->fps + ctx->last - now);
	}

	/*
	 We should accept framedrops (eg. not allow this difference
	 to grow unlimited.
	*/
	ctx->last += gl_capture->fps;
	now = util_time(gl_capture->glc);

	if (now - ctx->last > gl_capture->fps) /* reasonable choice? */
		ctx->last = now - 0.5 * gl_capture->fps;

	ps_packet_close(&ctx->packet);

finish:
	if (ret != 0)
		util_log(gl_capture->glc, GLC_ERROR, "gl_capture",
			 "%s (%d)", strerror(ret), ret);

	if (gl_capture->flags & GL_CAPTURE_DRAW_INDICATOR)
		glCallList(ctx->indicator_list);

	return ret;
cancel:
	if (ret == EBUSY) {
		ret = 0;
		util_log(gl_capture->glc, GLC_INFORMATION, "gl_capture",
			 "dropped frame, buffer not ready");
	}
	ps_packet_cancel(&ctx->packet);
	goto finish;
}

int gl_capture_refresh_color_correction(gl_capture_t gl_capture)
{
	struct gl_capture_ctx_s *ctx;

	util_log(gl_capture->glc, GLC_INFORMATION, "gl_capture",
		 "refreshing color correction");

	pthread_rwlock_rdlock(&gl_capture->ctxlist_lock);
	ctx = gl_capture->ctx;
	while (ctx != NULL) {
		gl_capture_update_color(gl_capture, ctx);
		
		ctx = ctx->next;
	}
	pthread_rwlock_unlock(&gl_capture->ctxlist_lock);

	return 0;
}

/** \todo support GammaRamp */
int gl_capture_update_color(gl_capture_t gl_capture, struct gl_capture_ctx_s *ctx)
{
	glc_message_header_t msg_hdr;
	glc_color_message_t msg;
	XF86VidModeGamma gamma;
	int ret = 0;

	XF86VidModeGetGamma(ctx->dpy, ctx->screen, &gamma);

	if ((gamma.red == ctx->gamma_red) &&
	    (gamma.green == ctx->gamma_green) &&
	    (gamma.blue == ctx->gamma_blue))
		return 0; /* nothing to update */

	msg_hdr.type = GLC_MESSAGE_COLOR;
	msg.ctx = ctx->ctx_i;
	msg.red = gamma.red;
	msg.green = gamma.green;
	msg.blue = gamma.blue;

	/** \todo figure out brightness and contrast */
	msg.brightness = msg.contrast = 0;

	util_log(gl_capture->glc, GLC_INFORMATION, "gl_capture",
		 "color correction: brightness=%f, contrast=%f, red=%f, green=%f, blue=%f",
		 msg.brightness, msg.contrast, msg.red, msg.green, msg.blue);

	if ((ret = ps_packet_open(&ctx->packet, PS_PACKET_WRITE)))
		goto err;
	if ((ret = ps_packet_write(&ctx->packet, &msg_hdr, GLC_MESSAGE_HEADER_SIZE)))
		goto err;
	if ((ret = ps_packet_write(&ctx->packet, &msg, GLC_COLOR_MESSAGE_SIZE)))
		goto err;
	if ((ret = ps_packet_close(&ctx->packet)))
		goto err;

	return 0;

err:
	ps_packet_cancel(&ctx->packet);

	util_log(gl_capture->glc, GLC_ERROR, "gl_capture",
		 "can't write gamma correction information to buffer: %s (%d)",
		 strerror(ret), ret);
	return ret;
}

/**  \} */
/**  \} */
