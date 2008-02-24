/**
 * \file src/hook/opengl.c
 * \brief opengl wrapper
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup hook
 *  \{
 * \defgroup opengl OpenGL wrapper
 *  \{
 */

#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#include "../common/glc.h"
#include "../common/util.h"
#include "../core/scale.h"
#include "../core/ycbcr.h"
#include "../capture/gl_capture.h"
#include "lib.h"

struct opengl_private_s {
	glc_t *glc;

	gl_capture_t gl_capture;
	ycbcr_t ycbcr;
	scale_t scale;

	ps_buffer_t *unscaled, *buffer;
	size_t unscaled_size;

	void *libGL_handle;
	void (*glXSwapBuffers)(Display *dpy, GLXDrawable drawable);
	void (*glFinish)(void);
	__GLXextFuncPtr (*glXGetProcAddressARB)(const GLubyte *);

	int capture_glfinish;
	int convert_ycbcr_420jpeg;
	double scale_factor;
	GLenum read_buffer;

	int started;
	int capturing;
};

__PRIVATE struct opengl_private_s opengl;

__PRIVATE void get_real_opengl();
__PRIVATE void opengl_capture_current();
__PRIVATE void opengl_draw_indicator();

int opengl_init(glc_t *glc)
{
	opengl.glc = glc;
	opengl.buffer = opengl.unscaled = NULL;
	opengl.started = 0;
	opengl.scale_factor = 1.0;
	opengl.capture_glfinish = 0;
	opengl.read_buffer = GL_FRONT;
	opengl.capturing = 0;
	int ret = 0;
	unsigned int x, y, w, h;

	util_log(opengl.glc, GLC_DEBUG, "opengl", "initializing");

	/* initialize gl_capture object */
	if ((ret = gl_capture_init(&opengl.gl_capture, opengl.glc)))
		return ret;

	/* load environment variables */
	if (getenv("GLC_FPS"))
		opengl.glc->fps = atof(getenv("GLC_FPS"));
	else
		opengl.glc->fps = 30;
	gl_capture_set_fps(opengl.gl_capture, opengl.glc->fps);

	if (getenv("GLC_COLORSPACE")) {
		if (!strcmp(getenv("GLC_COLORSPACE"), "420jpeg"))
			opengl.convert_ycbcr_420jpeg = 1;
		else if (!strcmp(getenv("GLC_COLORSPACE"), "bgr"))
			opengl.convert_ycbcr_420jpeg = 0;
		else
			util_log(opengl.glc, GLC_WARNING, "opengl",
				 "unknown colorspace '%s'", getenv("GLC_COLORSPACE"));
	} else
		opengl.convert_ycbcr_420jpeg = 1;

	if (getenv("GLC_UNSCALED_BUFFER_SIZE"))
		opengl.unscaled_size = atoi(getenv("GLC_UNSCALED_BUFFER_SIZE")) * 1024 * 1024;
	else
		opengl.unscaled_size = 1024 * 1024 * 25;

	if (getenv("GLC_CAPTURE")) {
		if (!strcmp(getenv("GLC_CAPTURE"), "front"))
			opengl.read_buffer = GL_FRONT;
		else if (!strcmp(getenv("GLC_CAPTURE"), "back"))
			opengl.read_buffer = GL_BACK;
		else
			util_log(opengl.glc, GLC_WARNING, "opengl",
				 "unknown capture buffer '%s'", getenv("GLC_CAPTURE"));
	}
	gl_capture_set_read_buffer(opengl.gl_capture, opengl.read_buffer);

	if (getenv("GLC_CAPTURE_GLFINISH"))
		opengl.capture_glfinish = atoi(getenv("GLC_CAPTURE_GLFINISH"));

	if (getenv("GLC_SCALE"))
		opengl.scale_factor = atof(getenv("GLC_SCALE"));

	gl_capture_try_pbo(opengl.gl_capture, 1);
	if (getenv("GLC_TRY_PBO"))
		gl_capture_try_pbo(opengl.gl_capture, atoi(getenv("GLC_TRY_PBO")));

	gl_capture_set_pack_alignment(opengl.gl_capture, 8);
	if (getenv("GLC_CAPTURE_DWORD_ALIGNED")) {
		if (!atoi(getenv("GLC_CAPTURE_DWORD_ALIGNED")))
			gl_capture_set_pack_alignment(opengl.gl_capture, 1);
	}

	if (getenv("GLC_CROP")) {
		w = h = x = y = 0;

		/* we need at least 2 values, width and height */
		if (sscanf(getenv("GLC_CROP"), "%ux%u+%u+%u",
			   &w, &h, &x, &y) >= 2)
			gl_capture_crop(opengl.gl_capture, x, y, w, h);
	}

	gl_capture_draw_indicator(opengl.gl_capture, 0);
	if (getenv("GLC_INDICATOR"))
		gl_capture_draw_indicator(opengl.gl_capture, atoi(getenv("GLC_INDICATOR")));

	gl_capture_lock_fps(opengl.gl_capture, 0);
	if (getenv("GLC_LOCK_FPS"))
		gl_capture_lock_fps(opengl.gl_capture, atoi(getenv("GLC_LOCK_FPS")));

	get_real_opengl();
	return 0;
}

int opengl_start(ps_buffer_t *buffer)
{
	if (opengl.started)
		return EINVAL;

	opengl.buffer = buffer;

	/* init unscaled buffer if it is needed */
	if ((opengl.scale_factor != 1.0) | opengl.convert_ycbcr_420jpeg) {
		/* if scaling is enabled, it is faster to capture as GL_BGRA */
		gl_capture_set_pixel_format(opengl.gl_capture, GL_BGRA);

		ps_bufferattr_t attr;
		ps_bufferattr_init(&attr);
		ps_bufferattr_setsize(&attr, opengl.unscaled_size);
		opengl.unscaled = (ps_buffer_t *) malloc(sizeof(ps_buffer_t));
		ps_buffer_init(opengl.unscaled, &attr);

		if (opengl.convert_ycbcr_420jpeg) {
			ycbcr_init(&opengl.ycbcr, opengl.glc);
			ycbcr_set_scale(opengl.ycbcr, opengl.scale_factor);
			ycbcr_process_start(opengl.ycbcr, opengl.unscaled, buffer);
		} else {
			scale_init(&opengl.scale, opengl.glc);
			scale_set_scale(opengl.scale, opengl.scale_factor);
			scale_process_start(opengl.scale, opengl.unscaled, buffer);
		}

		gl_capture_set_buffer(opengl.gl_capture, opengl.unscaled);
	} else {
		gl_capture_set_pixel_format(opengl.gl_capture, GL_BGR);
		gl_capture_set_buffer(opengl.gl_capture, opengl.buffer);
	}

	opengl.started = 1;
	return 0;
}

int opengl_close()
{
	int ret;
	if (!opengl.started)
		return 0;

	util_log(opengl.glc, GLC_DEBUG, "opengl", "closing");

	if (opengl.capturing)
		gl_capture_stop(opengl.gl_capture);
	gl_capture_destroy(opengl.gl_capture);

	if (opengl.unscaled) {
		if (lib.running) {
			if ((ret = util_write_end_of_stream(opengl.glc, opengl.unscaled))) {
				util_log(opengl.glc, GLC_ERROR, "opengl",
					 "can't write end of stream: %s (%d)", strerror(ret), ret);
				return ret;
			}
		} else
			ps_buffer_cancel(opengl.unscaled);

		if (opengl.convert_ycbcr_420jpeg) {
			ycbcr_process_wait(opengl.ycbcr);
			ycbcr_destroy(opengl.ycbcr);
		} else {
			scale_process_wait(opengl.scale);
			scale_destroy(opengl.scale);
		}
	} else if (lib.running) {
		if ((ret = util_write_end_of_stream(opengl.glc, opengl.buffer))) {
			util_log(opengl.glc, GLC_ERROR, "opengl",
				 "can't write end of stream: %s (%d)", strerror(ret), ret);
			return ret;
		}
	} else
		ps_buffer_cancel(opengl.buffer);

	if (opengl.unscaled) {
		ps_buffer_destroy(opengl.unscaled);
		free(opengl.unscaled);
	}

	return 0;
}

int opengl_capture_start()
{
	int ret;
	if (opengl.capturing)
		return 0;

	if (!(ret = gl_capture_start(opengl.gl_capture)))
		opengl.capturing = 1;

	return ret;
}

int opengl_capture_stop()
{
	int ret;
	if (!opengl.capturing)
		return 0;

	if (!(ret = gl_capture_stop(opengl.gl_capture)))
		opengl.capturing = 0;

	return ret;
}

int opengl_refresh_color_correction()
{
	return gl_capture_refresh_color_correction(opengl.gl_capture);
}

void get_real_opengl()
{
	if (!lib.dlopen)
		get_real_dlsym();

	opengl.libGL_handle = lib.dlopen("libGL.so.1", RTLD_LAZY);
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
	if (opengl.read_buffer == GL_FRONT)
		opengl.glXSwapBuffers(dpy, drawable);

	gl_capture_frame(opengl.gl_capture, dpy, drawable);

	if (opengl.read_buffer == GL_BACK)
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

	if ((dpy != NULL) && (drawable != None))
		gl_capture_frame(opengl.gl_capture, dpy, drawable);
}


/**  \} */
/**  \} */
