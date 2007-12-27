/**
 * \file src/hook/main.c
 * \brief main wrapper library
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup hook
 *  \{
 * \defgroup main main wrapper library
 *  \{
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
#include "../core/pack.h"
#include "../core/file.h"
#include "lib.h"

struct main_private_s {
	glc_t glc;

	ps_buffer_t *uncompressed;
	ps_buffer_t *compressed;
	size_t uncompressed_size, compressed_size;

	void *file, *pack;

	int sighandler;
	int compress;
	void (*sigint_handler)(int);
	void (*sighup_handler)(int);
	void (*sigterm_handler)(int);
};

__PRIVATE glc_lib_t lib = {NULL, /* dlopen */
			   NULL, /* dlsym */
			   NULL, /* dlvsym */
			   NULL, /* __libc_dlsym */
			   0, /* initialized */
			   0, /* running */
			   PTHREAD_MUTEX_INITIALIZER, /* init_lock */
			   NULL, /* gl */
			   };
__PRIVATE struct main_private_s mpriv;

__PRIVATE int init_buffers();
__PRIVATE void lib_close();
__PRIVATE int load_environ();
__PRIVATE void signal_handler(int signum);
__PRIVATE void get_real_libc_dlsym();

void init_glc()
{
	struct sigaction new_sighandler, old_sighandler;
	int ret;

	if ((ret = pthread_mutex_lock(&lib.init_lock)))
		goto err;

	if (lib.initialized)
		return;

	load_environ();
	util_init(&mpriv.glc);

	if (mpriv.glc.flags & GLC_LOG) {
		if (util_log_init(&mpriv.glc))
			mpriv.glc.flags &= ~GLC_LOG;
		util_log_version(&mpriv.glc);
	}


	if ((ret = init_buffers()))
		goto err;

	util_create_info(&mpriv.glc);

	if ((ret = opengl_init(&mpriv.glc)))
		goto err;
	if ((ret = alsa_init(&mpriv.glc)))
		goto err;
	if ((ret = x11_init(&mpriv.glc)))
		goto err;

	util_init_info(&mpriv.glc); /* init stream info */

	lib.initialized = 1; /* we've technically done */

	if (mpriv.glc.flags & GLC_CAPTURE) {
		if ((ret = start_glc()))
			goto err;
	}

	atexit(lib_close);

	/** \todo hook sigaction() ? */
	if (mpriv.sighandler) {
		util_log(&mpriv.glc, GLC_INFORMATION, "main",
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

	util_log_info(&mpriv.glc);
	util_log(&mpriv.glc, GLC_INFORMATION, "main", "glc initialized");
	util_log(&mpriv.glc, GLC_DEBUG, "main", "LD_PRELOAD=%s", getenv("LD_PRELOAD"));
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

	util_log(&mpriv.glc, GLC_INFORMATION, "main", "starting glc");

	if (lib.running)
		return EINVAL;

	if (!lib.initialized)
		return EAGAIN;

	if (mpriv.compress) {
		/* file needs stream info */
		if ((mpriv.file = file_init(&mpriv.glc, mpriv.compressed)) == NULL)
			return EAGAIN;
		if ((mpriv.pack = pack_init(&mpriv.glc, mpriv.uncompressed, mpriv.compressed)) == NULL)
			return EAGAIN;
	} else if ((mpriv.file = file_init(&mpriv.glc, mpriv.uncompressed)) == NULL)
		return EAGAIN;

	if ((ret = alsa_start(mpriv.uncompressed)))
		return ret;
	if ((ret = opengl_start(mpriv.uncompressed)))
		return ret;

	lib.running = 1;
	util_log(&mpriv.glc, GLC_INFORMATION, "main", "glc running");

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
	mpriv.glc.flags &= ~GLC_CAPTURE; /* disable capturing */
	/*
	 There is a small possibility that a capture operation in another
	 thread is still active. This should be called only in exit() or
	 at return from main loop so we choose performance and not safety.

	 Adding a rwlock for all capture operations might inflict a noticeable
	 cost, at least in complexity.
	*/

	util_log(&mpriv.glc, GLC_INFORMATION, "main", "closing glc");

	if ((ret = alsa_close()))
		goto err;
	if ((ret = opengl_close()))
		goto err;

	if (lib.running) {
		if (mpriv.compress)
			pack_wait(mpriv.pack);
		file_wait(mpriv.file);
	}

	if (mpriv.compressed) {
		ps_buffer_destroy(mpriv.compressed);
		free(mpriv.compressed);
	}

	ps_buffer_destroy(mpriv.uncompressed);
	free(mpriv.uncompressed);

	if (mpriv.glc.flags & GLC_LOG)
		util_log_close(&mpriv.glc);

	util_free_info(&mpriv.glc);
	util_free(&mpriv.glc);

	free(mpriv.glc.stream_file);
	free(mpriv.glc.log_file);
	return;
err:
	fprintf(stderr, "glc: cleanup and finish failed\n%s (%d)\n", strerror(ret), ret);
	return;
}

int load_environ()
{
	mpriv.glc.flags = 0;

	if (getenv("GLC_START")) {
		if (atoi(getenv("GLC_START")))
			mpriv.glc.flags |= GLC_CAPTURE;
	}

	mpriv.glc.stream_file = malloc(1024);
	if (getenv("GLC_FILE"))
		snprintf(mpriv.glc.stream_file, 1023, getenv("GLC_FILE"), getpid());
	else
		snprintf(mpriv.glc.stream_file, 1023, "pid-%d.glc", getpid());

	mpriv.glc.log_file = malloc(1024);
	if (getenv("GLC_LOG_FILE"))
		snprintf(mpriv.glc.log_file, 1023, getenv("GLC_LOG_FILE"), getpid());
	else
		snprintf(mpriv.glc.log_file, 1023, "/dev/stderr");

	if (getenv("GLC_LOG")) {
		mpriv.glc.log_level = atoi(getenv("GLC_LOG"));
		if (mpriv.glc.log_level >= 0)
			mpriv.glc.flags |= GLC_LOG;
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
			mpriv.glc.flags |= GLC_COMPRESS_LZO;
		} else if (!strcmp(getenv("GLC_COMPRESS"), "quicklz")) {
			mpriv.compress = 1;
			mpriv.glc.flags |= GLC_COMPRESS_QUICKLZ;
		}
	} else {
#ifdef __QUICKLZ
		mpriv.compress = 1;
		mpriv.glc.flags |= GLC_COMPRESS_QUICKLZ;
#else
# ifdef __LZO
		mpriv.compress = 1;
		mpriv.glc.flags |= GLC_COMPRESS_LZO;
# endif
#endif
	}

	return 0;
}

void get_real_dlsym()
{
	eh_obj_t libdl;

	if (eh_find_obj(&libdl, "*libdl.so*")) {
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

void get_real___libc_dlsym()
{
	eh_obj_t libc;

	if (eh_find_obj(&libc, "*libc.so*")) {
		fprintf(stderr, "(glc) libc.so is not present in memory\n");
		exit(1);
	}

	if (eh_find_sym(&libc, "__libc_dlsym", (void *) &lib.__libc_dlsym)) {
		fprintf(stderr, "(glc) can't get real __libc_dlsym()\n");
		exit(1);
	}

	eh_destroy_obj(&libc);
}

void *wrapped_func(const char *symbol)
{
	if (!strcmp(symbol, "glXGetProcAddressARB"))
		return &__opengl_glXGetProcAddressARB;
	else if (!strcmp(symbol, "glXSwapBuffers"))
		return &__opengl_glXSwapBuffers;
	else if (!strcmp(symbol, "glFinish"))
		return &__opengl_glFinish;
	else if (!strcmp(symbol, "snd_pcm_open"))
		return &__alsa_snd_pcm_open;
	else if (!strcmp(symbol, "snd_pcm_close"))
		return &__alsa_snd_pcm_close;
	else if (!strcmp(symbol, "snd_pcm_open_lconf"))
		return &__alsa_snd_pcm_open_lconf;
	else if (!strcmp(symbol, "snd_pcm_hw_params"))
		return &__alsa_snd_pcm_hw_params;
	else if (!strcmp(symbol, "snd_pcm_writei"))
		return &__alsa_snd_pcm_writei;
	else if (!strcmp(symbol, "snd_pcm_writen"))
		return &__alsa_snd_pcm_writen;
	else if (!strcmp(symbol, "snd_pcm_mmap_writei"))
		return &__alsa_snd_pcm_mmap_writei;
	else if (!strcmp(symbol, "snd_pcm_mmap_writen"))
		return &__alsa_snd_pcm_mmap_writen;
	else if (!strcmp(symbol, "snd_pcm_mmap_begin"))
		return &__alsa_snd_pcm_mmap_begin;
	else if (!strcmp(symbol, "snd_pcm_mmap_commit"))
		return &__alsa_snd_pcm_mmap_commit;
	else if (!strcmp(symbol, "XNextEvent"))
		return &__x11_XNextEvent;
	else if (!strcmp(symbol, "XPeekEvent"))
		return &__x11_XPeekEvent;
	else if (!strcmp(symbol, "XWindowEvent"))
		return &__x11_XWindowEvent;
	else if (!strcmp(symbol, "XMaskEvent"))
		return &__x11_XMaskEvent;
	else if (!strcmp(symbol, "XCheckWindowEvent"))
		return &__x11_XCheckWindowEvent;
	else if (!strcmp(symbol, "XCheckMaskEvent"))
		return &__x11_XCheckMaskEvent;
	else if (!strcmp(symbol, "XCheckTypedEvent"))
		return &__x11_XCheckTypedEvent;
	else if (!strcmp(symbol, "XCheckTypedWindowEvent"))
		return &__x11_XCheckTypedWindowEvent;
	else if (!strcmp(symbol, "XIfEvent"))
		return &__x11_XIfEvent;
	else if (!strcmp(symbol, "XCheckIfEvent"))
		return &__x11_XCheckIfEvent;
	else if (!strcmp(symbol, "XPeekIfEvent"))
		return &__x11_XPeekIfEvent;
	else if (!strcmp(symbol, "XF86VidModeSetGamma"))
		return &__x11_XF86VidModeSetGamma;
	else if (!strcmp(symbol, "dlopen"))
		return &__main_dlopen;
	else if (!strcmp(symbol, "dlsym"))
		return &__main_dlsym;
	else if (!strcmp(symbol, "dlvsym"))
		return &__main_dlvsym;
	else if (!strcmp(symbol, "__libc_dlsym"))
		return &__main___libc_dlsym;
	else
		return NULL;
}

__PUBLIC void *dlopen(const char *filename, int flag)
{
	return __main_dlopen(filename, flag);
}

void *__main_dlopen(const char *filename, int flag)
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

__PUBLIC void *dlsym(void *handle, const char *symbol)
{
	return __main_dlsym(handle, symbol);
}

void *__main_dlsym(void *handle, const char *symbol)
{
	if (lib.dlsym == NULL)
		get_real_dlsym();

	void *ret = wrapped_func(symbol);
	if (ret)
		return ret;

	return lib.dlsym(handle, symbol);
}

__PUBLIC void *dlvsym(void *handle, const char *symbol, const char *version)
{
	return __main_dlvsym(handle, symbol, version);
}

void *__main_dlvsym(void *handle, const char *symbol, const char *version)
{
	if (lib.dlvsym == NULL)
		get_real_dlsym();

	void *ret = wrapped_func(symbol); /* should we too check for version? */
	if (ret)
		return ret;

	return lib.dlvsym(handle, symbol, version);
}

__PUBLIC void *__libc_dlsym(void *handle, const char *symbol)
{
	return __main___libc_dlsym(handle, symbol);
}

void *__main___libc_dlsym(void *handle, const char *symbol)
{
	if (lib.__libc_dlsym == NULL)
		get_real___libc_dlsym();

	void *ret = wrapped_func(symbol);
	if (ret)
		return ret;

	return lib.__libc_dlsym(handle, symbol);
}

/**  \} */
/**  \} */
