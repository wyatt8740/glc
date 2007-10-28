/**
 * \file src/lib/alsa.c
 * \brief alsa wrapper
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* alsa.c -- alsa wrapper
 * Copyright (C) 2007 Pyry Haulos
 * For conditions of distribution and use, see copyright notice in glc.h
 */

#include <dlfcn.h>
#include <elfhacks.h>
#include <alsa/asoundlib.h>

#include "../stream/audio_capture.h"
#include "../common/util.h"
#include "lib.h"

/**
 * \addtogroup lib
 *  \{
 */

/**
 * \defgroup alsa alsa wrapper
 *  \{
 */

struct alsa_private_s {
	glc_t *glc;

	void *audio;
	int capture;
	int started;

	void *libasound_handle;
	int (*snd_pcm_open)(snd_pcm_t **, const char *, snd_pcm_stream_t, int);
	snd_pcm_sframes_t (*snd_pcm_writei)(snd_pcm_t *, const void *, snd_pcm_uframes_t);
	snd_pcm_sframes_t (*snd_pcm_writen)(snd_pcm_t *, void **, snd_pcm_uframes_t);
	int (*snd_pcm_mmap_begin)(snd_pcm_t *, const snd_pcm_channel_area_t **, snd_pcm_uframes_t *, snd_pcm_uframes_t *);
	snd_pcm_sframes_t (*snd_pcm_mmap_commit)(snd_pcm_t *, snd_pcm_uframes_t, snd_pcm_uframes_t);
};

__PRIVATE struct alsa_private_s alsa;

__PRIVATE void get_real_alsa();

int alsa_init(glc_t *glc)
{
	alsa.glc = glc;
	alsa.started = 0;

	util_log(alsa.glc, GLC_INFORMATION, "alsa", "initializing");

	if (getenv("GLC_AUDIO"))
		alsa.capture = atoi(getenv("GLC_AUDIO"));
	else
		alsa.capture = 1;

	if (getenv("GLC_AUDIO_SKIP")) {
		if (atoi(getenv("GLC_AUDIO_SKIP")))
			alsa.glc->flags |= GLC_AUDIO_ALLOW_SKIP;
	} else
		alsa.glc->flags |= GLC_AUDIO_ALLOW_SKIP;

	get_real_alsa();
	return 0;
}

int alsa_start(ps_buffer_t *buffer)
{
	if (alsa.started)
		return EINVAL;

	if (alsa.capture) {
		if (!(alsa.audio = audio_capture_init(alsa.glc, buffer)))
			return EAGAIN;
	}

	alsa.started = 1;
	return 0;
}

int alsa_close()
{
	if (!alsa.started)
		return 0;

	util_log(alsa.glc, GLC_INFORMATION, "alsa", "closing");

	if (alsa.capture)
		audio_capture_close(alsa.audio);
	/*if (alsa.libasound_handle)
		dlclose(alsa.libasound_handle);*/
	return 0;
}

void get_real_alsa()
{
	if (!lib.dlopen)
		get_real_dlsym();

	alsa.libasound_handle = lib.dlopen("libasound.so", RTLD_LAZY);
	if (!alsa.libasound_handle)
		goto err;
	alsa.snd_pcm_open =
	  (int (*)(snd_pcm_t **, const char *, snd_pcm_stream_t, int))
	    lib.dlsym(alsa.libasound_handle, "snd_pcm_open");
	if (!alsa.snd_pcm_open)
		goto err;
	alsa.snd_pcm_writei =
	  (snd_pcm_sframes_t (*)(snd_pcm_t *, const void *, snd_pcm_uframes_t))
	    lib.dlsym(alsa.libasound_handle, "snd_pcm_writei");
	if (!alsa.snd_pcm_writei)
		goto err;
	alsa.snd_pcm_writen =
	  (snd_pcm_sframes_t (*)(snd_pcm_t *, void **, snd_pcm_uframes_t))
	    lib.dlsym(alsa.libasound_handle, "snd_pcm_writen");
	if (!alsa.snd_pcm_writen)
		goto err;
	alsa.snd_pcm_mmap_begin =
	  (int (*)(snd_pcm_t *, const snd_pcm_channel_area_t **, snd_pcm_uframes_t *, snd_pcm_uframes_t *))
	    lib.dlsym(alsa.libasound_handle, "snd_pcm_mmap_begin");
	if (!alsa.snd_pcm_mmap_begin)
		goto err;
	alsa.snd_pcm_mmap_commit =
	  (snd_pcm_sframes_t (*)(snd_pcm_t *, snd_pcm_uframes_t, snd_pcm_uframes_t))
	    lib.dlsym(alsa.libasound_handle, "snd_pcm_mmap_commit");
	if (alsa.snd_pcm_mmap_commit)
		return;
err:
	fprintf(stderr, "(glc:alsa) can't get real alsa");
	exit(1);
}

int alsa_unhook_so(const char *soname)
{
	/* TODO cache applied unhooks => reduce fs load */
	int ret;
	eh_obj_t so;

	if ((ret = eh_init_obj(&so, soname)))
		return ret;

	/* don't look at 'elfhacks'... contains some serious black magic */
	/* TODO should we apply to snd_pcm_open() as well? */
	eh_set_rel(&so, "snd_pcm_writei", alsa.snd_pcm_writei);
	eh_set_rel(&so, "snd_pcm_writen", alsa.snd_pcm_writen);
	eh_set_rel(&so, "snd_pcm_mmap_begin", alsa.snd_pcm_mmap_begin);
	eh_set_rel(&so, "snd_pcm_mmap_commit", alsa.snd_pcm_mmap_commit);
	eh_set_rel(&so, "dlsym", lib.dlsym);
	eh_set_rel(&so, "dlvsym", lib.dlvsym);

	eh_destroy_obj(&so);

	return 0;
}

int snd_pcm_open(snd_pcm_t **pcmp, const char *name, snd_pcm_stream_t stream, int mode)
{
	/* it is not necessarily safe to call glc_init() from write funcs
	   especially async mode (initiated from signal) is troublesome */
	INIT_GLC
	return alsa.snd_pcm_open(pcmp, name, stream, mode);
}

snd_pcm_sframes_t snd_pcm_writei(snd_pcm_t *pcm, const void *buffer, snd_pcm_uframes_t size)
{
	INIT_GLC
	snd_pcm_sframes_t ret = alsa.snd_pcm_writei(pcm, buffer, size);
	if ((alsa.capture) && (ret > 0) && (alsa.glc->flags & GLC_CAPTURE))
		audio_capture_alsa_i(alsa.audio, pcm, buffer, ret);
	return ret;
}

snd_pcm_sframes_t snd_pcm_writen(snd_pcm_t *pcm, void **bufs, snd_pcm_uframes_t size)
{
	INIT_GLC
	snd_pcm_sframes_t ret = alsa.snd_pcm_writen(pcm, bufs, size);
	if ((alsa.capture) && (ret > 0) && (alsa.glc->flags & GLC_CAPTURE))
		audio_capture_alsa_n(alsa.audio, pcm, bufs, ret);
	return ret;
}

int snd_pcm_mmap_begin(snd_pcm_t *pcm, const snd_pcm_channel_area_t **areas, snd_pcm_uframes_t *offset, snd_pcm_uframes_t *frames)
{
	INIT_GLC
	int ret = alsa.snd_pcm_mmap_begin(pcm, areas, offset, frames);
	if ((alsa.capture) && (ret >= 0) && (alsa.glc->flags & GLC_CAPTURE))
		audio_capture_alsa_mmap_begin(alsa.audio, pcm, *areas);
	return ret;
}

snd_pcm_sframes_t snd_pcm_mmap_commit(snd_pcm_t *pcm, snd_pcm_uframes_t offset, snd_pcm_uframes_t frames)
{
	INIT_GLC
	if (alsa.capture && (alsa.glc->flags & GLC_CAPTURE))
		audio_capture_alsa_mmap_commit(alsa.audio, pcm, offset,  frames);
	return alsa.snd_pcm_mmap_commit(pcm, offset, frames);
}

/**  \} */
/**  \} */
