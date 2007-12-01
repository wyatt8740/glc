/**
 * \file src/capture/gl_capture.h
 * \brief OpenGL capture
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup gl_capture
 *  \{
 */

#ifndef _GL_CAPTURE_H
#define _GL_CAPTURE_H

#include <X11/X.h>
#include <GL/glx.h>
#include <packetstream.h>
#include "../common/glc.h"

__PUBLIC void *gl_capture_init(glc_t *glc, ps_buffer_t *to);
__PUBLIC void gl_capture_close(void *glpriv);

__PUBLIC int gl_capture_frame(void *glpriv, Display *dpy, GLXDrawable drawable);
__PUBLIC int gl_capture(void *glpriv, Display *dpy, GLXDrawable drawable);
__PUBLIC int gl_capture_refresh_color(void *glpriv);

/**  \} */

#endif
