/**
 * \file hook/alsa.c
 * \brief alsa wrapper
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup hook
 *  \{
 * \defgroup alsa alsa wrapper
 *  \{
 */

#include <dlfcn.h>
#include <elfhacks.h>
#include <alsa/asoundlib.h>

#include <glc/common/util.h>
#include <glc/common/core.h>
#include <glc/common/log.h>
#include <glc/capture/audio_hook.h>
#include <glc/capture/audio_capture.h>

#include "lib.h"

struct alsa_capture_stream_s {
	audio_capture_t capture;
	char *device;
	unsigned int channels;
	unsigned int rate;

	struct alsa_capture_stream_s *next;
};

struct alsa_private_s {
	glc_t *glc;
	audio_hook_t audio_hook;
	
	int started;
	int capture;
	int capturing;

	struct alsa_capture_stream_s *capture_stream;

	void *libasound_handle;
	int (*snd_pcm_open)(snd_pcm_t **, const char *, snd_pcm_stream_t, int);
	int (*snd_pcm_open_lconf)(snd_pcm_t **, const char *, snd_pcm_stream_t, int, snd_config_t *);
	int (*snd_pcm_close)(snd_pcm_t *);
	int (*snd_pcm_hw_params)(snd_pcm_t *, snd_pcm_hw_params_t *);
	snd_pcm_sframes_t (*snd_pcm_writei)(snd_pcm_t *, const void *, snd_pcm_uframes_t);
	snd_pcm_sframes_t (*snd_pcm_writen)(snd_pcm_t *, void **, snd_pcm_uframes_t);
	snd_pcm_sframes_t (*snd_pcm_mmap_writei)(snd_pcm_t *, const void *, snd_pcm_uframes_t);
	snd_pcm_sframes_t (*snd_pcm_mmap_writen)(snd_pcm_t *, void **, snd_pcm_uframes_t);
	int (*snd_pcm_mmap_begin)(snd_pcm_t *, const snd_pcm_channel_area_t **,
				  snd_pcm_uframes_t *, snd_pcm_uframes_t *);
	snd_pcm_sframes_t (*snd_pcm_mmap_commit)(snd_pcm_t *, snd_pcm_uframes_t, snd_pcm_uframes_t);
};

__PRIVATE struct alsa_private_s alsa;
__PRIVATE int alsa_loaded = 0;

__PRIVATE void get_real_alsa();

__PRIVATE int alsa_parse_capture_cfg(const char *cfg);

int alsa_init(glc_t *glc)
{
	alsa.glc = glc;
	alsa.started = alsa.capturing = 0;
	alsa.capture_stream = NULL;
	alsa.audio_hook = NULL;
	int ret = 0;

	glc_log(alsa.glc, GLC_DEBUG, "alsa", "initializing");

	if (getenv("GLC_AUDIO"))
		alsa.capture = atoi(getenv("GLC_AUDIO"));
	else
		alsa.capture = 1;

	/* initialize audio hook system */
	if (alsa.capture) {
		if ((ret = audio_hook_init(&alsa.audio_hook, alsa.glc)))
			return ret;

		audio_hook_allow_skip(alsa.audio_hook, 0);
		if (getenv("GLC_AUDIO_SKIP"))
			audio_hook_allow_skip(alsa.audio_hook, atoi(getenv("GLC_AUDIO_SKIP")));
	}

	if (getenv("GLC_AUDIO_RECORD"))
		alsa_parse_capture_cfg(getenv("GLC_AUDIO_RECORD"));

	get_real_alsa();

	/* make sure libasound.so does not call our hooked functions */
	alsa_unhook_so("*libasound.so*");

	return 0;
}

int alsa_parse_capture_cfg(const char *cfg)
{
	struct alsa_capture_stream_s *stream;
	const char *args, *next, *device = cfg;
	unsigned int channels, rate;
	size_t len;

	while (device != NULL) {
		while (*device == ';')
			device++;
		if (*device == '\0')
			break;

		channels = 2;
		rate = 44100;

		/* check if some args have been given */
		if ((args = strstr(device, ",")))
			sscanf(args, ",%u,%u", &rate, &channels);
		next = strstr(device, ";");

		stream = malloc(sizeof(struct alsa_capture_stream_s));
		memset(stream, 0, sizeof(struct alsa_capture_stream_s));

		if (args)
			len = args - device;
		else if (next)
			len = next - device;
		else
			len = strlen(device);

		stream->device = (char *) malloc(sizeof(char) * len);
		memcpy(stream->device, device, len);
		stream->device[len] = '\0';

		stream->channels = channels;
		stream->rate = rate;
		stream->next = alsa.capture_stream;
		alsa.capture_stream = stream;

		device = next;
	}

	return 0;
}

int alsa_start(ps_buffer_t *buffer)
{
	struct alsa_capture_stream_s *stream = alsa.capture_stream;
	int ret;

	if (alsa.started)
		return EINVAL;

	if (alsa.audio_hook) {
		if ((ret = audio_hook_set_buffer(alsa.audio_hook, buffer)))
			return ret;
	}

	/* start capture streams */
	while (stream != NULL) {
		audio_capture_init(&stream->capture, alsa.glc);
		audio_capture_set_device(stream->capture, stream->device);
		audio_capture_set_rate(stream->capture, stream->rate);
		audio_capture_set_channels(stream->capture, stream->channels);

		stream = stream->next;
	}

	alsa.started = 1;
	return 0;
}

int alsa_close()
{
	struct alsa_capture_stream_s *del;

	if (!alsa.started)
		return 0;

	glc_log(alsa.glc, GLC_DEBUG, "alsa", "closing");

	if (alsa.capture) {
		if (alsa.capturing)
			audio_hook_stop(alsa.audio_hook);
		audio_hook_destroy(alsa.audio_hook);
	}

	while (alsa.capture_stream != NULL) {
		del = alsa.capture_stream;
		alsa.capture_stream = alsa.capture_stream->next;

		if (del->capture)
			audio_capture_destroy(del->capture);

		free(del->device);
		free(del);
	}

	return 0;
}

int alsa_capture_stop()
{
	struct alsa_capture_stream_s *stream = alsa.capture_stream;

	if (!alsa.capturing)
		return 0;

	while (stream != NULL) {
		if (stream->capture)
			audio_capture_start(stream->capture);
		stream = stream->next;
	}

	if (alsa.capture)
		audio_hook_stop(alsa.audio_hook);

	alsa.capturing = 0;
	return 0;
}

int alsa_capture_start()
{
	struct alsa_capture_stream_s *stream = alsa.capture_stream;

	if (alsa.capturing)
		return 0;

	while (stream != NULL) {
		if (stream->capture)
			audio_capture_start(stream->capture);
		stream = stream->next;
	}

	if (alsa.capture)
		audio_hook_start(alsa.audio_hook);

	alsa.capturing = 1;
	return 0;
}

void get_real_alsa()
{
	if (!lib.dlopen)
		get_real_dlsym();

	if (alsa_loaded)
		return;

	alsa.libasound_handle = lib.dlopen("libasound.so.2", RTLD_LAZY);
	if (!alsa.libasound_handle)
		goto err;

	alsa.snd_pcm_open =
	  (int (*)(snd_pcm_t **, const char *, snd_pcm_stream_t, int))
	    lib.dlsym(alsa.libasound_handle, "snd_pcm_open");
	if (!alsa.snd_pcm_open)
		goto err;

	alsa.snd_pcm_hw_params =
	  (int (*)(snd_pcm_t *, snd_pcm_hw_params_t *))
	    lib.dlsym(alsa.libasound_handle, "snd_pcm_hw_params");
	if (!alsa.snd_pcm_hw_params)
		goto err;

	alsa.snd_pcm_open_lconf =
	  (int (*)(snd_pcm_t **, const char *, snd_pcm_stream_t, int, snd_config_t *))
	    lib.dlsym(alsa.libasound_handle, "snd_pcm_open_lconf");
	if (!alsa.snd_pcm_open_lconf)
		goto err;

	alsa.snd_pcm_close =
	  (int (*)(snd_pcm_t *))
	    lib.dlsym(alsa.libasound_handle, "snd_pcm_close");
	if (!alsa.snd_pcm_close)
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

	alsa.snd_pcm_mmap_writei =
	  (snd_pcm_sframes_t (*)(snd_pcm_t *, const void *, snd_pcm_uframes_t))
	    lib.dlsym(alsa.libasound_handle, "snd_pcm_mmap_writei");
	if (!alsa.snd_pcm_writei)
		goto err;

	alsa.snd_pcm_mmap_writen =
	  (snd_pcm_sframes_t (*)(snd_pcm_t *, void **, snd_pcm_uframes_t))
	    lib.dlsym(alsa.libasound_handle, "snd_pcm_mmap_writen");
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
	if (!alsa.snd_pcm_mmap_commit)
		goto err;

	alsa_loaded = 1;
	return;
err:
	fprintf(stderr, "(glc:alsa) can't get real alsa");
	exit(1);
}

int alsa_unhook_so(const char *soname)
{
	int ret;
	eh_obj_t so;

	if (!alsa_loaded)
		get_real_alsa(); /* make sure we have real functions */

	if ((ret = eh_find_obj(&so, soname)))
		return ret;

	/* don't look at 'elfhacks'... contains some serious black magic */
	eh_set_rel(&so, "snd_pcm_open", alsa.snd_pcm_open);
	eh_set_rel(&so, "snd_pcm_open_lconf", alsa.snd_pcm_open_lconf);
	eh_set_rel(&so, "snd_pcm_close", alsa.snd_pcm_close);
	eh_set_rel(&so, "snd_pcm_hw_params", alsa.snd_pcm_hw_params);
	eh_set_rel(&so, "snd_pcm_writei", alsa.snd_pcm_writei);
	eh_set_rel(&so, "snd_pcm_writen", alsa.snd_pcm_writen);
	eh_set_rel(&so, "snd_pcm_mmap_writei", alsa.snd_pcm_mmap_writei);
	eh_set_rel(&so, "snd_pcm_mmap_writen", alsa.snd_pcm_mmap_writen);
	eh_set_rel(&so, "snd_pcm_mmap_begin", alsa.snd_pcm_mmap_begin);
	eh_set_rel(&so, "snd_pcm_mmap_commit", alsa.snd_pcm_mmap_commit);
	eh_set_rel(&so, "dlsym", lib.dlsym);
	eh_set_rel(&so, "dlvsym", lib.dlvsym);

	eh_destroy_obj(&so);

	return 0;
}

__PUBLIC int snd_pcm_open(snd_pcm_t **pcmp, const char *name, snd_pcm_stream_t stream, int mode)
{
	return __alsa_snd_pcm_open(pcmp, name, stream, mode);
}

int __alsa_snd_pcm_open(snd_pcm_t **pcmp, const char *name, snd_pcm_stream_t stream, int mode)
{
	/* it is not necessarily safe to call glc_init() from write funcs
	   especially async mode (initiated from signal) is troublesome */
	INIT_GLC
	int ret = alsa.snd_pcm_open(pcmp, name, stream, mode);
	if ((alsa.capture) && (ret == 0))
		audio_hook_alsa_open(alsa.audio_hook, *pcmp, name, stream, mode);
	return ret;
}

__PUBLIC int snd_pcm_open_lconf(snd_pcm_t **pcmp, const char *name, snd_pcm_stream_t stream,
				int mode, snd_config_t *lconf)
{
	return __alsa_snd_pcm_open_lconf(pcmp, name, stream, mode, lconf);
}

int __alsa_snd_pcm_open_lconf(snd_pcm_t **pcmp, const char *name, snd_pcm_stream_t stream,
			      int mode, snd_config_t *lconf)
{
	INIT_GLC
	int ret = alsa.snd_pcm_open_lconf(pcmp, name, stream, mode, lconf);
	if ((alsa.capture) && (ret == 0))
		audio_hook_alsa_open(alsa.audio_hook, *pcmp, name, stream, mode);
	return ret;
}

__PUBLIC int snd_pcm_close(snd_pcm_t *pcm)
{
	return __alsa_snd_pcm_close(pcm);
}

int __alsa_snd_pcm_close(snd_pcm_t *pcm)
{
	INIT_GLC
	int ret = alsa.snd_pcm_close(pcm);
	if ((alsa.capture) && (ret == 0))
		audio_hook_alsa_close(alsa.audio_hook, pcm);
	return ret;
}

__PUBLIC int snd_pcm_hw_params(snd_pcm_t *pcm, snd_pcm_hw_params_t *params)
{
	return __alsa_snd_pcm_hw_params(pcm, params);
}

int __alsa_snd_pcm_hw_params(snd_pcm_t *pcm, snd_pcm_hw_params_t *params)
{
	INIT_GLC
	int ret = alsa.snd_pcm_hw_params(pcm, params);
	if ((alsa.capture) && (ret == 0))
		audio_hook_alsa_hw_params(alsa.audio_hook, pcm, params);
	return ret;
}

__PUBLIC snd_pcm_sframes_t snd_pcm_writei(snd_pcm_t *pcm, const void *buffer, snd_pcm_uframes_t size)
{
	return __alsa_snd_pcm_writei(pcm, buffer, size);
}

snd_pcm_sframes_t __alsa_snd_pcm_writei(snd_pcm_t *pcm, const void *buffer, snd_pcm_uframes_t size)
{
	INIT_GLC
	snd_pcm_sframes_t ret = alsa.snd_pcm_writei(pcm, buffer, size);
	if ((alsa.capture) && (ret > 0) && alsa.capturing)
		audio_hook_alsa_i(alsa.audio_hook, pcm, buffer, ret);
	return ret;
}

__PUBLIC snd_pcm_sframes_t snd_pcm_writen(snd_pcm_t *pcm, void **bufs, snd_pcm_uframes_t size)
{
	return __alsa_snd_pcm_writen(pcm, bufs, size);
}

snd_pcm_sframes_t __alsa_snd_pcm_writen(snd_pcm_t *pcm, void **bufs, snd_pcm_uframes_t size)
{
	INIT_GLC
	snd_pcm_sframes_t ret = alsa.snd_pcm_writen(pcm, bufs, size);
	if (alsa.capture && (ret > 0))
		audio_hook_alsa_n(alsa.audio_hook, pcm, bufs, ret);
	return ret;
}

__PUBLIC snd_pcm_sframes_t snd_pcm_mmap_writei(snd_pcm_t *pcm, const void *buffer, snd_pcm_uframes_t size)
{
	return __alsa_snd_pcm_mmap_writei(pcm, buffer, size);
}

snd_pcm_sframes_t __alsa_snd_pcm_mmap_writei(snd_pcm_t *pcm, const void *buffer, snd_pcm_uframes_t size)
{
	INIT_GLC
	snd_pcm_sframes_t ret = alsa.snd_pcm_mmap_writei(pcm, buffer, size);
	if (alsa.capture && (ret > 0))
		audio_hook_alsa_i(alsa.audio_hook, pcm, buffer, ret);
	return ret;
}

__PUBLIC snd_pcm_sframes_t snd_pcm_mmap_writen(snd_pcm_t *pcm, void **bufs, snd_pcm_uframes_t size)
{
	return __alsa_snd_pcm_mmap_writen(pcm, bufs, size);
}

snd_pcm_sframes_t __alsa_snd_pcm_mmap_writen(snd_pcm_t *pcm, void **bufs, snd_pcm_uframes_t size)
{
	INIT_GLC
	snd_pcm_sframes_t ret = alsa.snd_pcm_mmap_writen(pcm, bufs, size);
	if ((alsa.capture) && (ret > 0))
		audio_hook_alsa_n(alsa.audio_hook, pcm, bufs, ret);
	return ret;
}

__PUBLIC int snd_pcm_mmap_begin(snd_pcm_t *pcm, const snd_pcm_channel_area_t **areas, snd_pcm_uframes_t *offset, snd_pcm_uframes_t *frames)
{
	return __alsa_snd_pcm_mmap_begin(pcm, areas, offset, frames);
}

int __alsa_snd_pcm_mmap_begin(snd_pcm_t *pcm, const snd_pcm_channel_area_t **areas, snd_pcm_uframes_t *offset, snd_pcm_uframes_t *frames)
{
	INIT_GLC
	int ret = alsa.snd_pcm_mmap_begin(pcm, areas, offset, frames);
	if (alsa.capture && (ret >= 0))
		audio_hook_alsa_mmap_begin(alsa.audio_hook, pcm, *areas, *offset, *frames);
	return ret;
}

__PUBLIC snd_pcm_sframes_t snd_pcm_mmap_commit(snd_pcm_t *pcm, snd_pcm_uframes_t offset, snd_pcm_uframes_t frames)
{
	return __alsa_snd_pcm_mmap_commit(pcm, offset, frames);
}

snd_pcm_sframes_t __alsa_snd_pcm_mmap_commit(snd_pcm_t *pcm, snd_pcm_uframes_t offset, snd_pcm_uframes_t frames)
{
	INIT_GLC
	snd_pcm_uframes_t ret;
	if (alsa.capture)
		audio_hook_alsa_mmap_commit(alsa.audio_hook, pcm, offset,  frames);

	ret = alsa.snd_pcm_mmap_commit(pcm, offset, frames);
	if (ret != frames)
		glc_log(alsa.glc, GLC_WARNING, "alsa", "frames=%lu, ret=%ld", frames, ret);
	return ret;
}

/**  \} */
/**  \} */
