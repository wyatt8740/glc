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
#include <pthread.h>

/**
 * \addtogroup lib
 *  \{
 */

typedef struct {
	void *(*dlopen)(const char *filename, int flag);
	void *(*dlsym)(void *, const char *);
	void *(*dlvsym)(void *, const char *, const char *);
	int initialized;
	int running;
	pthread_mutex_t init_lock;
} glc_lib_t;

#define __PRIVATE __attribute__ ((visibility ("hidden")))

#define INIT_GLC \
	if (!lib.initialized) \
		init_glc();

/**
 * \addtogroup main
 *  \{
 */
__PRIVATE extern glc_lib_t lib;
__PRIVATE void init_glc();
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

/**
 * \defgroup hooks Hooked functions
 *  \{
 */
typedef void (*GLXextFuncPtr)(void);
GLXextFuncPtr glXGetProcAddressARB(const GLubyte *proc_name);
void glXSwapBuffers(Display *dpy, GLXDrawable drawable);
void glFinish(void);
void glXSwapBuffers(Display *dpy, GLXDrawable drawable);

int XNextEvent(Display *display, XEvent *event_return);
int XPeekEvent(Display *display, XEvent *event_return);
int XWindowEvent(Display *display, Window w, long event_mask, XEvent *event_return);
Bool XCheckWindowEvent(Display *display, Window w, long event_mask, XEvent *event_return);
int XMaskEvent(Display *display, long event_mask, XEvent *event_return);
Bool XCheckMaskEvent(Display *display, long event_mask, XEvent *event_return);
Bool XCheckTypedEvent(Display *display, int event_type, XEvent *event_return);
Bool XCheckTypedWindowEvent(Display *display, Window w, int event_type, XEvent *event_return);
int XIfEvent(Display *display, XEvent *event_return, Bool ( *predicate)(), XPointer arg);
Bool XCheckIfEvent(Display *display, XEvent *event_return, Bool ( *predicate)(), XPointer arg);
int XPeekIfEvent(Display *display, XEvent *event_return, Bool ( *predicate)(), XPointer arg);

int snd_pcm_open(snd_pcm_t **pcmp, const char *name, snd_pcm_stream_t stream, int mode);
snd_pcm_sframes_t snd_pcm_writei(snd_pcm_t *pcm, const void *buffer, snd_pcm_uframes_t size);
snd_pcm_sframes_t snd_pcm_writen(snd_pcm_t *pcm, void **bufs, snd_pcm_uframes_t size);
int snd_pcm_mmap_begin(snd_pcm_t *pcm, const snd_pcm_channel_area_t **areas, snd_pcm_uframes_t *offset, snd_pcm_uframes_t *frames);
snd_pcm_sframes_t snd_pcm_mmap_commit(snd_pcm_t *pcm, snd_pcm_uframes_t offset, snd_pcm_uframes_t frames);
/**  \} */

#endif


/**  \} */
