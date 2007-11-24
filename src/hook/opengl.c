/**
 * \file src/hook/opengl.c
 * \brief opengl wrapper
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* opengl.c -- opengl wrapper
 * Copyright (C) 2007 Pyry Haulos
 * For conditions of distribution and use, see copyright notice in glc.h
 */

#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#include "../common/glc.h"
#include "../common/util.h"
#include "../capture/gl_capture.h"
#include "../core/scale.h"
#include "../core/ycbcr.h"
#include "lib.h"

/**
 * \addtogroup hook
 *  \{
 */

/**
 * \defgroup opengl OpenGL wrapper
 *  \{
 */

struct opengl_private_s {
	glc_t *glc;
	ps_buffer_t *unscaled, *buffer;
	size_t unscaled_size;

	void *libGL_handle;
	void (*glXSwapBuffers)(Display *dpy, GLXDrawable drawable);
	void (*glFinish)(void);
	__GLXextFuncPtr (*glXGetProcAddressARB)(const GLubyte *);

	int capture_glfinish;
	int convert_ycbcr_420jpeg;

	int started;
};

__PRIVATE struct opengl_private_s opengl;

__PRIVATE void get_real_opengl();
__PRIVATE void opengl_capture_current();
__PRIVATE void opengl_draw_indicator();

int opengl_init(glc_t *glc)
{
	opengl.glc = glc;
	opengl.glc->flags |= GLC_SCALE;
	opengl.started = 0;

	util_log(opengl.glc, GLC_DEBUG, "opengl", "initializing");

	/* load environment variables */
	if (getenv("GLC_FPS"))
		opengl.glc->fps = atof(getenv("GLC_FPS"));
	else
		opengl.glc->fps = 30;

	if (getenv("GLC_COLORSPACE")) {
		if (!strcmp(getenv("GLC_COLORSPACE"), "420jpeg"))
			opengl.glc->flags |= GLC_CONVERT_420JPEG;
		else if (strcmp(getenv("GLC_COLORSPACE"), "bgr"))
			util_log(opengl.glc, GLC_WARNING, "opengl",
				 "unknown colorspace '%s'", getenv("GLC_COLORSPACE"));
	} else
		opengl.glc->flags |= GLC_CONVERT_420JPEG;

	if (getenv("GLC_UNSCALED_BUFFER_SIZE"))
		opengl.unscaled_size = atoi(getenv("GLC_UNSCALED_BUFFER_SIZE")) * 1024 * 1024;
	else
		opengl.unscaled_size = 1024 * 1024 * 25;

	if (getenv("GLC_CAPTURE")) {
		if (!strcmp(getenv("GLC_CAPTURE"), "front"))
			opengl.glc->flags |= GLC_CAPTURE_FRONT;
		else if (!strcmp(getenv("GLC_CAPTURE"), "back"))
			opengl.glc->flags |= GLC_CAPTURE_BACK;
		else
			util_log(opengl.glc, GLC_WARNING, "opengl",
				 "unknown capture buffer '%s'", getenv("GLC_CAPTURE"));
	} else
		opengl.glc->flags |= GLC_CAPTURE_FRONT;

	if (getenv("GLC_CAPTURE_GLFINISH"))
		opengl.capture_glfinish = atoi(getenv("GLC_CAPTURE_GLFINISH"));
	else
		opengl.capture_glfinish = 0;

	if (getenv("GLC_SCALE"))
		opengl.glc->scale = atof(getenv("GLC_SCALE"));
	else
		opengl.glc->scale = 1.0;

	if (getenv("GLC_TRY_PBO")) {
		if (atoi(getenv("GLC_TRY_PBO")))
			opengl.glc->flags |= GLC_TRY_PBO;
	} else
		opengl.glc->flags |= GLC_TRY_PBO;

	if (getenv("GLC_CAPTURE_DWORD_ALIGNED")) {
		if (atoi(getenv("GLC_CAPTURE_DWORD_ALIGNED")))
			opengl.glc->flags |= GLC_CAPTURE_DWORD_ALIGNED;
	} else
		opengl.glc->flags |= GLC_CAPTURE_DWORD_ALIGNED;

	if (getenv("GLC_CROP")) {
		opengl.glc->crop_width = opengl.glc->crop_height = 0;
		opengl.glc->crop_x = opengl.glc->crop_y = 0;

		/* we need at least 2 values, width and height */
		if (sscanf(getenv("GLC_CROP"), "%ux%u+%u+%u",
			   &opengl.glc->crop_width, &opengl.glc->crop_height,
			   &opengl.glc->crop_x, &opengl.glc->crop_y) >= 2)
			opengl.glc->flags |= GLC_CROP;
	}

	if (getenv("GLC_INDICATOR")) {
		if (atoi(getenv("GLC_INDICATOR")))
			opengl.glc->flags |= GLC_DRAW_INDICATOR;
	}

	if (getenv("GLC_LOCK_FPS")) {
		if (atoi(getenv("GLC_LOCK_FPS")))
			opengl.glc->flags |= GLC_LOCK_FPS;
	}

	get_real_opengl();
	return 0;
}

int opengl_start(ps_buffer_t *buffer)
{
	if (opengl.started)
		return EINVAL;

	opengl.buffer = buffer;

	if ((opengl.glc->scale == 1.0) && (!(opengl.glc->flags & GLC_CONVERT_420JPEG)))
		opengl.glc->flags &= ~GLC_SCALE; /* no scaling or conversion needed */

	/* init unscaled buffer if it is needed */
	if (opengl.glc->flags & GLC_SCALE) {
		/* capture in GL_BGRA format if scaling is enabled */
		opengl.glc->flags |= GLC_CAPTURE_BGRA;

		ps_bufferattr_t attr;
		ps_bufferattr_init(&attr);
		ps_bufferattr_setsize(&attr, opengl.unscaled_size);
		opengl.unscaled = (ps_buffer_t *) malloc(sizeof(ps_buffer_t));
		ps_buffer_init(opengl.unscaled, &attr);

		if (opengl.glc->flags & GLC_CONVERT_420JPEG)
			ycbcr_init(opengl.glc, opengl.unscaled, buffer);
		else
			scale_init(opengl.glc, opengl.unscaled, buffer);
		lib.gl = gl_capture_init(opengl.glc, opengl.unscaled);
	} else
		lib.gl = gl_capture_init(opengl.glc, buffer);

	if (!lib.gl)
		return EAGAIN;

	opengl.started = 1;
	return 0;
}

int opengl_close()
{
	int ret;
	if (!opengl.started)
		return 0;

	util_log(opengl.glc, GLC_DEBUG, "opengl", "closing");

	gl_capture_close(lib.gl);

	if (opengl.glc->flags & GLC_SCALE) {
		if (lib.running) {
			if ((ret = util_write_end_of_stream(opengl.glc, opengl.unscaled)))
				return ret;
		} else
			ps_buffer_cancel(opengl.unscaled);

		if (opengl.glc->flags & GLC_CONVERT_420JPEG)
			sem_wait(&opengl.glc->signal[GLC_SIGNAL_YCBCR_FINISHED]);
		else
			sem_wait(&opengl.glc->signal[GLC_SIGNAL_SCALE_FINISHED]);
	} else if (lib.running) {
		if ((ret = util_write_end_of_stream(opengl.glc, opengl.buffer)))
			return ret;
	} else
		ps_buffer_cancel(opengl.buffer);

	if (opengl.glc->flags & GLC_SCALE) {
		ps_buffer_destroy(opengl.unscaled);
		free(opengl.unscaled);
	}

	return 0;
}

void get_real_opengl()
{
	if (!lib.dlopen)
		get_real_dlsym();

	opengl.libGL_handle = lib.dlopen("libGL.so", RTLD_LAZY);
	if (!opengl.libGL_handle)
		goto err;
	opengl.glXSwapBuffers =
	  (void (*)(Display *, GLXDrawable))
	    lib.dlsym(opengl.libGL_handle, "glXSwapBuffers");
	if (!opengl.glXSwapBuffers)
		goto err;
	opengl.glFinish =
	  (void (*)(void))
	    lib.dlsym(opengl.libGL_handle, "glFinish");
	if (!opengl.glFinish)
		goto err;
	opengl.glXGetProcAddressARB =
	  (__GLXextFuncPtr (*)(const GLubyte *))
	    lib.dlsym(opengl.libGL_handle, "glXGetProcAddressARB");
	if (opengl.glXGetProcAddressARB)
		return;
err:
	fprintf(stderr, "(glc:opengl) can't get real OpenGL\n");
	exit(1);
}

__PUBLIC GLXextFuncPtr glXGetProcAddressARB(const GLubyte *proc_name)
{
	return __opengl_glXGetProcAddressARB(proc_name);
}

GLXextFuncPtr __opengl_glXGetProcAddressARB(const GLubyte *proc_name)
{
	INIT_GLC

	GLXextFuncPtr ret = (GLXextFuncPtr) wrapped_func((char *) proc_name);
	if (ret)
		return ret;

	return opengl.glXGetProcAddressARB(proc_name);
}

__PUBLIC void glXSwapBuffers(Display *dpy, GLXDrawable drawable)
{
	return __opengl_glXSwapBuffers(dpy, drawable);
}

void __opengl_glXSwapBuffers(Display *dpy, GLXDrawable drawable)
{
	INIT_GLC

	/* both flags shouldn't be defined */
	if (opengl.glc->flags & GLC_CAPTURE_FRONT)
		opengl.glXSwapBuffers(dpy, drawable);

	if (opengl.glc->flags & GLC_CAPTURE)
		gl_capture(lib.gl, dpy, drawable);

	if (opengl.glc->flags & GLC_CAPTURE_BACK)
		opengl.glXSwapBuffers(dpy, drawable);
}

__PUBLIC void glFinish(void)
{
	__opengl_glFinish();
}

void __opengl_glFinish(void)
{
	INIT_GLC

	opengl.glFinish();
	if (opengl.capture_glfinish)
		opengl_capture_current();
}

void opengl_capture_current()
{
	INIT_GLC

	Display *dpy = glXGetCurrentDisplay();
	GLXDrawable drawable = glXGetCurrentDrawable();

	if ((opengl.glc->flags & GLC_CAPTURE) && (dpy != NULL) && (drawable != None))
		gl_capture(lib.gl, dpy, drawable);
}


/**  \} */
/**  \} */
