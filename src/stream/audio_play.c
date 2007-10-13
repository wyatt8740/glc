/**
 * \file src/stream/audio_play.c
 * \brief audio playback
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */


/* audio_play.c -- audio playback
 * Copyright (C) 2007 Pyry Haulos
 * For conditions of distribution and use, see copyright notice in glc.h
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <packetstream.h>
#include <alsa/asoundlib.h>
#include <errno.h>
#include <sched.h>

#include "../common/glc.h"
#include "../common/util.h"
#include "../common/thread.h"
#include "audio_play.h"

/**
 * \addtogroup stream
 *  \{
 */

/**
 * \defgroup audio_play audio playback
 *  \{
 */

struct audio_play_stream_s {
	glc_audio_i audio_i;
	snd_pcm_t *pcm;
	
	unsigned int channels;
	unsigned int rate;
	glc_flags_t flags;

	int fmt;

	void **bufs;

	struct audio_play_stream_s *next;
};

struct audio_play_private_s {
	glc_t *glc;
	glc_thread_t thread;

	const char *device;
	unsigned int silence_threshold;
	
	struct audio_play_stream_s *stream;
};

int audio_play_read_callback(glc_thread_state_t *state);
void audio_play_finish_callback(void *priv, int err);

int audio_play_hw(struct audio_play_private_s *audio_play, glc_audio_format_message_t *fmt_msg);
int audio_play_play(struct audio_play_private_s *audio_play, glc_audio_header_t *audio_msg, char *data);
int audio_play_get_stream(struct audio_play_private_s *audio_play, glc_audio_i audio_i, struct audio_play_stream_s **stream);

snd_pcm_format_t glc_fmt_to_pcm_fmt(glc_flags_t flags);

int audio_play_xrun(struct audio_play_stream_s *stream, int err);

snd_pcm_format_t glc_fmt_to_pcm_fmt(glc_flags_t flags)
{
	if (flags & GLC_AUDIO_S16_LE)
		return SND_PCM_FORMAT_S16_LE;
	else if (flags & GLC_AUDIO_S24_LE)
		return SND_PCM_FORMAT_S24_LE;
	else if (flags & GLC_AUDIO_S32_LE)
		return SND_PCM_FORMAT_S32_LE;
	return 0;
}

int audio_play_init(glc_t *glc, ps_buffer_t *from)
{
	struct audio_play_private_s *audio_play = (struct audio_play_private_s *) malloc(sizeof(struct audio_play_private_s));
	memset(audio_play, 0, sizeof(struct audio_play_private_s));

	audio_play->glc = glc;
	audio_play->device = "default";
	audio_play->silence_threshold = 100000;

	audio_play->thread.flags = GLC_THREAD_READ;
	audio_play->thread.ptr = audio_play;
	audio_play->thread.read_callback = &audio_play_read_callback;
	audio_play->thread.finish_callback = &audio_play_finish_callback;
	audio_play->thread.threads = 1;

	return glc_thread_create(glc, &audio_play->thread, from, NULL);
}

void audio_play_finish_callback(void *priv, int err)
{
	struct audio_play_private_s *audio_play = (struct audio_play_private_s *) priv;
	struct audio_play_stream_s *del;

	if (err)
		fprintf(stderr, "audio failed: %s (%d)\n", strerror(err), err);
	
	while (audio_play->stream != NULL) {
		del = audio_play->stream;
		audio_play->stream = audio_play->stream->next;

		if (del->pcm)
			snd_pcm_close(del->pcm);

		if (del->bufs)
			free(del->bufs);
		
		free(del);
	}

	sem_post(&audio_play->glc->signal[GLC_SIGNAL_AUDIO_FINISHED]);
	free(audio_play);
}

int audio_play_read_callback(glc_thread_state_t *state)
{
	struct audio_play_private_s *audio_play = (struct audio_play_private_s *) state->ptr;

	if (state->header.type == GLC_MESSAGE_AUDIO_FORMAT)
		return audio_play_hw(audio_play, (glc_audio_format_message_t *) state->read_data);
	else if (state->header.type == GLC_MESSAGE_AUDIO)
		return audio_play_play(audio_play, (glc_audio_header_t *) state->read_data, &state->read_data[GLC_AUDIO_HEADER_SIZE]);
	
	return 0;
}

int audio_play_get_stream(struct audio_play_private_s *audio_play, glc_audio_i audio_i, struct audio_play_stream_s **stream)
{
	struct audio_play_stream_s *find = audio_play->stream;

	while (find != NULL) {
		if (find->audio_i == audio_i)
			break;
		find = find->next;
	}

	if (find == NULL) {
		find = (struct audio_play_stream_s *) malloc(sizeof(struct audio_play_stream_s));
		memset(find, 0, sizeof(struct audio_play_stream_s));
		find->audio_i = audio_i;

		find->next = audio_play->stream;
		audio_play->stream = find;
	}

	*stream = find;
	return 0;
}

int audio_play_hw(struct audio_play_private_s *audio_play, glc_audio_format_message_t *fmt_msg)
{
	struct audio_play_stream_s *stream;
	snd_pcm_hw_params_t *hw_params;
	snd_pcm_access_t access;
	snd_pcm_uframes_t max_buffer_size;
	unsigned int min_periods;
	int dir, ret = 0;

	audio_play_get_stream(audio_play, fmt_msg->audio, &stream);

	stream->flags = fmt_msg->flags;
	stream->rate = fmt_msg->rate;
	stream->channels = fmt_msg->channels;

	if (stream->pcm) /* re-open */
		snd_pcm_close(stream->pcm);

	if (stream->flags & GLC_AUDIO_INTERLEAVED)
		access = SND_PCM_ACCESS_RW_INTERLEAVED;
	else
		access = SND_PCM_ACCESS_RW_NONINTERLEAVED;

	/* initialize device, SND_PCM_NONBLOCK? */
	/* TODO multiple streams */
	if ((ret = snd_pcm_open(&stream->pcm, audio_play->device, SND_PCM_STREAM_PLAYBACK, 0)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_malloc(&hw_params)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_any(stream->pcm, hw_params)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_set_access(stream->pcm, hw_params, access)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_set_format(stream->pcm, hw_params, glc_fmt_to_pcm_fmt(stream->flags))) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_set_channels(stream->pcm, hw_params, stream->channels)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_set_rate(stream->pcm, hw_params, stream->rate, 0)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_get_buffer_size_max(hw_params, &max_buffer_size)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_set_buffer_size(stream->pcm, hw_params, max_buffer_size)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_get_periods_min(hw_params, &min_periods, &dir)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_set_periods(stream->pcm, hw_params, min_periods < 2 ? 2 : min_periods, dir)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params(stream->pcm, hw_params)) < 0)
		goto err;

	stream->bufs = (void **) malloc(sizeof(void *) * stream->channels);

	snd_pcm_hw_params_free(hw_params);
	return 0;
err:
	if (hw_params)
		snd_pcm_hw_params_free(hw_params);
	return -ret;
}

int audio_play_play(struct audio_play_private_s *audio_play, glc_audio_header_t *audio_hdr, char *data)
{
	struct audio_play_stream_s *stream;
	snd_pcm_uframes_t frames, rem;
	snd_pcm_sframes_t ret = 0;
	unsigned int c;

	audio_play_get_stream(audio_play, audio_hdr->audio, &stream);

	if (!stream->pcm) {
		fprintf(stderr, "audio: broken stream %d\n", stream->audio_i);
		return EINVAL;
	}
	
	frames = snd_pcm_bytes_to_frames(stream->pcm, audio_hdr->size);
	glc_utime_t time = util_timestamp(audio_play->glc);
	glc_utime_t duration = (1000000 * frames) / stream->rate;
	
	if (time + audio_play->glc->silence_threshold + duration < audio_hdr->timestamp)
		usleep(audio_hdr->timestamp - time);
	else if (time > audio_hdr->timestamp + audio_play->glc->silence_threshold)
		return 0;

	rem = frames;

	while (rem > 0) {
		/* alsa is horrible... */
		snd_pcm_wait(stream->pcm, duration);
		
		if (stream->flags & GLC_AUDIO_INTERLEAVED)
			ret = snd_pcm_writei(stream->pcm, &data[snd_pcm_frames_to_bytes(stream->pcm, frames - rem)], rem);
		else {
			for (c = 0; c < stream->channels; c++)
				stream->bufs[c] = &data[snd_pcm_samples_to_bytes(stream->pcm, frames) * c
				                        + snd_pcm_samples_to_bytes(stream->pcm, frames - rem)];
			ret = snd_pcm_writen(stream->pcm, stream->bufs, rem);
		}

		if (ret == 0)
			break;

		if ((ret == -EBUSY) | (ret == -EAGAIN))
			break;
		else if (ret < 0) {
			if ((ret = audio_play_xrun(stream, ret))) {
				fprintf(stderr, "audio: xrun recovery failed: %s\n", snd_strerror(-ret));
				return ret;
			}
		} else
			rem -= ret;
	}

	return 0;
}

int audio_play_xrun(struct audio_play_stream_s *stream, int err)
{
	/* fprintf(stderr, "audio: xrun\n"); */
	if (err == -EPIPE) {
		if ((err = snd_pcm_prepare(stream->pcm)) < 0)
			return -err;
		return 0;
	} else if (err == -ESTRPIPE) {
		while ((err = snd_pcm_resume(stream->pcm)) == -EAGAIN)
			sched_yield();
		if (err < 0) {
			if ((err = snd_pcm_prepare(stream->pcm)) < 0)
				return -err;
			return 0;
		}
	}
	return -err;
}


/**  \} */
/**  \} */
