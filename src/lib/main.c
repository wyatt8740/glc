/**
 * \file src/lib/main.c
 * \brief main wrapper library
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* main.c -- main wrapper library
 * Copyright (C) 2007 Pyry Haulos
 * For conditions of distribution and use, see copyright notice in glc.h
 */

#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>
#include <elfhacks.h>
#include <unistd.h>
#include <signal.h>
#include <fnmatch.h>
#include <sched.h>
#include <pthread.h>

#include "../common/glc.h"
#include "../common/util.h"
#include "../stream/pack.h"
#include "../stream/file.h"
#include "lib.h"

/**
 * \addtogroup lib
 *  \{
 */

/**
 * \defgroup main main wrapper library
 *  \{
 */

struct main_private_s {
	glc_t *glc;

	void *gl;

	ps_buffer_t *uncompressed;
	ps_buffer_t *compressed;
	size_t uncompressed_size, compressed_size;

	int sighandler;
	int compress;
	void (*sigint_handler)(int);
	void (*sighup_handler)(int);
	void (*sigterm_handler)(int);
};

__PRIVATE glc_lib_t lib = {NULL, /* dlopen */
			   NULL, /* dlsym */
			   NULL, /* dlvsym */
			   0, /* initialized */
			   0, /* running */
			   PTHREAD_MUTEX_INITIALIZER, /* init_lock */
			   };
__PRIVATE struct main_private_s mpriv;

__PRIVATE int init_buffers();
__PRIVATE void lib_close();
__PRIVATE int load_environ();
__PRIVATE void signal_handler(int signum);

void init_glc()
{
	struct sigaction new_sighandler, old_sighandler;
	int ret;

	if ((ret = pthread_mutex_lock(&lib.init_lock)))
		goto err;

	if (lib.initialized)
		return;

	glc_create(&mpriv.glc);
	load_environ();
	util_init(mpriv.glc);

	if (mpriv.glc->flags & GLC_LOG) {
		if (util_log_init(mpriv.glc))
			mpriv.glc->flags &= ~GLC_LOG;
	}

	if ((ret = init_buffers()))
		goto err;

	util_create_info(mpriv.glc);

	if ((ret = opengl_init(mpriv.glc)))
		goto err;
	if ((ret = alsa_init(mpriv.glc)))
		goto err;
	if ((ret = x11_init(mpriv.glc)))
		goto err;

	util_init_info(mpriv.glc); /* init stream info */

	lib.initialized = 1; /* we've technically done */

	if (mpriv.glc->flags & GLC_CAPTURE) {
		if ((ret = start_glc()))
			goto err;
	}

	atexit(lib_close);

	/* TODO hook sigaction() ? */
	if (mpriv.sighandler) {
		util_log(mpriv.glc, GLC_INFORMATION, "main",
			 "setting signal handler");

		new_sighandler.sa_handler = signal_handler;
		sigemptyset(&new_sighandler.sa_mask);
		new_sighandler.sa_flags = 0;

		sigaction(SIGINT, &new_sighandler, &old_sighandler);
		mpriv.sigint_handler = old_sighandler.sa_handler;

		sigaction(SIGHUP, &new_sighandler, &old_sighandler);
		mpriv.sighup_handler = old_sighandler.sa_handler;

		sigaction(SIGTERM, &new_sighandler, &old_sighandler);
		mpriv.sigterm_handler = old_sighandler.sa_handler;
	}

	if ((ret = pthread_mutex_unlock(&lib.init_lock)))
		goto err;

	util_log_info(mpriv.glc);
	util_log(mpriv.glc, GLC_INFORMATION, "main", "glc initialized");
	return;
err:
	fprintf(stderr, "glc: %s (%d)\n", strerror(ret), ret);
	exit(ret); /* glc initialization is critical */
}

int init_buffers()
{
	int ret;
	ps_bufferattr_t attr;
	ps_bufferattr_init(&attr);

	ps_bufferattr_setsize(&attr, mpriv.uncompressed_size);
	mpriv.uncompressed = (ps_buffer_t *) malloc(sizeof(ps_buffer_t));
	if ((ret = ps_buffer_init(mpriv.uncompressed, &attr)))
		return ret;

	if (mpriv.compress) {
		ps_bufferattr_setsize(&attr, mpriv.compressed_size);
		mpriv.compressed = (ps_buffer_t *) malloc(sizeof(ps_buffer_t));
		if ((ret = ps_buffer_init(mpriv.compressed, &attr)))
			return ret;
	}

	ps_bufferattr_destroy(&attr);
	return 0;
}

int start_glc()
{
	int ret;

	util_log(mpriv.glc, GLC_INFORMATION, "main", "starting glc");

	if (lib.running)
		return EINVAL;

	if (!lib.initialized)
		return EAGAIN;

	if (mpriv.compress) {
		if ((ret = file_init(mpriv.glc, mpriv.compressed))) /* file needs stream info */
			return ret;
		if ((ret = pack_init(mpriv.glc, mpriv.uncompressed, mpriv.compressed)))
			return ret;
	} else if ((ret = file_init(mpriv.glc, mpriv.uncompressed)))
		return ret;

	if ((ret = alsa_start(mpriv.uncompressed)))
		return ret;
	if ((ret = opengl_start(mpriv.uncompressed)))
		return ret;

	lib.running = 1;
	util_log(mpriv.glc, GLC_INFORMATION, "main", "glc running");

	return 0;
}

void signal_handler(int signum)
{
	if ((signum == SIGINT) &&
	    (mpriv.sigint_handler == SIG_IGN))
		return;
	else if ((signum == SIGHUP) &&
	         (mpriv.sighup_handler == SIG_IGN))
		return;
	else if ((signum == SIGTERM) &&
	         (mpriv.sigterm_handler == SIG_IGN))
		return;

	if ((signum == SIGINT) &&
	    (mpriv.sigint_handler != SIG_DFL) &&
	    (mpriv.sigint_handler != NULL))
		mpriv.sigint_handler(signum);
	else if ((signum == SIGHUP) &&
	         (mpriv.sighup_handler != SIG_DFL) &&
	         (mpriv.sighup_handler != NULL))
		mpriv.sighup_handler(signum);
	else if ((signum == SIGTERM) &&
	         (mpriv.sigterm_handler != SIG_DFL) &&
	         (mpriv.sigterm_handler != NULL))
		mpriv.sigterm_handler(signum);

	fprintf(stderr, "(glc:main) got C-c, will now exit...");
	exit(0); /* may cause lots of damage... */
}

void lib_close()
{
	int ret;
	mpriv.glc->flags &= ~GLC_CAPTURE; /* disable capturing */

	util_log(mpriv.glc, GLC_INFORMATION, "main", "closing glc");

	if ((ret = alsa_close()))
		goto err;
	if ((ret = opengl_close()))
		goto err;

	if (lib.running) {
		if (mpriv.compress)
			sem_wait(&mpriv.glc->signal[GLC_SIGNAL_PACK_FINISHED]);
		sem_wait(&mpriv.glc->signal[GLC_SIGNAL_FILE_FINISHED]);
	}

	if (mpriv.compressed) {
		ps_buffer_destroy(mpriv.compressed);
		free(mpriv.compressed);
	}

	ps_buffer_destroy(mpriv.uncompressed);
	free(mpriv.uncompressed);

	if (mpriv.glc->flags & GLC_LOG)
		util_log_close(mpriv.glc);

	util_free_info(mpriv.glc);
	util_free(mpriv.glc);

	free(mpriv.glc->stream_file);
	free(mpriv.glc->log_file);
	glc_destroy(mpriv.glc);
	return;
err:
	fprintf(stderr, "glc: cleanup and finish failed\n%s (%d)\n", strerror(ret), ret);
	return;
}

int load_environ()
{
	mpriv.glc->flags = 0;

	if (getenv("GLC_START")) {
		if (atoi(getenv("GLC_START")))
			mpriv.glc->flags |= GLC_CAPTURE;
	}

	mpriv.glc->stream_file = malloc(1024);
	if (getenv("GLC_FILE"))
		snprintf(mpriv.glc->stream_file, 1023, getenv("GLC_FILE"), getpid());
	else
		snprintf(mpriv.glc->stream_file, 1023, "pid-%d.glc", getpid());

	mpriv.glc->log_file = malloc(1024);
	if (getenv("GLC_LOG_FILE"))
		snprintf(mpriv.glc->log_file, 1023, getenv("GLC_LOG_FILE"), getpid());
	else
		snprintf(mpriv.glc->log_file, 1023, "/dev/stderr");

	if (getenv("GLC_LOG")) {
		mpriv.glc->log_level = atoi(getenv("GLC_LOG"));
		if (mpriv.glc->log_level >= 0)
			mpriv.glc->flags |= GLC_LOG;
	}

	if (getenv("GLC_SIGHANDLER"))
		mpriv.sighandler = atoi(getenv("GLC_SIGHANDLER"));
	else
		mpriv.sighandler = 0;

	if (getenv("GLC_UNCOMPRESSED_BUFFER_SIZE"))
		mpriv.uncompressed_size = atoi(getenv("GLC_UNCOMPRESSED_BUFFER_SIZE")) * 1024 * 1024;
	else
		mpriv.uncompressed_size = 1024 * 1024 * 25;

	if (getenv("GLC_COMPRESSED_BUFFER_SIZE"))
		mpriv.compressed_size = atoi(getenv("GLC_COMPRESSED_BUFFER_SIZE")) * 1024 * 1024;
	else
		mpriv.compressed_size = 1024 * 1024 * 50;

	if (getenv("GLC_COMPRESS")) {
		if (!strcmp(getenv("GLC_COMPRESS"), "lzo")) {
			mpriv.compress = 1;
			mpriv.glc->flags |= GLC_COMPRESS_LZO;
		} else if (!strcmp(getenv("GLC_COMPRESS"), "quicklz")) {
			mpriv.compress = 1;
			mpriv.glc->flags |= GLC_COMPRESS_QUICKLZ;
		}
	} else {
#ifdef __QUICKLZ
		mpriv.compress = 1;
		mpriv.glc->flags |= GLC_COMPRESS_QUICKLZ;
#else
# ifdef __LZO
		mpriv.compress = 1;
		mpriv.glc->flags |= GLC_COMPRESS_LZO;
# endif
#endif
	}

	return 0;
}

void get_real_dlsym()
{
	eh_obj_t libdl;

	if (eh_init_obj(&libdl, "*libdl.so*")) {
		fprintf(stderr, "(glc) libdl.so is not present in memory\n");
		exit(1);
	}

	if (eh_find_sym(&libdl, "dlopen", (void *) &lib.dlopen)) {
		fprintf(stderr, "(glc) can't get real dlopen()\n");
		exit(1);
	}

	if (eh_find_sym(&libdl, "dlsym", (void *) &lib.dlsym)) {
		fprintf(stderr, "(glc) can't get real dlsym()\n");
		exit(1);
	}

	if (eh_find_sym(&libdl, "dlvsym", (void *) &lib.dlvsym)) {
		fprintf(stderr, "(glc) can't get real dlvsym()\n");
		exit(1);
	}

	eh_destroy_obj(&libdl);
}

void *wrapped_func(const char *symbol)
{
	/* prog shouldn't dlsym() dlopen or dlsym :P */
	if (!strcmp(symbol, "glXGetProcAddressARB"))
		return &glXGetProcAddressARB;
	else if (!strcmp(symbol, "glXSwapBuffers"))
		return &glXSwapBuffers;
	else if (!strcmp(symbol, "glFinish"))
		return &glFinish;
	else if (!strcmp(symbol, "snd_pcm_open"))
		return &snd_pcm_open;
	else if (!strcmp(symbol, "snd_pcm_writei"))
		return &snd_pcm_writei;
	else if (!strcmp(symbol, "snd_pcm_writen"))
		return &snd_pcm_writen;
	else if (!strcmp(symbol, "snd_pcm_mmap_begin"))
		return &snd_pcm_mmap_begin;
	else if (!strcmp(symbol, "snd_pcm_mmap_commit"))
		return &snd_pcm_mmap_commit;
	else if (!strcmp(symbol, "XNextEvent"))
		return &XNextEvent;
	else if (!strcmp(symbol, "XPeekEvent"))
		return &XPeekEvent;
	else if (!strcmp(symbol, "XWindowEvent"))
		return &XWindowEvent;
	else if (!strcmp(symbol, "XMaskEvent"))
		return &XMaskEvent;
	else if (!strcmp(symbol, "XCheckWindowEvent"))
		return &XCheckWindowEvent;
	else if (!strcmp(symbol, "XCheckMaskEvent"))
		return &XCheckMaskEvent;
	else if (!strcmp(symbol, "XCheckTypedEvent"))
		return &XCheckTypedEvent;
	else if (!strcmp(symbol, "XCheckTypedWindowEvent"))
		return &XCheckTypedWindowEvent;
	else if (!strcmp(symbol, "XIfEvent"))
		return &XIfEvent;
	else if (!strcmp(symbol, "XCheckIfEvent"))
		return &XCheckIfEvent;
	else if (!strcmp(symbol, "XPeekIfEvent"))
		return &XPeekIfEvent;
	else
		return NULL;
}

void *dlopen(const char *filename, int flag)
{
	if (lib.dlopen == NULL)
		get_real_dlsym();

	void *ret = lib.dlopen(filename, flag);

	if ((ret != NULL) && (filename != NULL)) {
		if ((!fnmatch("*libasound.so*", filename, 0)) | (!fnmatch("*libasound_module_*.so*", filename, 0)))
			alsa_unhook_so(filename); /* no audio stream duplication, thanks */
	}

	return ret;
}

void *dlsym(void *handle, const char *symbol)
{
	if (lib.dlsym == NULL)
		get_real_dlsym();

	void *ret = wrapped_func(symbol);
	if (ret)
		return ret;

	return lib.dlsym(handle, symbol);
}

void *dlvsym(void *handle, const char *symbol, const char *version)
{
	if (lib.dlvsym == NULL)
		get_real_dlsym();

	void *ret = wrapped_func(symbol); /* should we too check for version? */
	if (ret)
		return ret;

	return lib.dlvsym(handle, symbol, version);
}

/**  \} */
/**  \} */
