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

struct audio_play_private_s {
	glc_t *glc;
	glc_thread_t thread;
	sem_t *finished;

	glc_audio_i audio_i;
	snd_pcm_t *pcm;
	const char *device;
	
	unsigned int channels;
	unsigned int rate;
	glc_flags_t flags;

	int fmt;

	void **bufs;
};

int audio_play_read_callback(glc_thread_state_t *state);
void audio_play_finish_callback(void *priv, int err);

int audio_play_hw(struct audio_play_private_s *audio_play, glc_audio_format_message_t *fmt_msg);
int audio_play_play(struct audio_play_private_s *audio_play, glc_audio_header_t *audio_msg, char *data);

snd_pcm_format_t glc_fmt_to_pcm_fmt(glc_flags_t flags);

int audio_play_xrun(struct audio_play_private_s *audio_play, int err);

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

int audio_play_init(glc_t *glc, ps_buffer_t *from, glc_audio_i audio, sem_t *finished)
{
	struct audio_play_private_s *audio_play = (struct audio_play_private_s *) malloc(sizeof(struct audio_play_private_s));
	memset(audio_play, 0, sizeof(struct audio_play_private_s));

	audio_play->glc = glc;
	audio_play->device = "default"; /* TODO configurable */
	audio_play->audio_i = audio;
	audio_play->finished = finished;

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

	if (err)
		util_log(audio_play->glc, GLC_ERROR, "audio", "%s (%d)", strerror(err), err);
	
	if (audio_play->pcm)
		snd_pcm_close(audio_play->pcm);

	if (audio_play->bufs)
		free(audio_play->bufs);

	sem_post(audio_play->finished);
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

int audio_play_hw(struct audio_play_private_s *audio_play, glc_audio_format_message_t *fmt_msg)
{
	snd_pcm_hw_params_t *hw_params;
	snd_pcm_access_t access;
	snd_pcm_uframes_t max_buffer_size;
	unsigned int min_periods;
	int dir, ret = 0;

	if (fmt_msg->audio != audio_play->audio_i)
		return 0;

	audio_play->flags = fmt_msg->flags;
	audio_play->rate = fmt_msg->rate;
	audio_play->channels = fmt_msg->channels;

	if (audio_play->pcm) /* re-open */
		snd_pcm_close(audio_play->pcm);

	if (audio_play->flags & GLC_AUDIO_INTERLEAVED)
		access = SND_PCM_ACCESS_RW_INTERLEAVED;
	else
		access = SND_PCM_ACCESS_RW_NONINTERLEAVED;

	if ((ret = snd_pcm_open(&audio_play->pcm, audio_play->device, SND_PCM_STREAM_PLAYBACK, 0)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_malloc(&hw_params)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_any(audio_play->pcm, hw_params)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_set_access(audio_play->pcm, hw_params, access)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_set_format(audio_play->pcm, hw_params, glc_fmt_to_pcm_fmt(audio_play->flags))) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_set_channels(audio_play->pcm, hw_params, audio_play->channels)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_set_rate(audio_play->pcm, hw_params, audio_play->rate, 0)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_get_buffer_size_max(hw_params, &max_buffer_size)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_set_buffer_size(audio_play->pcm, hw_params, max_buffer_size)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_get_periods_min(hw_params, &min_periods, &dir)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_set_periods(audio_play->pcm, hw_params, min_periods < 2 ? 2 : min_periods, dir)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params(audio_play->pcm, hw_params)) < 0)
		goto err;

	audio_play->bufs = (void **) malloc(sizeof(void *) * audio_play->channels);

	snd_pcm_hw_params_free(hw_params);
	return 0;
err:
	if (hw_params)
		snd_pcm_hw_params_free(hw_params);
	return -ret;
}

int audio_play_play(struct audio_play_private_s *audio_play, glc_audio_header_t *audio_hdr, char *data)
{
	snd_pcm_uframes_t frames, rem;
	snd_pcm_sframes_t ret = 0;
	unsigned int c;

	if (audio_hdr->audio != audio_play->audio_i)
		return 0;

	if (!audio_play->pcm) {
		util_log(audio_play->glc, GLC_ERROR, "audio", "broken stream %d", audio_play->audio_i);
		return EINVAL;
	}
	
	frames = snd_pcm_bytes_to_frames(audio_play->pcm, audio_hdr->size);
	glc_utime_t time = util_timestamp(audio_play->glc);
	glc_utime_t duration = (1000000 * frames) / audio_play->rate;
	
	if (time + audio_play->glc->silence_threshold + duration < audio_hdr->timestamp)
		usleep(audio_hdr->timestamp - time - duration);
	else if (time > audio_hdr->timestamp)
		return 0;

	rem = frames;

	while (rem > 0) {
		/* alsa is horrible... */
		snd_pcm_wait(audio_play->pcm, duration);
		
		if (audio_play->flags & GLC_AUDIO_INTERLEAVED)
			ret = snd_pcm_writei(audio_play->pcm,
					    &data[snd_pcm_frames_to_bytes(audio_play->pcm, frames - rem)],
					    rem);
		else {
			for (c = 0; c < audio_play->channels; c++)
				audio_play->bufs[c] =
					&data[snd_pcm_samples_to_bytes(audio_play->pcm, frames)
					      * c + snd_pcm_samples_to_bytes(audio_play->pcm, frames - rem)];
			ret = snd_pcm_writen(audio_play->pcm, audio_play->bufs, rem);
		}

		if (ret == 0)
			break;

		if ((ret == -EBUSY) | (ret == -EAGAIN))
			break;
		else if (ret < 0) {
			if ((ret = audio_play_xrun(audio_play, ret))) {
				util_log(audio_play->glc, GLC_ERROR, "audio",
					 "xrun recovery failed: %s", snd_strerror(-ret));
				return ret;
			}
		} else
			rem -= ret;
	}

	return 0;
}

int audio_play_xrun(struct audio_play_private_s *audio_play, int err)
{
	util_log(audio_play->glc, GLC_WARNING, "audio", "xrun");
	if (err == -EPIPE) {
		if ((err = snd_pcm_prepare(audio_play->pcm)) < 0)
			return -err;
		return 0;
	} else if (err == -ESTRPIPE) {
		while ((err = snd_pcm_resume(audio_play->pcm)) == -EAGAIN)
			sched_yield();
		if (err < 0) {
			if ((err = snd_pcm_prepare(audio_play->pcm)) < 0)
				return -err;
			return 0;
		}
	}
	return -err;
}

/**  \} */
/**  \} */
