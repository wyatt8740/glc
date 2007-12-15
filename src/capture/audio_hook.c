/**
 * \file src/capture/audio_hook.c
 * \brief audio capture hooks
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup capture
 *  \{
 * \defgroup audio_hook audio capture hooks
 *  \{
 */

/**
 * \note this has some threading bugs, but async alsa uses signals,
 *       so some tradeoffs are required
 */

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
#include "audio_hook.h"

struct audio_hook_stream_s {
	glc_audio_i audio_i;
	snd_pcm_t *pcm;
	int mode;
	const snd_pcm_channel_area_t *mmap_areas;
	snd_pcm_uframes_t frames, offset;

	unsigned int channels;
	unsigned int rate;
	glc_flags_t flags;
	int complex;

	int fmt, initialized;

	ps_packet_t packet;

	/* thread-related */
	pthread_t capture_thread;
	sem_t capture_finished;
	int capture_running;

	/* for communicating with capture thread */
	sem_t capture_empty, capture_full;

	/* for locking access */
	pthread_mutex_t write_mutex;
	pthread_spinlock_t write_spinlock;

	/* for busy waiting */
	int capture_ready;

	char *capture_data;
	size_t capture_size, capture_data_size;
	glc_utime_t capture_time;

	struct audio_hook_stream_s *next;
};

struct audio_hook_private_s {
	glc_t *glc;
	ps_buffer_t *to;

	int started;

	struct audio_hook_stream_s *stream;
};

int audio_hook_get_stream_alsa(struct audio_hook_private_s *audio_hook, snd_pcm_t *pcm, struct audio_hook_stream_s **stream);
int audio_hook_stream_init(struct audio_hook_private_s *audio_hook, struct audio_hook_stream_s *stream);
void *audio_hook_alsa_mmap_pos(const snd_pcm_channel_area_t *area, snd_pcm_uframes_t offset);
int audio_hook_complex_to_interleaved(struct audio_hook_stream_s *stream, const snd_pcm_channel_area_t *areas, snd_pcm_uframes_t offset, snd_pcm_uframes_t frames, char *to);

int audio_hook_wait_for_thread(struct audio_hook_private_s *audio_hook, struct audio_hook_stream_s *stream);
int audio_hook_lock_write(struct audio_hook_private_s *audio_hook, struct audio_hook_stream_s *stream);
int audio_hook_unlock_write(struct audio_hook_private_s *audio_hook, struct audio_hook_stream_s *stream);
int audio_hook_set_data_size(struct audio_hook_stream_s *stream, size_t size);
void *audio_hook_thread(void *argptr);

glc_flags_t pcm_fmt_to_glc_fmt(snd_pcm_format_t pcm_fmt);

void *audio_hook_init(glc_t *glc)
{
	struct audio_hook_private_s *audio_hook = malloc(sizeof(struct audio_hook_private_s));
	memset(audio_hook, 0, sizeof(struct audio_hook_private_s));

	audio_hook->glc = glc;

	return audio_hook;
}

int audio_hook_start(void *audiopriv, ps_buffer_t *to)
{
	struct audio_hook_private_s *audio_hook = audiopriv;
	struct audio_hook_stream_s *stream = audio_hook->stream;

	audio_hook->to = to;

	/* initialize all pending streams */
	while (stream != NULL) {
		audio_hook_stream_init(audio_hook, stream);
		stream = stream->next;
	}

	audio_hook->started = 1;

	return 0;
}

int audio_hook_close(void *audiopriv)
{
	struct audio_hook_private_s *audio_hook = audiopriv;
	struct audio_hook_stream_s *del;

	if (audio_hook == NULL)
		return EINVAL;

	while (audio_hook->stream != NULL) {
		del = audio_hook->stream;
		audio_hook->stream = audio_hook->stream->next;

		if (del->capture_running) {
			del->capture_running = 0;

			/* tell thread to quit */
			sem_post(&del->capture_full);

			sem_wait(&del->capture_finished);
			sem_destroy(&del->capture_finished);

			sem_destroy(&del->capture_full);
			sem_destroy(&del->capture_empty);

			pthread_mutex_destroy(&del->write_mutex);
			pthread_spin_destroy(&del->write_spinlock);
		}

		if (del->capture_data)
			free(del->capture_data);
		if (del->initialized)
			ps_packet_destroy(&del->packet);
		free(del);
	}

	free(audio_hook);
	return 0;
}

glc_flags_t pcm_fmt_to_glc_fmt(snd_pcm_format_t pcm_fmt)
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

int audio_hook_get_stream_alsa(struct audio_hook_private_s *audio_hook, snd_pcm_t *pcm, struct audio_hook_stream_s **stream)
{
	struct audio_hook_stream_s *find = audio_hook->stream;

	while (find != NULL) {
		if (find->pcm == pcm)
			break;
		find = find->next;
	}

	if (find == NULL) {
		find = (struct audio_hook_stream_s *) malloc(sizeof(struct audio_hook_stream_s));
		memset(find, 0, sizeof(struct audio_hook_stream_s));
		find->pcm = pcm;

		find->audio_i = util_audio_stream_id(audio_hook->glc);
		sem_init(&find->capture_finished, 0, 0);

		sem_init(&find->capture_full, 0, 0);
		sem_init(&find->capture_empty, 0, 1);

		pthread_mutex_init(&find->write_mutex, NULL);
		pthread_spin_init(&find->write_spinlock, 0);

		find->next = audio_hook->stream;
		audio_hook->stream = find;
	}

	*stream = find;
	return 0;
}

void *audio_hook_thread(void *argptr)
{
	struct audio_hook_stream_s *stream = (struct audio_hook_stream_s *) argptr;
	glc_audio_header_t hdr;
	glc_message_header_t msg_hdr;

	msg_hdr.type = GLC_MESSAGE_AUDIO;
	hdr.audio = stream->audio_i;

	stream->capture_ready = 1;
	while (1) {
		sem_wait(&stream->capture_full);
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

		sem_post(&stream->capture_empty);
		stream->capture_ready = 1;
	}

	sem_post(&stream->capture_finished);
	return NULL;
}

int audio_hook_wait_for_thread(struct audio_hook_private_s *audio_hook, struct audio_hook_stream_s *stream)
{
	if (stream->mode & SND_PCM_ASYNC) {
		/**
		* \note this is ugly, but snd_pcm_...() functions can be called from
		*       signal handler (f.ex. async mode)
		*/
		while (!stream->capture_ready) {
			if (audio_hook->glc->flags & GLC_AUDIO_ALLOW_SKIP)
				goto busy;
			sched_yield();
		}
	} else
		sem_wait(&stream->capture_empty);

	return 0;
busy:
	util_log(audio_hook->glc, GLC_WARNING, "audio_hook",
		 "dropped audio data, capture thread not ready");
	return EBUSY;
}

int audio_hook_lock_write(struct audio_hook_private_s *audio_hook, struct audio_hook_stream_s *stream)
{
	int ret = 0;
	if (stream->mode & SND_PCM_ASYNC)
		ret = pthread_spin_lock(&stream->write_spinlock);
	else
		ret = pthread_mutex_lock(&stream->write_mutex);
	return ret;
}

int audio_hook_unlock_write(struct audio_hook_private_s *audio_hook, struct audio_hook_stream_s *stream)
{
	int ret = 0;
	if (stream->mode & SND_PCM_ASYNC)
		ret = pthread_spin_unlock(&stream->write_spinlock);
	else
		ret = pthread_mutex_unlock(&stream->write_mutex);
	return ret;
}

int audio_hook_set_data_size(struct audio_hook_stream_s *stream, size_t size)
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


int audio_hook_alsa_open(void *audiopriv, snd_pcm_t *pcm, const char *name,
			 snd_pcm_stream_t pcm_stream, int mode)
{
	struct audio_hook_private_s *audio_hook = audiopriv;
	struct audio_hook_stream_s *stream;

	audio_hook_get_stream_alsa(audio_hook, pcm, &stream);

	stream->mode = mode;

	util_log(audio_hook->glc, GLC_INFORMATION, "audio_hook",
		 "stream %d: opened device \"%s\"",
		 stream->audio_i, name);
	util_log(audio_hook->glc, GLC_DEBUG, "audio_hook",
		 "stream %d: mode is 0x%02x (async=%s, nonblock=%s)",
		 stream->audio_i, mode,
		 mode & SND_PCM_ASYNC ? "yes" : "no",
		 mode & SND_PCM_NONBLOCK ? "yes" : "no");

	return 0;
}

int audio_hook_alsa_i(void *audiopriv, snd_pcm_t *pcm, const void *buffer, snd_pcm_uframes_t size)
{
	struct audio_hook_private_s *audio_hook = (struct audio_hook_private_s *) audiopriv;
	struct audio_hook_stream_s *stream;
	int ret = 0;

	audio_hook_get_stream_alsa(audio_hook, pcm, &stream);

	if (!stream->initialized) {
		ret = EINVAL;
		goto unlock;
	}

	if ((ret = audio_hook_lock_write(audio_hook, stream)))
		return ret;

	if ((ret = audio_hook_wait_for_thread(audio_hook, stream)))
		goto unlock;

	if ((ret = audio_hook_set_data_size(stream, snd_pcm_frames_to_bytes(pcm, size))))
		goto unlock;

	stream->capture_time = util_time(audio_hook->glc);
	memcpy(stream->capture_data, buffer, stream->capture_size);
	sem_post(&stream->capture_full);

unlock:
	audio_hook_unlock_write(audio_hook, stream);
	return ret;
}

int audio_hook_alsa_n(void *audiopriv, snd_pcm_t *pcm, void **bufs, snd_pcm_uframes_t size)
{
	struct audio_hook_private_s *audio_hook = (struct audio_hook_private_s *) audiopriv;
	struct audio_hook_stream_s *stream;
	int c, ret = 0;

	audio_hook_get_stream_alsa(audio_hook, pcm, &stream);

	if (!stream->initialized) {
		ret = EINVAL;
		goto unlock;
	}

	if ((ret = audio_hook_lock_write(audio_hook, stream)))
		return ret;

	if (stream->flags & GLC_AUDIO_INTERLEAVED) {
		util_log(audio_hook->glc, GLC_ERROR, "audio_hook",
			 "stream format (interleaved) incompatible with snd_pcm_writen()");
		ret = EINVAL;
		goto unlock;
	}

	if ((ret = audio_hook_wait_for_thread(audio_hook, stream)))
		goto unlock;

	if ((ret = audio_hook_set_data_size(stream, snd_pcm_frames_to_bytes(pcm, size))))
		goto unlock;

	stream->capture_time = util_time(audio_hook->glc);
	for (c = 0; c < stream->channels; c++)
		memcpy(&stream->capture_data[c * snd_pcm_samples_to_bytes(pcm, size)], bufs[c],
		       snd_pcm_samples_to_bytes(pcm, size));

	sem_post(&stream->capture_full);

unlock:
	audio_hook_unlock_write(audio_hook, stream);
	return ret;
}

int audio_hook_alsa_mmap_begin(void *audiopriv, snd_pcm_t *pcm,
			       const snd_pcm_channel_area_t *areas,
			       snd_pcm_uframes_t offset, snd_pcm_uframes_t frames)
{
	struct audio_hook_private_s *audio_hook = (struct audio_hook_private_s *) audiopriv;
	struct audio_hook_stream_s *stream;
	int ret;

	audio_hook_get_stream_alsa(audio_hook, pcm, &stream);

	if (!stream->initialized) {
		audio_hook_unlock_write(audio_hook, stream);
		return EINVAL;
	}

	if ((ret = audio_hook_lock_write(audio_hook, stream)))
		return ret;

	stream->mmap_areas = areas;
	stream->frames = frames;
	stream->offset = offset;
	return 0;
}

int audio_hook_alsa_mmap_commit(void *audiopriv, snd_pcm_t *pcm,
				snd_pcm_uframes_t offset, snd_pcm_uframes_t frames)
{
	struct audio_hook_private_s *audio_hook = (struct audio_hook_private_s *) audiopriv;
	struct audio_hook_stream_s *stream;
	unsigned int c;
	int ret = 0;

	audio_hook_get_stream_alsa(audio_hook, pcm, &stream);

	if (stream->channels == 0)
		goto unlock; /* 0 channels :P */

	if (!stream->mmap_areas) {
		/* this might actually happen */
		util_log(audio_hook->glc, GLC_WARNING, "audio_hook",
			 "snd_pcm_mmap_commit() before snd_pcm_mmap_begin()");
		return EINVAL; /* not locked */
	}

	if (offset != stream->offset)
		util_log(audio_hook->glc, GLC_WARNING, "audio_hook",
			 "offset=%lu != stream->offset=%lu", offset, stream->offset);

	if ((ret = audio_hook_wait_for_thread(audio_hook, stream)))
		goto unlock;

	if ((ret = audio_hook_set_data_size(stream, snd_pcm_frames_to_bytes(pcm, frames))))
		goto unlock;

	stream->capture_time = util_time(audio_hook->glc);

	if (stream->flags & GLC_AUDIO_INTERLEAVED) {
		memcpy(stream->capture_data,
		       audio_hook_alsa_mmap_pos(stream->mmap_areas, offset),
		       stream->capture_size);
	} else if (stream->complex) {
		audio_hook_complex_to_interleaved(stream, stream->mmap_areas, offset,
		                                  frames, stream->capture_data);
	} else {
		for (c = 0; c < stream->channels; c++)
			memcpy(&stream->capture_data[c * snd_pcm_samples_to_bytes(stream->pcm, frames)],
			       audio_hook_alsa_mmap_pos(&stream->mmap_areas[c], offset),
			       snd_pcm_samples_to_bytes(stream->pcm, frames));
	}

	sem_post(&stream->capture_full);

unlock:
	audio_hook_unlock_write(audio_hook, stream);
	return ret;
}

void *audio_hook_alsa_mmap_pos(const snd_pcm_channel_area_t *area, snd_pcm_uframes_t offset)
{
	/** \todo FIX: first or step not divisible by 8 */
	void *addr = &((unsigned char *) area->addr)[area->first / 8];
	addr = &((unsigned char *) addr)[offset * (area->step / 8)];
	return addr;
}

int audio_hook_complex_to_interleaved(struct audio_hook_stream_s *stream, const snd_pcm_channel_area_t *areas, snd_pcm_uframes_t offset, snd_pcm_uframes_t frames, char *to)
{
	/** \todo test this... :D */
	/** \note this is quite expensive operation */
	unsigned int c;
	size_t s, off, add, ssize;

	add = snd_pcm_frames_to_bytes(stream->pcm, 1);
	ssize = snd_pcm_samples_to_bytes(stream->pcm, 1);

	for (c = 0; c < stream->channels; c++) {
		off = add * c;
		for (s = 0; s < frames; s++) {
			memcpy(&to[off], audio_hook_alsa_mmap_pos(&areas[c], offset + s), ssize);
			off += add;
		}
	}

	return 0;
}

int audio_hook_alsa_hw_params(void *audiopriv, snd_pcm_t *pcm, snd_pcm_hw_params_t *params)
{
	struct audio_hook_private_s *audio_hook = audiopriv;
	struct audio_hook_stream_s *stream;

	snd_pcm_format_t format;
	snd_pcm_uframes_t period_size;
	snd_pcm_access_t access;
	int dir, ret;

	audio_hook_get_stream_alsa(audio_hook, pcm, &stream);
	if ((ret = audio_hook_lock_write(audio_hook, stream)))
		return ret;

	util_log(audio_hook->glc, GLC_INFORMATION, "audio_hook",
		 "creating/updating configuration for stream %d", stream->audio_i);

	/* extract information */
	if ((ret = snd_pcm_hw_params_get_format(params, &format)) < 0)
		goto err;
	stream->flags = 0; /* zero flags */
	stream->flags |= pcm_fmt_to_glc_fmt(format);
	if (stream->flags & GLC_AUDIO_FORMAT_UNKNOWN) {
		util_log(audio_hook->glc, GLC_ERROR, "audio_hook",
			 "unsupported audio format 0x%02x", format);
		ret = ENOTSUP;
		goto err;
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
		stream->flags |= GLC_AUDIO_INTERLEAVED;
	else if (access == SND_PCM_ACCESS_MMAP_COMPLEX) {
		stream->flags |= GLC_AUDIO_INTERLEAVED; /* convert to interleaved */
		stream->complex = 1; /* do conversion */
	} else {
		util_log(audio_hook->glc, GLC_ERROR, "audio_hook",
			 "unsupported access mode 0x%02x", access);
		ret = ENOTSUP;
		goto err;
	}

	util_log(audio_hook->glc, GLC_DEBUG, "audio_hook",
		 "stream %d: %d channels, rate %d, flags 0x%02x",
		 stream->audio_i, stream->channels, stream->rate, stream->flags);

	stream->fmt = 1;
	if (audio_hook->started) {
		if ((ret = audio_hook_stream_init(audio_hook, stream)))
			goto err;
	}

	audio_hook_unlock_write(audio_hook, stream);
	return 0;

err:
	util_log(audio_hook->glc, GLC_ERROR, "audio_hook",
		 "can't extract hardware configuration: %s (%d)", snd_strerror(ret), ret);

	audio_hook_unlock_write(audio_hook, stream);
	return ret;
}

int audio_hook_stream_init(struct audio_hook_private_s *audio_hook, struct audio_hook_stream_s *stream)
{
	glc_message_header_t msg_hdr;
	glc_audio_format_message_t fmt_msg;

	if (!stream->fmt)
		return EINVAL;

	util_log(audio_hook->glc, GLC_INFORMATION, "audio_hook",
		 "initializing stream %d", stream->audio_i);

	/* init packet */
	if (stream->initialized)
		ps_packet_destroy(&stream->packet);
	ps_packet_init(&stream->packet, audio_hook->to);

	/* prepare audio format message */
	msg_hdr.type = GLC_MESSAGE_AUDIO_FORMAT;
	fmt_msg.audio = stream->audio_i;
	fmt_msg.flags = stream->flags;
	fmt_msg.rate = stream->rate;
	fmt_msg.channels = stream->channels;
	ps_packet_open(&stream->packet, PS_PACKET_WRITE);
	ps_packet_write(&stream->packet, &msg_hdr, GLC_MESSAGE_HEADER_SIZE);
	ps_packet_write(&stream->packet, &fmt_msg, GLC_AUDIO_FORMAT_MESSAGE_SIZE);
	ps_packet_close(&stream->packet);

	if (stream->capture_running) {
		/* kill old thread */
		stream->capture_running = 0;
		sem_post(&stream->capture_full);
		sem_wait(&stream->capture_finished);
	}

	stream->capture_running = 1;
	pthread_create(&stream->capture_thread, NULL, audio_hook_thread, stream);

	stream->initialized = 1;
	return 0;
}

/**  \} */
/**  \} */
