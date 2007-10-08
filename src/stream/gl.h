/**
 * \file src/stream/gl.h
 * \brief OpenGL stuff
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* gl.h -- OpenGL stuff
 * Copyright (C) 2007 Pyry Haulos
 * For conditions of distribution and use, see copyright notice in glc.h
 */

#ifndef _GL_H
#define _GL_H

#include <X11/X.h>
#include <GL/glx.h>
#include <packetstream.h>
#include "../common/glc.h"

/**
 * \addtogroup gl
 *  \{
 */

void *gl_capture_init(glc_t *glc, ps_buffer_t *to);
int gl_capture(void *glpriv, Display *dpy, GLXDrawable drawable);
void gl_capture_close(void *glpriv);

int gl_show_init(glc_t *glc, ps_buffer_t *from);

/**  \} */

#endif
