/**
 * \file src/stream/audio_capture.c
 * \brief audio capture
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */


/* audio_capture.c -- audio capture
 * Copyright (C) 2007 Pyry Haulos
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/* NOTE this has some threading bugs, but async alsa uses signals,
        so some tradeoffs are required */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <packetstream.h>
#include <alsa/asoundlib.h>
#include <pthread.h>
#include <errno.h>
#include <sched.h>

#include "../common/glc.h"
#include "../common/util.h"
#include "audio_capture.h"

/**
 * \addtogroup stream
 *  \{
 */

/**
 * \defgroup audio_capture audio capture
 *  \{
 */

struct audio_capture_stream_s {
	glc_audio_i audio_i;
	snd_pcm_t *pcm;
	const snd_pcm_channel_area_t *mmap_areas;
	
	unsigned int channels;
	unsigned int rate;
	glc_audio_format_t format;
	int interleaved, complex;

	int fmt;

	ps_packet_t packet;

	pthread_t capture_thread;
	sem_t capture, capture_finished;
	int capture_ready, capture_running;
	char *capture_data;
	size_t capture_size, capture_data_size;
	glc_utime_t capture_time;
	
	struct audio_capture_stream_s *next;
};

struct audio_capture_private_s {
	glc_t *glc;
	ps_buffer_t *to;

	glc_audio_i stream_count;
	struct audio_capture_stream_s *stream;
};

int audio_capture_get_stream_alsa(struct audio_capture_private_s *audio_capture, snd_pcm_t *pcm, struct audio_capture_stream_s **stream);
int audio_capture_alsa_fmt(struct audio_capture_private_s *audio_capture, struct audio_capture_stream_s *stream);
void *audio_capture_alsa_mmap_pos(const snd_pcm_channel_area_t *area, snd_pcm_uframes_t offset);
int audio_capture_complex_to_interleaved(struct audio_capture_stream_s *stream, const snd_pcm_channel_area_t *areas, snd_pcm_uframes_t offset, snd_pcm_uframes_t frames, char *to);

int audio_capture_wait_for_thread(struct audio_capture_private_s *audio_capture, struct audio_capture_stream_s *stream);
int audio_capture_set_data_size(struct audio_capture_stream_s *stream, size_t size);
void *audio_capture_thread(void *argptr);

glc_audio_format_t pcm_fmt_to_glc_fmt(snd_pcm_format_t pcm_fmt);

void *audio_capture_init(glc_t *glc, ps_buffer_t *to)
{
	struct audio_capture_private_s *audio_capture = malloc(sizeof(struct audio_capture_private_s));
	memset(audio_capture, 0, sizeof(struct audio_capture_private_s));
	
	audio_capture->glc = glc;
	audio_capture->to = to;

	return audio_capture;
}

int audio_capture_close(void *audiopriv)
{
	struct audio_capture_private_s *audio_capture = (struct audio_capture_private_s *) audiopriv;
	struct audio_capture_stream_s *del;
	if (audio_capture == NULL)
		return EINVAL;
	
	while (audio_capture->stream != NULL) {
		del = audio_capture->stream;
		audio_capture->stream = audio_capture->stream->next;
		
		if (del->capture_running) {
			del->capture_running = 0;
			sem_post(&del->capture);
			sem_wait(&del->capture_finished);
			sem_destroy(&del->capture);
			sem_destroy(&del->capture_finished);
		}
		
		if (del->capture_data)
			free(del->capture_data);
		ps_packet_destroy(&del->packet);
		free(del);
	}

	free(audio_capture);
	return 0;
}

glc_audio_format_t pcm_fmt_to_glc_fmt(snd_pcm_format_t pcm_fmt)
{
	switch (pcm_fmt) {
	case SND_PCM_FORMAT_S16_LE:
		return GLC_AUDIO_FORMAT_S16_LE;
	case SND_PCM_FORMAT_S24_LE:
		return GLC_AUDIO_FORMAT_S24_LE;
	case SND_PCM_FORMAT_S32_LE:
		return GLC_AUDIO_FORMAT_S32_LE;
	default:
		return GLC_AUDIO_FORMAT_UNKNOWN;
	}
}

int audio_capture_get_stream_alsa(struct audio_capture_private_s *audio_capture, snd_pcm_t *pcm, struct audio_capture_stream_s **stream)
{
	struct audio_capture_stream_s *find = audio_capture->stream;

	while (find != NULL) {
		if (find->pcm == pcm)
			break;
		find = find->next;
	}

	if (find == NULL) {
		find = (struct audio_capture_stream_s *) malloc(sizeof(struct audio_capture_stream_s));
		memset(find, 0, sizeof(struct audio_capture_stream_s));
		find->pcm = pcm;

		ps_packet_init(&find->packet, audio_capture->to);
		find->audio_i = ++audio_capture->stream_count;
		sem_init(&find->capture, 0, 0);
		sem_init(&find->capture_finished, 0, 0);

		find->next = audio_capture->stream;
		audio_capture->stream = find;
	}

	*stream = find;
	return 0;
}

void *audio_capture_thread(void *argptr)
{
	struct audio_capture_stream_s *stream = (struct audio_capture_stream_s *) argptr;
	glc_audio_header_t hdr;
	glc_message_header_t msg_hdr;

	msg_hdr.type = GLC_MESSAGE_AUDIO;
	hdr.audio = stream->audio_i;

	while (1) {
		stream->capture_ready = 1;
		sem_wait(&stream->capture);
		stream->capture_ready = 0;
		if (!stream->capture_running)
			break;

		hdr.timestamp = stream->capture_time;
		hdr.size = stream->capture_size;
		
		ps_packet_open(&stream->packet, PS_PACKET_WRITE);
		ps_packet_write(&stream->packet, &msg_hdr, GLC_MESSAGE_HEADER_SIZE);
		ps_packet_write(&stream->packet, &hdr, GLC_AUDIO_HEADER_SIZE);
		ps_packet_write(&stream->packet, stream->capture_data, hdr.size);
		ps_packet_close(&stream->packet);
	}

	sem_post(&stream->capture_finished);
	return NULL;
}

int audio_capture_wait_for_thread(struct audio_capture_private_s *audio_capture, struct audio_capture_stream_s *stream)
{
	/* NOTE this is ugly, but snd_pcm_...() functions can be called from
	        signal handler (f.ex. async mode) */
	while (!stream->capture_ready) {
		if (audio_capture->glc->flags & GLC_AUDIO_ALLOW_SKIP)
			return EBUSY;
		sched_yield();
	}
	return 0;
}

int audio_capture_set_data_size(struct audio_capture_stream_s *stream, size_t size)
{
	stream->capture_size = size;
	if (size <= stream->capture_data_size)
		return 0;
	stream->capture_data_size = size;
	if (stream->capture_data)
		stream->capture_data = (char *) realloc(stream->capture_data, stream->capture_data_size);
	else
		stream->capture_data = (char *) malloc(stream->capture_data_size);
	if (!stream->capture_data)
		return ENOMEM;
	return 0;
}

int audio_capture_alsa_i(void *audiopriv, snd_pcm_t *pcm, const void *buffer, snd_pcm_uframes_t size)
{
	struct audio_capture_private_s *audio_capture = (struct audio_capture_private_s *) audiopriv;
	struct audio_capture_stream_s *stream;
	int ret;

	audio_capture_get_stream_alsa(audio_capture, pcm, &stream);

	if (!stream->fmt) { /* TODO update this? */
		if ((ret = audio_capture_alsa_fmt(audio_capture, stream)))
			return ret;
	}

	if (audio_capture_wait_for_thread(audio_capture, stream))
		return 0;

	if (!stream->capture_ready) {
		/*fprintf(stderr, "audio: capture thread not ready, skipping...\n");*/
		return 0;
	}

	if ((ret = audio_capture_set_data_size(stream, snd_pcm_frames_to_bytes(pcm, size))))
		return ret;

	stream->capture_time = util_timestamp(audio_capture->glc);
	memcpy(stream->capture_data, buffer, stream->capture_size);
	sem_post(&stream->capture);

	return 0;
}

int audio_capture_alsa_n(void *audiopriv, snd_pcm_t *pcm, void **bufs, snd_pcm_uframes_t size)
{
	struct audio_capture_private_s *audio_capture = (struct audio_capture_private_s *) audiopriv;
	struct audio_capture_stream_s *stream;
	int c, ret;

	audio_capture_get_stream_alsa(audio_capture, pcm, &stream);

	if (!stream->fmt) {
		if ((ret = audio_capture_alsa_fmt(audio_capture, stream)))
			return ret;
	}

	if (stream->interleaved) {
		fprintf(stderr, "audio: stream format (interleaved) incompatible with snd_pcm_writen()\n");
		return EINVAL;
	}

	if (audio_capture_wait_for_thread(audio_capture, stream))
		return 0;

	if ((ret = audio_capture_set_data_size(stream, snd_pcm_frames_to_bytes(pcm, size))))
		return ret;
	stream->capture_time = util_timestamp(audio_capture->glc);
	
	for (c = 0; c < stream->channels; c++)
		memcpy(&stream->capture_data[c * snd_pcm_samples_to_bytes(pcm, size)], bufs[c],
		       snd_pcm_samples_to_bytes(pcm, size));
	
	sem_post(&stream->capture);
	return 0;
}

int audio_capture_alsa_mmap_begin(void *audiopriv, snd_pcm_t *pcm, const snd_pcm_channel_area_t *areas)
{
	struct audio_capture_private_s *audio_capture = (struct audio_capture_private_s *) audiopriv;
	struct audio_capture_stream_s *stream;
	int ret;
	
	audio_capture_get_stream_alsa(audio_capture, pcm, &stream);

	if (!stream->fmt) {
		if ((ret = audio_capture_alsa_fmt(audio_capture, stream)))
			return ret;
	}

	stream->mmap_areas = areas;
	return 0;
}

int audio_capture_alsa_mmap_commit(void *audiopriv, snd_pcm_t *pcm, snd_pcm_uframes_t offset, snd_pcm_uframes_t frames)
{
	struct audio_capture_private_s *audio_capture = (struct audio_capture_private_s *) audiopriv;
	struct audio_capture_stream_s *stream;
	unsigned int c;
	int ret;

	audio_capture_get_stream_alsa(audio_capture, pcm, &stream);
	
	if (stream->channels == 0)
		return 0; /* 0 channels :P */

	if (!stream->mmap_areas) {
		fprintf(stderr, "audio: snd_pcm_mmap_commit() before snd_pcm_mmap_begin()\n");
		return EINVAL;
	}
	
	if (audio_capture_wait_for_thread(audio_capture, stream))
		return 0;

	if ((ret = audio_capture_set_data_size(stream, snd_pcm_frames_to_bytes(pcm, frames))))
		return ret;
	stream->capture_time = util_timestamp(audio_capture->glc);
	
	if (stream->interleaved)
		memcpy(stream->capture_data,
		       audio_capture_alsa_mmap_pos(stream->mmap_areas, offset),
		       stream->capture_size);
	else if (stream->complex)
		audio_capture_complex_to_interleaved(stream, stream->mmap_areas, offset,
		                                     frames, stream->capture_data);
	else {
		for (c = 0; c < stream->channels; c++)
			memcpy(&stream->capture_data[c * snd_pcm_samples_to_bytes(stream->pcm, frames)],
			       audio_capture_alsa_mmap_pos(&stream->mmap_areas[c], offset),
			       snd_pcm_samples_to_bytes(stream->pcm, frames));
	}
	
	sem_post(&stream->capture);
	return 0;
}

void *audio_capture_alsa_mmap_pos(const snd_pcm_channel_area_t *area, snd_pcm_uframes_t offset)
{
	/* FIXME first or step not divisible by 8 */
	void *addr = &((unsigned char *) area->addr)[area->first / 8];
	addr = &((unsigned char *) addr)[offset * (area->step / 8)];
	return addr;
}

int audio_capture_complex_to_interleaved(struct audio_capture_stream_s *stream, const snd_pcm_channel_area_t *areas, snd_pcm_uframes_t offset, snd_pcm_uframes_t frames, char *to)
{
	/* TODO test this... :D */
	/* FIXME this is quite expensive operation */
	unsigned int c;
	size_t s, off, add, ssize;
	
	add = snd_pcm_frames_to_bytes(stream->pcm, 1);
	ssize = snd_pcm_samples_to_bytes(stream->pcm, 1);

	for (c = 0; c < stream->channels; c++) {
		off = add * c;
		for (s = 0; s < frames; s++) {
			memcpy(&to[off], audio_capture_alsa_mmap_pos(&areas[c], offset + s), ssize);
			off += add;
		}
	}
	
	return 0;
}

int audio_capture_alsa_fmt(struct audio_capture_private_s *audio_capture, struct audio_capture_stream_s *stream)
{
	snd_pcm_hw_params_t *params;
	snd_pcm_format_t format;
	snd_pcm_uframes_t period_size;
	snd_pcm_access_t access;
	glc_message_header_t msg_hdr;
	glc_audio_format_message_t fmt_msg;
	int dir, ret;

	/* read configuration */
	if ((ret = snd_pcm_hw_params_malloc(&params)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_current(stream->pcm, params)) < 0)
		goto err;

	/* extract information */
	if ((ret = snd_pcm_hw_params_get_format(params, &format)) < 0)
		goto err;
	stream->format = pcm_fmt_to_glc_fmt(format);
	if (stream->format == GLC_AUDIO_FORMAT_UNKNOWN) {
		fprintf(stderr, "audio: unsupported audio format\n");
		return ENOTSUP;
	}
	if ((ret = snd_pcm_hw_params_get_rate(params, &stream->rate, &dir)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_get_channels(params, &stream->channels)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_get_period_size(params, &period_size, NULL)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_get_access(params, &access)) < 0)
		goto err;
	if ((access == SND_PCM_ACCESS_RW_INTERLEAVED) | (access == SND_PCM_ACCESS_MMAP_INTERLEAVED))
		stream->interleaved = 1;
	else if ((access == SND_PCM_ACCESS_RW_NONINTERLEAVED) | (access == SND_PCM_ACCESS_MMAP_NONINTERLEAVED))
		stream->interleaved = 0;
	else if (access == SND_PCM_ACCESS_MMAP_COMPLEX) {
		stream->interleaved = 1; /* convert to interleaved */
		stream->complex = 1; /* do conversion */
	} else {
		fprintf(stderr, "audio: unsupported access mode\n");
		return ENOTSUP;
	}

	/* prepare audio format message */
	msg_hdr.type = GLC_MESSAGE_AUDIO_FORMAT;
	fmt_msg.audio = stream->audio_i;
	fmt_msg.format = stream->format;
	fmt_msg.rate = stream->rate;
	fmt_msg.channels = stream->channels;
	fmt_msg.interleaved = stream->interleaved;
	ps_packet_open(&stream->packet, PS_PACKET_WRITE);
	ps_packet_write(&stream->packet, &msg_hdr, GLC_MESSAGE_HEADER_SIZE);
	ps_packet_write(&stream->packet, &fmt_msg, GLC_AUDIO_FORMAT_MESSAGE_SIZE);
	ps_packet_close(&stream->packet);

	if (stream->capture_running) { /* kill old thread */
		stream->capture_running = 0;
		sem_post(&stream->capture);
		sem_wait(&stream->capture_finished);
	}
	stream->capture_running = 1;
	pthread_create(&stream->capture_thread, NULL, audio_capture_thread, stream);

	stream->fmt = 1;
	snd_pcm_hw_params_free(params);
	return 0;
err:
	if (params)
		snd_pcm_hw_params_free(params);
	fprintf(stderr, "audio: can't extract hardware configuration: %s (%d)\n", snd_strerror(ret), ret);
	return ret;
}

/**  \} */
/**  \} */
