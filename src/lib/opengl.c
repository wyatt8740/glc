/**
 * \file src/lib/opengl.c
 * \brief opengl wrapper
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* opengl.c -- opengl wrapper
 * Copyright (C) 2007 Pyry Haulos
 * For conditions of distribution and use, see copyright notice in glc.h
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#include "../common/glc.h"
#include "../common/util.h"
#include "../stream/gl.h"
#include "../stream/scale.h"
#include "../stream/ycbcr.h"
#include "lib.h"

/**
 * \addtogroup lib
 *  \{
 */

/**
 * \defgroup opengl opengl wrapper
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
	void *gl;

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
	
	/* load environment variables */
	if (getenv("GLC_FPS"))
		opengl.glc->fps = atoi(getenv("GLC_FPS"));
	else
		opengl.glc->fps = 30;

	if (getenv("GLC_COLORSPACE")) {
		if (!strcmp(getenv("GLC_COLORSPACE"), "420jpeg"))
			opengl.glc->flags |= GLC_CONVERT_420JPEG;
		else if (strcmp(getenv("GLC_COLORSPACE"), "bgr"))
			fprintf(stderr, "glc: unknown colorspace '%s'\n", getenv("GLC_COLORSPACE"));
	} else
		opengl.glc->flags |= GLC_CONVERT_420JPEG;

	if (getenv("GLC_UNSCALED_BUFFER_SIZE"))
		opengl.unscaled_size = atoi(getenv("GLC_UNSCALED_BUFFER_SIZE")) * 1024 * 1024;
	else
		opengl.unscaled_size = 1024 * 1024 * 10;
	
	if (getenv("GLC_CAPTURE")) {
		if (!strcmp(getenv("GLC_CAPTURE"), "front"))
			opengl.glc->flags |= GLC_CAPTURE_FRONT;
		else /* back */
			opengl.glc->flags |= GLC_CAPTURE_BACK;
	} else
		opengl.glc->flags |= GLC_CAPTURE_BACK;
	
	if (getenv("GLC_CAPTURE_GLFINISH"))
		opengl.capture_glfinish = atoi(getenv("GLC_CAPTURE_GLFINISH"));
	else
		opengl.capture_glfinish = 0;
	
	if (getenv("GLC_SCALE"))
		opengl.glc->scale = atof(getenv("GLC_SCALE"));
	else
		opengl.glc->scale = 1.0;

	/* Y'CbCr upscaling is broken... probably serious :/ */
	if (opengl.glc->scale > 1.0)
		opengl.glc->scale = 1.0;

	if (getenv("GLC_CAPTURE_BGRA")) {
		if (atoi(getenv("GLC_CAPTURE_BGRA")))
			opengl.glc->flags |= GLC_CAPTURE_BGRA;
	}

	if (getenv("GLC_TRY_PBO")) {
		if (atoi(getenv("GLC_TRY_PBO")))
			opengl.glc->flags |= GLC_TRY_PBO;
	} else
		opengl.glc->flags |= GLC_TRY_PBO;

	get_real_opengl();
	return 0;
}

int opengl_start(ps_buffer_t *buffer)
{
	if (opengl.started)
		return EINVAL;

	opengl.buffer = buffer;

	if ((opengl.glc->scale == 1.0) && (!(opengl.glc->flags & GLC_CAPTURE_BGRA)) && (!(opengl.glc->flags & GLC_CONVERT_420JPEG)))
		opengl.glc->flags &= ~GLC_SCALE; /* no scaling or conversion needed */

	/* init unscaled buffer if it is needed */
	if (opengl.glc->flags & GLC_SCALE) {
		ps_bufferattr_t attr;
		ps_bufferattr_init(&attr);
		ps_bufferattr_setsize(&attr, opengl.unscaled_size);
		opengl.unscaled = (ps_buffer_t *) malloc(sizeof(ps_buffer_t));
		ps_buffer_init(opengl.unscaled, &attr);

		if (opengl.glc->flags & GLC_CONVERT_420JPEG)
			ycbcr_init(opengl.glc, opengl.unscaled, buffer);
		else
			scale_init(opengl.glc, opengl.unscaled, buffer);
		opengl.gl = gl_capture_init(opengl.glc, opengl.unscaled);
	} else
		opengl.gl = gl_capture_init(opengl.glc, buffer);

	if (!opengl.gl)
		return EAGAIN;

	opengl.started = 1;
	return 0;
}

int opengl_close()
{
	int ret;
	if (!opengl.started)
		return 0;
	
	gl_capture_close(opengl.gl);
	
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
	
	/*if (opengl.libGL_handle)
		dlclose(opengl.libGL_handle);*/
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
	fprintf(stderr, "can't get real OpenGL\n");
	exit(1);
}

__GLXextFuncPtr glXGetProcAddressARB(const GLubyte *proc_name)
{
	__GLXextFuncPtr ret = (__GLXextFuncPtr) wrapped_func((char *) proc_name);
	if (ret)
		return ret;
	
	return opengl.glXGetProcAddressARB(proc_name);
}

void glXSwapBuffers(Display *dpy, GLXDrawable drawable)
{
	/* both flags shouldn't be defined */
	if (opengl.glc->flags & GLC_CAPTURE_FRONT)
		opengl.glXSwapBuffers(dpy, drawable);
	
	if (opengl.glc->flags & GLC_CAPTURE)
		gl_capture(opengl.gl, dpy, drawable);
	
	if (opengl.glc->flags & GLC_CAPTURE_BACK)
		opengl.glXSwapBuffers(dpy, drawable);
}

void glFinish(void)
{
	opengl.glFinish();
	if (opengl.capture_glfinish)
		opengl_capture_current();
}

void opengl_capture_current()
{
	Display *dpy = glXGetCurrentDisplay();
	GLXDrawable drawable = glXGetCurrentDrawable();
	
	if ((opengl.glc->flags & GLC_CAPTURE) && (dpy != NULL) && (drawable != None))
		gl_capture(opengl.gl, dpy, drawable);
}


/**  \} */
/**  \} */
