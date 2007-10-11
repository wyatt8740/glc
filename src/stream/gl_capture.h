/**
 * \file src/stream/gl_capture.h
 * \brief OpenGL capture
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* gl_capture.h -- OpenGL stuff
 * Copyright (C) 2007 Pyry Haulos
 * For conditions of distribution and use, see copyright notice in glc.h
 */

#ifndef _GL_CAPTURE_H
#define _GL_CAPTURE_H

#include <X11/X.h>
#include <GL/glx.h>
#include <packetstream.h>
#include "../common/glc.h"

/**
 * \addtogroup gl_capture
 *  \{
 */

void *gl_capture_init(glc_t *glc, ps_buffer_t *to);
int gl_capture(void *glpriv, Display *dpy, GLXDrawable drawable);
void gl_capture_close(void *glpriv);

/**  \} */

#endif
