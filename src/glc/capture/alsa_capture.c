/**
 * \file glc/capture/alsa_capture.c
 * \brief audio capture
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup alsa_capture
 *  \{
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sched.h>
#include <packetstream.h>
#include <alsa/asoundlib.h>
#include <pthread.h>

#include <glc/common/glc.h>
#include <glc/common/core.h>
#include <glc/common/log.h>
#include <glc/common/state.h>
#include <glc/common/util.h>

#include "alsa_capture.h"

struct alsa_capture_s {
	glc_t *glc;
	ps_buffer_t *to;

	glc_state_audio_t state_audio;
	glc_stream_id_t id;

	snd_pcm_t *pcm;
	snd_pcm_uframes_t period_size;

	glc_flags_t flags;
	const char *device;
	unsigned int channels;
	unsigned int rate;
	unsigned int min_periods;
	snd_pcm_format_t format;
	ssize_t bytes_per_frame;
	int rate_usec;
	size_t period_size_in_bytes;

	snd_async_handler_t *async_handler;

	pthread_t capture_thread;
	sem_t capture;
	int skip_data;
	int stop_capture;
};

int alsa_capture_open(alsa_capture_t alsa_capture);
int alsa_capture_init_hw(alsa_capture_t alsa_capture, snd_pcm_hw_params_t *hw_params);
int alsa_capture_init_sw(alsa_capture_t alsa_capture, snd_pcm_sw_params_t *sw_params);

void alsa_capture_async_callback(snd_async_handler_t *async_handler);
void *alsa_capture_thread(void *argptr);

glc_audio_format_t alsa_capture_glc_format(snd_pcm_format_t pcm_fmt);

int alsa_capture_xrun(alsa_capture_t alsa_capture, int err);
int alsa_capture_stop(alsa_capture_t alsa_capture);

int alsa_capture_init(alsa_capture_t *alsa_capture, glc_t *glc)
{
	*alsa_capture = (alsa_capture_t) malloc(sizeof(struct alsa_capture_s));
	memset(*alsa_capture, 0, sizeof(struct alsa_capture_s));
	pthread_attr_t attr;

	(*alsa_capture)->glc = glc;
	(*alsa_capture)->device = "default";
	(*alsa_capture)->channels = 2;
	(*alsa_capture)->rate = 44100;
	(*alsa_capture)->min_periods = 2;
	glc_state_audio_new((*alsa_capture)->glc, &(*alsa_capture)->id,
			    &(*alsa_capture)->state_audio);
	(*alsa_capture)->skip_data = 1;

	sem_init(&(*alsa_capture)->capture, 0, 0);

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	pthread_create(&(*alsa_capture)->capture_thread, &attr, alsa_capture_thread, (void *) *alsa_capture);
	pthread_attr_destroy(&attr);

	return 0;
}

int alsa_capture_destroy(alsa_capture_t alsa_capture)
{
	if (alsa_capture == NULL)
		return EINVAL;

	/** \todo snd_pcm_drain() ? */
	if (alsa_capture->pcm)
		snd_pcm_close(alsa_capture->pcm);

	alsa_capture->stop_capture = 1;
	sem_post(&alsa_capture->capture);
	pthread_join(alsa_capture->capture_thread, NULL);

	free(alsa_capture);
	return 0;
}

int alsa_capture_set_device(alsa_capture_t alsa_capture, const char *device)
{
	if (alsa_capture->pcm)
		return EALREADY;

	alsa_capture->device = device;
	return 0;
}

int alsa_capture_set_rate(alsa_capture_t alsa_capture, unsigned int rate)
{
	if (alsa_capture->pcm)
		return EALREADY;

	alsa_capture->rate = rate;
	return 0;
}

int alsa_capture_set_channels(alsa_capture_t alsa_capture, unsigned int channels)
{
	if (alsa_capture->pcm)
		return EALREADY;

	alsa_capture->channels = channels;
	return 0;
}

int alsa_capture_start(alsa_capture_t alsa_capture)
{
	int ret;
	if (alsa_capture == NULL)
		return EINVAL;

	if (alsa_capture->skip_data)
		glc_log(alsa_capture->glc, GLC_WARNING, "alsa_capture",
			 "device %s already started", alsa_capture->device);
	else
		glc_log(alsa_capture->glc, GLC_INFORMATION, "alsa_capture",
			 "starting device %s", alsa_capture->device);

	if (!alsa_capture->pcm) {
		if ((ret = alsa_capture_open(alsa_capture)))
			return ret;
	}

	alsa_capture->skip_data = 0;
	return 0;
}

int alsa_capture_stop(alsa_capture_t alsa_capture)
{
	if (alsa_capture == NULL)
		return EINVAL;

	if (alsa_capture->skip_data)
		glc_log(alsa_capture->glc, GLC_INFORMATION, "alsa_capture",
			 "stopping device %s", alsa_capture->device);
	else
		glc_log(alsa_capture->glc, GLC_WARNING, "alsa_capture",
			 "device %s already stopped", alsa_capture->device);

	alsa_capture->skip_data = 1;
	return 0;
}

int alsa_capture_open(alsa_capture_t alsa_capture)
{
	snd_pcm_hw_params_t *hw_params = NULL;
	snd_pcm_sw_params_t *sw_params = NULL;
	ps_packet_t packet;
	int dir, ret = 0;
	glc_message_header_t msg_hdr;
	glc_audio_format_message_t fmt_msg;

	glc_log(alsa_capture->glc, GLC_DEBUG, "alsa_capture", "opening device %s",
		 alsa_capture->device);

	/* open pcm */
	if ((ret = snd_pcm_open(&alsa_capture->pcm, alsa_capture->device, SND_PCM_STREAM_CAPTURE, 0)) < 0)
		goto err;

	/* init hw */
	if ((ret = snd_pcm_hw_params_malloc(&hw_params)) < 0)
		goto err;
	if ((ret = -alsa_capture_init_hw(alsa_capture, hw_params)))
		goto err;

	/* set software params */
	if ((ret = snd_pcm_sw_params_malloc(&sw_params)) < 0)
		goto err;
	if ((ret = -alsa_capture_init_sw(alsa_capture, sw_params)))
		goto err;

	/* we need period size */
	if ((ret = snd_pcm_hw_params_get_period_size(hw_params, &alsa_capture->period_size, NULL)))
		goto err;
	alsa_capture->bytes_per_frame = snd_pcm_frames_to_bytes(alsa_capture->pcm, 1);
	alsa_capture->period_size_in_bytes = alsa_capture->period_size * alsa_capture->bytes_per_frame;

	/* read actual settings */
	if ((ret = snd_pcm_hw_params_get_format(hw_params, &alsa_capture->format)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_get_rate(hw_params, &alsa_capture->rate, &dir)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_get_channels(hw_params, &alsa_capture->channels)) < 0)
		goto err;

	alsa_capture->rate_usec = 1000000 / alsa_capture->rate;

	alsa_capture->flags = GLC_AUDIO_INTERLEAVED;

	/* prepare packet */
	fmt_msg.id = alsa_capture->id;
	fmt_msg.rate = alsa_capture->rate;
	fmt_msg.channels = alsa_capture->channels;
	fmt_msg.flags = alsa_capture->flags;
	fmt_msg.format = alsa_capture_glc_format(alsa_capture->format);

	if (!fmt_msg.format) {
		glc_log(alsa_capture->glc, GLC_ERROR, "alsa_capture",
			"unsupported audio format 0x%02x", alsa_capture->format);
		return ENOTSUP;
	}

	msg_hdr.type = GLC_MESSAGE_AUDIO_FORMAT;
	ps_packet_init(&packet, alsa_capture->to);
	ps_packet_open(&packet, PS_PACKET_WRITE);
	ps_packet_write(&packet, &msg_hdr, GLC_MESSAGE_HEADER_SIZE);
	ps_packet_write(&packet, &fmt_msg, GLC_AUDIO_FORMAT_MESSAGE_SIZE);
	ps_packet_close(&packet);
	ps_packet_destroy(&packet);

	snd_pcm_hw_params_free(hw_params);
	snd_pcm_sw_params_free(sw_params);

	/* init callback */
	if ((ret = snd_async_add_pcm_handler(&alsa_capture->async_handler, alsa_capture->pcm,
					     alsa_capture_async_callback, alsa_capture)) < 0)
		goto err;
	if ((ret = snd_pcm_start(alsa_capture->pcm)) < 0)
		goto err;

	glc_log(alsa_capture->glc, GLC_DEBUG, "alsa_capture",
		 "success (stream=%d, device=%s, rate=%u, channels=%u)", alsa_capture->id,
		 alsa_capture->device, alsa_capture->rate, alsa_capture->channels);

	return 0;
err:
	if (hw_params)
		snd_pcm_hw_params_free(hw_params);
	if (sw_params)
		snd_pcm_sw_params_free(sw_params);

	glc_log(alsa_capture->glc, GLC_ERROR, "alsa_capture",
		 "initialization failed: %s", snd_strerror(ret));
	return -ret;
}

glc_audio_format_t alsa_capture_glc_format(snd_pcm_format_t pcm_fmt)
{
	switch (pcm_fmt) {
	case SND_PCM_FORMAT_S16_LE:
		return GLC_AUDIO_S16_LE;
	case SND_PCM_FORMAT_S24_LE:
		return GLC_AUDIO_S24_LE;
	case SND_PCM_FORMAT_S32_LE:
		return GLC_AUDIO_S32_LE;
	default:
		return 0;
	}
}

int alsa_capture_init_hw(alsa_capture_t alsa_capture, snd_pcm_hw_params_t *hw_params)
{
	snd_pcm_format_mask_t *formats = NULL;
	snd_pcm_uframes_t max_buffer_size;
	unsigned int min_periods;
	int dir, ret = 0;

	if ((ret = snd_pcm_hw_params_any(alsa_capture->pcm, hw_params)) < 0)
		goto err;

	if ((ret = snd_pcm_hw_params_set_access(alsa_capture->pcm, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
		goto err;

	formats = (snd_pcm_format_mask_t *) malloc(snd_pcm_format_mask_sizeof());
	snd_pcm_format_mask_none(formats);
	snd_pcm_format_mask_set(formats, SND_PCM_FORMAT_S16_LE);
	snd_pcm_format_mask_set(formats, SND_PCM_FORMAT_S24_LE);
	snd_pcm_format_mask_set(formats, SND_PCM_FORMAT_S32_LE);

	if ((ret = snd_pcm_hw_params_set_format_mask(alsa_capture->pcm, hw_params, formats)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_set_channels(alsa_capture->pcm, hw_params, alsa_capture->channels)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_set_rate(alsa_capture->pcm, hw_params, alsa_capture->rate, 0)) < 0)
		goto err;

	if ((ret = snd_pcm_hw_params_get_buffer_size_max(hw_params, &max_buffer_size)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_set_buffer_size(alsa_capture->pcm, hw_params, max_buffer_size)) < 0)
		goto err;

	if ((ret = snd_pcm_hw_params_get_periods_min(hw_params, &min_periods, &dir)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_set_periods(alsa_capture->pcm, hw_params,
						 min_periods < alsa_capture->min_periods ?
						 alsa_capture->min_periods : min_periods, dir)) < 0)
		goto err;

	if ((ret = snd_pcm_hw_params(alsa_capture->pcm, hw_params)) < 0)
		goto err;

	free(formats);
	return 0;
err:
	if (formats)
		free(formats);
	return -ret;
}

int alsa_capture_init_sw(alsa_capture_t alsa_capture, snd_pcm_sw_params_t *sw_params)
{
	int ret = 0;

	if ((ret = snd_pcm_sw_params_current(alsa_capture->pcm, sw_params)) < 0)
		goto err;
	if ((ret = snd_pcm_sw_params(alsa_capture->pcm, sw_params)))
		goto err;

	return 0;
err:
	return -ret;
}

void alsa_capture_async_callback(snd_async_handler_t *async_handler)
{
	/*
	 Async handler is called from signal handler so mixing this
	 with mutexes seems to be a retared idea.

	 http://www.kaourantin.net/2006/08/pthreads-and-signals.html
	 */
	alsa_capture_t alsa_capture =
		snd_async_handler_get_callback_private(async_handler);
	sem_post(&alsa_capture->capture);
}

void *alsa_capture_thread(void *argptr)
{
	alsa_capture_t alsa_capture = argptr;
	snd_pcm_sframes_t avail, read;
	glc_utime_t time, delay_usec = 0;
	glc_audio_data_header_t hdr;
	glc_message_header_t msg_hdr;
	ps_packet_t packet;
	int ret;
	char *dma;

	ps_packet_init(&packet, alsa_capture->to);
	msg_hdr.type = GLC_MESSAGE_AUDIO_DATA;

	while (!sem_wait(&alsa_capture->capture)) {
		if (alsa_capture->stop_capture)
			break;

		avail = 0;
		if ((ret = snd_pcm_delay(alsa_capture->pcm, &avail)) < 0)
			alsa_capture_xrun(alsa_capture, ret);

		while (avail >= alsa_capture->period_size) {
			/* loop till we have one period available */
			avail = snd_pcm_avail_update(alsa_capture->pcm);
			if (avail < alsa_capture->period_size)
				continue;

			/* and discard it if glc is paused */
			if (alsa_capture->skip_data) {
				fprintf(stderr, "snd_pcm_reset()\n");
				snd_pcm_reset(alsa_capture->pcm);
				continue;
			}

			time = glc_state_time(alsa_capture->glc);
			delay_usec = avail * alsa_capture->rate_usec;

			if (delay_usec < time)
				time -= delay_usec;
			hdr.time = time;
			hdr.size = alsa_capture->period_size_in_bytes;
			hdr.id = alsa_capture->id;

			if ((ret = ps_packet_open(&packet, PS_PACKET_WRITE)))
				goto cancel;
			if ((ret = ps_packet_write(&packet, &msg_hdr, GLC_MESSAGE_HEADER_SIZE)))
				goto cancel;
			if ((ret = ps_packet_write(&packet, &hdr, GLC_AUDIO_DATA_HEADER_SIZE)))
				goto cancel;
			if ((ret = ps_packet_dma(&packet, (void *) &dma, hdr.size, PS_ACCEPT_FAKE_DMA)))
				goto cancel;

			if ((read = snd_pcm_readi(alsa_capture->pcm, dma, alsa_capture->period_size)) < 0)
				read = -alsa_capture_xrun(alsa_capture, read);

			if (read != alsa_capture->period_size)
				glc_log(alsa_capture->glc, GLC_WARNING, "alsa_capture",
					 "read %ld, expected %zd", read * alsa_capture->bytes_per_frame,
					 alsa_capture->period_size_in_bytes);
			else if (read < 0)
				glc_log(alsa_capture->glc, GLC_ERROR, "alsa_capture",
					 "xrun recovery failed: %s", snd_strerror(read));

			hdr.size = read * alsa_capture->bytes_per_frame;
			if ((ret = ps_packet_setsize(&packet, GLC_MESSAGE_HEADER_SIZE +
							     GLC_AUDIO_DATA_HEADER_SIZE +
							     hdr.size)))
				goto cancel;
			if ((ret = ps_packet_close(&packet)))
				goto cancel;

			/* just check for xrun */
			if ((ret = snd_pcm_delay(alsa_capture->pcm, &avail)) < 0) {
				alsa_capture_xrun(alsa_capture, ret);
				break;
			}
			continue;

cancel:
			glc_log(alsa_capture->glc, GLC_ERROR, "alsa_capture", "%s (%d)", strerror(ret), ret);
			if (ret == EINTR)
				break;
			if (ps_packet_cancel(&packet))
				break;
		}
	}

	ps_packet_destroy(&packet);
	return NULL;
}

int alsa_capture_xrun(alsa_capture_t alsa_capture, int err)
{
	glc_log(alsa_capture->glc, GLC_DEBUG, "alsa_capture", "xrun");

	if (err == -EPIPE) {
		if ((err = snd_pcm_prepare(alsa_capture->pcm)) < 0)
			return -err;
		if ((err = snd_pcm_start(alsa_capture->pcm)) < 0)
			return -err;
		return 0;
	} else if (err == -ESTRPIPE) {
		while ((err = snd_pcm_resume(alsa_capture->pcm)) == -EAGAIN)
			sched_yield();
		if (err < 0) {
			if ((err = snd_pcm_prepare(alsa_capture->pcm)) < 0)
				return -err;
			if ((err = snd_pcm_start(alsa_capture->pcm)) < 0)
				return -err;
			return 0;
		}
	}

	return -err;
}

/**  \} */
