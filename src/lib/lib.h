/**
 * \file src/lib/lib.h
 * \brief wrapper library
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* lib.h -- wrapper library
 * Copyright (C) 2007 Pyry Haulos
 * For conditions of distribution and use, see copyright notice in glc.h
 */

#ifndef _LIB_H
#define _LIB_H

#include "../common/glc.h"
#include <X11/X.h>
#include <X11/Xlib.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <alsa/asoundlib.h>
#include <packetstream.h>

/**
 * \addtogroup lib
 *  \{
 */

typedef struct {
	void *(*dlopen)(const char *filename, int flag);
	void *(*dlsym)(void *, const char *);
	void *(*dlvsym)(void *, const char *, const char *);
	int running;
} glc_lib_t;

#define __PRIVATE __attribute__ ((visibility ("hidden")))

/**
 * \addtogroup main
 *  \{
 */
__PRIVATE extern glc_lib_t lib;
__PRIVATE int start_glc();
__PRIVATE void get_real_dlsym();
__PRIVATE void *wrapped_func(const char *symbol);
/**  \} */

/**
 * \addtogroup alsa
 *  \{
 */
__PRIVATE int alsa_init(glc_t *glc);
__PRIVATE int alsa_start(ps_buffer_t *buffer);
__PRIVATE int alsa_close();
__PRIVATE int alsa_unhook_so(const char *soname);
/**  \} */

/**
 * \addtogroup opengl
 *  \{
 */
__PRIVATE int opengl_init(glc_t *glc);
__PRIVATE int opengl_start(ps_buffer_t *buffer);
__PRIVATE int opengl_close();
/**  \} */

/**
 * \addtogroup x11
 *  \{
 */
__PRIVATE int x11_init(glc_t *glc);
__PRIVATE int x11_close();
/**  \} */

#endif


/**  \} */
