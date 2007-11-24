/**
 * \file src/capture/audio_capture.c
 * \brief audio capture
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* audio_capture.c -- audio capture
 * Copyright (C) 2007 Pyry Haulos
 * For conditions of distribution and use, see copyright notice in glc.h
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <packetstream.h>
#include <alsa/asoundlib.h>
#include <pthread.h>

#include "../common/glc.h"
#include "../common/util.h"
#include "audio_capture.h"

/**
 * \addtogroup capture
 *  \{
 */

/**
 * \defgroup audio_capture audio capture
 *  \{
 */

struct audio_capture_private_s {
	glc_t *glc;
	ps_buffer_t *to;

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
	int stop_capture;
};

int audio_capture_open(struct audio_capture_private_s *audio_capture);
int audio_capture_init_hw(struct audio_capture_private_s *audio_capture, snd_pcm_hw_params_t *hw_params);
int audio_capture_init_sw(struct audio_capture_private_s *audio_capture, snd_pcm_sw_params_t *sw_params);

void audio_capture_async_callback(snd_async_handler_t *async_handler);
void *audio_capture_thread(void *argptr);

glc_flags_t audio_capture_fmt_flags(snd_pcm_format_t pcm_fmt);

int audio_capture_xrun(struct audio_capture_private_s *audio_capture, int err);

void *audio_capture_init(glc_t *glc, ps_buffer_t *to, const char *device, unsigned int rate, unsigned int channels)
{
	struct audio_capture_private_s *audio_capture = malloc(sizeof(struct audio_capture_private_s));
	memset(audio_capture, 0, sizeof(struct audio_capture_private_s));
	pthread_attr_t attr;

	audio_capture->glc = glc;
	audio_capture->to = to;
	audio_capture->device = device;
	audio_capture->channels = channels;
	audio_capture->rate = rate;
	audio_capture->min_periods = 2;

	sem_init(&audio_capture->capture, 0, 0);

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	pthread_create(&audio_capture->capture_thread, &attr, audio_capture_thread, audio_capture);
	pthread_attr_destroy(&attr);

	if (audio_capture_open(audio_capture))
		return NULL;

	return audio_capture;
}

int audio_capture_close(void *audiopriv)
{
	struct audio_capture_private_s *audio_capture = audiopriv;
	if (audio_capture == NULL)
		return EINVAL;

	/* TODO snd_pcm_drain() ? */
	if (audio_capture->pcm)
		snd_pcm_close(audio_capture->pcm);

	audio_capture->stop_capture = 1;
	sem_post(&audio_capture->capture);
	pthread_join(audio_capture->capture_thread, NULL);

	free(audio_capture);
	return 0;
}

int audio_capture_open(struct audio_capture_private_s *audio_capture)
{
	snd_pcm_hw_params_t *hw_params = NULL;
	snd_pcm_sw_params_t *sw_params = NULL;
	ps_packet_t packet;
	int dir, ret = 0;
	glc_message_header_t msg_hdr;
	glc_audio_format_message_t fmt_msg;

	/* open pcm */
	if ((ret = snd_pcm_open(&audio_capture->pcm, audio_capture->device, SND_PCM_STREAM_CAPTURE, 0)) < 0)
		goto err;

	/* init hw */
	if ((ret = snd_pcm_hw_params_malloc(&hw_params)) < 0)
		goto err;
	if ((ret = -audio_capture_init_hw(audio_capture, hw_params)))
		goto err;

	/* set software params */
	if ((ret = snd_pcm_sw_params_malloc(&sw_params)) < 0)
		goto err;
	if ((ret = -audio_capture_init_sw(audio_capture, sw_params)))
		goto err;

	/* we need period size */
	if ((ret = snd_pcm_hw_params_get_period_size(hw_params, &audio_capture->period_size, NULL)))
		goto err;
	audio_capture->bytes_per_frame = snd_pcm_frames_to_bytes(audio_capture->pcm, 1);
	audio_capture->period_size_in_bytes = audio_capture->period_size * audio_capture->bytes_per_frame;

	/* read actual settings */
	if ((ret = snd_pcm_hw_params_get_format(hw_params, &audio_capture->format)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_get_rate(hw_params, &audio_capture->rate, &dir)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_get_channels(hw_params, &audio_capture->channels)) < 0)
		goto err;
	audio_capture->rate_usec = 1000000 / audio_capture->rate;

	audio_capture->flags = 0; /* zero flags */
	audio_capture->flags |= audio_capture_fmt_flags(audio_capture->format);
	if (audio_capture->flags & GLC_AUDIO_FORMAT_UNKNOWN) {
		util_log(audio_capture->glc, GLC_ERROR, "audio_capture",
			 "unsupported audio format 0x%02x", audio_capture->format);
		return ENOTSUP;
	}

	/* prepare packet */
	fmt_msg.rate = audio_capture->rate;
	fmt_msg.channels = audio_capture->channels;

	msg_hdr.type = GLC_MESSAGE_AUDIO_FORMAT;
	ps_packet_init(&packet, audio_capture->to);
	ps_packet_open(&packet, PS_PACKET_WRITE);
	ps_packet_write(&packet, &msg_hdr, GLC_MESSAGE_HEADER_SIZE);
	ps_packet_write(&packet, &fmt_msg, GLC_AUDIO_FORMAT_MESSAGE_SIZE);
	ps_packet_close(&packet);
	ps_packet_destroy(&packet);

	snd_pcm_hw_params_free(hw_params);
	snd_pcm_sw_params_free(sw_params);

	/* init callback */
	if ((ret = snd_async_add_pcm_handler(&audio_capture->async_handler, audio_capture->pcm, audio_capture_async_callback, audio_capture)) < 0)
		goto err;
	if ((ret = snd_pcm_start(audio_capture->pcm)) < 0)
		goto err;
	return 0;
err:
	if (hw_params)
		snd_pcm_hw_params_free(hw_params);
	if (sw_params)
		snd_pcm_sw_params_free(sw_params);
	util_log(audio_capture->glc, GLC_ERROR, "audio_capture",
		 "initialization failed: %s", snd_strerror(ret));
	return -ret;
}

glc_flags_t audio_capture_fmt_flags(snd_pcm_format_t pcm_fmt)
{
	switch (pcm_fmt) {
	case SND_PCM_FORMAT_S16_LE:
		return GLC_AUDIO_S16_LE;
	case SND_PCM_FORMAT_S24_LE:
		return GLC_AUDIO_S24_LE;
	case SND_PCM_FORMAT_S32_LE:
		return GLC_AUDIO_S32_LE;
	default:
		return GLC_AUDIO_FORMAT_UNKNOWN;
	}
}

int audio_capture_init_hw(struct audio_capture_private_s *audio_capture, snd_pcm_hw_params_t *hw_params)
{
	snd_pcm_format_mask_t *formats = NULL;
	snd_pcm_uframes_t max_buffer_size;
	unsigned int min_periods;
	int dir, ret = 0;

	if ((ret = snd_pcm_hw_params_any(audio_capture->pcm, hw_params)) < 0)
		goto err;

	if ((ret = snd_pcm_hw_params_set_access(audio_capture->pcm, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
		goto err;

	formats = (snd_pcm_format_mask_t *) malloc(snd_pcm_format_mask_sizeof());
	snd_pcm_format_mask_none(formats);
	snd_pcm_format_mask_set(formats, SND_PCM_FORMAT_S16_LE);
	snd_pcm_format_mask_set(formats, SND_PCM_FORMAT_S24_LE);
	snd_pcm_format_mask_set(formats, SND_PCM_FORMAT_S32_LE);

	if ((ret = snd_pcm_hw_params_set_format_mask(audio_capture->pcm, hw_params, formats)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_set_channels(audio_capture->pcm, hw_params, audio_capture->channels)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_set_rate(audio_capture->pcm, hw_params, audio_capture->rate, 0)) < 0)
		goto err;

	if ((ret = snd_pcm_hw_params_get_buffer_size_max(hw_params, &max_buffer_size)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_set_buffer_size(audio_capture->pcm, hw_params, max_buffer_size)) < 0)
		goto err;

	if ((ret = snd_pcm_hw_params_get_periods_min(hw_params, &min_periods, &dir)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_set_periods(audio_capture->pcm, hw_params, min_periods < audio_capture->min_periods ? audio_capture->min_periods : min_periods, dir)) < 0)
		goto err;

	if ((ret = snd_pcm_hw_params(audio_capture->pcm, hw_params)) < 0)
		goto err;

	free(formats);
	return 0;
err:
	if (formats)
		free(formats);
	return -ret;
}

int audio_capture_init_sw(struct audio_capture_private_s *audio_capture, snd_pcm_sw_params_t *sw_params)
{
	int ret = 0;

	if ((ret = snd_pcm_sw_params_current(audio_capture->pcm, sw_params)) < 0)
		goto err;
	if ((ret = snd_pcm_sw_params(audio_capture->pcm, sw_params)))
		goto err;

	return 0;
err:
	return -ret;
}

void audio_capture_async_callback(snd_async_handler_t *async_handler)
{
	/*
	 Async handler is called from signal handler so mixing this
	 with mutexes seems to be a retared idea.

	 http://www.kaourantin.net/2006/08/pthreads-and-signals.html
	 */
	struct audio_capture_private_s *audio_capture =
		snd_async_handler_get_callback_private(async_handler);
	sem_post(&audio_capture->capture);
}

void *audio_capture_thread(void *argptr)
{
	struct audio_capture_private_s *audio_capture = argptr;
	snd_pcm_sframes_t avail, read;
	glc_utime_t time, delay_usec = 0;
	glc_audio_header_t hdr;
	glc_message_header_t msg_hdr;
	ps_packet_t packet;
	int ret;
	char *dma;

	ps_packet_init(&packet, audio_capture->to);
	msg_hdr.type = GLC_MESSAGE_AUDIO;

	while (!sem_wait(&audio_capture->capture)) {
		if (audio_capture->stop_capture)
			break;

		avail = 0;
		if ((ret = snd_pcm_delay(audio_capture->pcm, &avail)) < 0)
			audio_capture_xrun(audio_capture, ret);

		while (avail >= audio_capture->period_size) {
			avail = snd_pcm_avail_update(audio_capture->pcm);
			if (avail < audio_capture->period_size)
				continue;

			time = util_time(audio_capture->glc);
			delay_usec = avail * audio_capture->rate_usec;

			if (delay_usec < time)
				time -= delay_usec;
			hdr.timestamp = time;
			hdr.size = audio_capture->period_size_in_bytes;

			ps_packet_open(&packet, PS_PACKET_WRITE);
			ps_packet_write(&packet, &msg_hdr, GLC_MESSAGE_HEADER_SIZE);
			ps_packet_write(&packet, &hdr, GLC_AUDIO_HEADER_SIZE);
			ps_packet_dma(&packet, (void *) &dma, hdr.size, PS_ACCEPT_FAKE_DMA);

			if ((read = snd_pcm_readi(audio_capture->pcm, dma, audio_capture->period_size)) < 0)
				read = -audio_capture_xrun(audio_capture, read);

			if (read != audio_capture->period_size)
				util_log(audio_capture->glc, GLC_WARNING, "audio_capture",
					 "read %ld, expected %ld", read * audio_capture->bytes_per_frame,
					 audio_capture->period_size_in_bytes);
			else if (read < 0)
				util_log(audio_capture->glc, GLC_ERROR, "audio_capture",
					 "xrun recovery failed: %s", snd_strerror(read));

			if (read != audio_capture->period_size) {
				if (ps_packet_cancel(&packet))
					fprintf(stderr, "audio: cancel failed\n");
			} else
				ps_packet_close(&packet);

			if ((ret = snd_pcm_delay(audio_capture->pcm, &avail)) < 0) {
				audio_capture_xrun(audio_capture, ret);
				break;
			}
		}
	}

	ps_packet_destroy(&packet);
	return NULL;
}

int audio_capture_xrun(struct audio_capture_private_s *audio_capture, int err)
{
	util_log(audio_capture->glc, GLC_DEBUG, "audio_capture", "xrun");

	if (err == -EPIPE) {
		if ((err = snd_pcm_prepare(audio_capture->pcm)) < 0)
			return -err;
		if ((err = snd_pcm_start(audio_capture->pcm)) < 0)
			return -err;
		return 0;
	} else if (err == -ESTRPIPE) {
		while ((err = snd_pcm_resume(audio_capture->pcm)) == -EAGAIN)
			sched_yield();
		if (err < 0) {
			if ((err = snd_pcm_prepare(audio_capture->pcm)) < 0)
				return -err;
			if ((err = snd_pcm_start(audio_capture->pcm)) < 0)
				return -err;
			return 0;
		}
	}

	return -err;
}

/**  \} */
/**  \} */
