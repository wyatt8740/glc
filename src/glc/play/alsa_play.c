/**
 * \file glc/play/alsa_play.c
 * \brief audio playback
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup alsa_play
 *  \{
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <packetstream.h>
#include <alsa/asoundlib.h>
#include <errno.h>
#include <sched.h>

#include <glc/common/glc.h>
#include <glc/common/core.h>
#include <glc/common/log.h>
#include <glc/common/util.h>
#include <glc/common/state.h>
#include <glc/common/thread.h>

#include "alsa_play.h"

struct alsa_play_s {
	glc_t *glc;
	glc_thread_t thread;
	int running;

	glc_utime_t silence_threshold;

	glc_audio_i audio_i;
	snd_pcm_t *pcm;
	const char *device;

	unsigned int channels;
	unsigned int rate;
	glc_flags_t flags;

	int fmt;

	void **bufs;
};

int alsa_play_read_callback(glc_thread_state_t *state);
void alsa_play_finish_callback(void *priv, int err);

int alsa_play_hw(alsa_play_t alsa_play, glc_audio_format_message_t *fmt_msg);
int alsa_play_play(alsa_play_t alsa_play, glc_audio_header_t *audio_msg, char *data);

snd_pcm_format_t glc_fmt_to_pcm_fmt(glc_flags_t flags);

int alsa_play_xrun(alsa_play_t alsa_play, int err);

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

int alsa_play_init(alsa_play_t *alsa_play, glc_t *glc)
{
	*alsa_play = (alsa_play_t) malloc(sizeof(struct alsa_play_s));
	memset(*alsa_play, 0, sizeof(struct alsa_play_s));

	(*alsa_play)->glc = glc;
	(*alsa_play)->device = "default";
	(*alsa_play)->audio_i = 1;
	(*alsa_play)->silence_threshold = 200000; /** \todo make configurable? */

	(*alsa_play)->thread.flags = GLC_THREAD_READ;
	(*alsa_play)->thread.ptr = *alsa_play;
	(*alsa_play)->thread.read_callback = &alsa_play_read_callback;
	(*alsa_play)->thread.finish_callback = &alsa_play_finish_callback;
	(*alsa_play)->thread.threads = 1;

	return 0;
}

int alsa_play_destroy(alsa_play_t alsa_play)
{
	free(alsa_play);
	return 0;
}

int alsa_play_set_alsa_playback_device(alsa_play_t alsa_play, const char *device)
{
	alsa_play->device = device;
	return 0;
}

int alsa_play_set_stream_number(alsa_play_t alsa_play, glc_audio_i audio)
{
	alsa_play->audio_i = audio;
	return 0;
}

int alsa_play_process_start(alsa_play_t alsa_play, ps_buffer_t *from)
{
	int ret;
	if (alsa_play->running)
		return EAGAIN;

	if ((ret = glc_thread_create(alsa_play->glc, &alsa_play->thread, from, NULL)))
		return ret;
	alsa_play->running = 1;

	return 0;
}

int alsa_play_process_wait(alsa_play_t alsa_play)
{
	if (!alsa_play->running)
		return EAGAIN;

	glc_thread_wait(&alsa_play->thread);
	alsa_play->running = 0;

	return 0;
}

void alsa_play_finish_callback(void *priv, int err)
{
	alsa_play_t alsa_play = (alsa_play_t) priv;

	if (err)
		glc_log(alsa_play->glc, GLC_ERROR, "alsa_play", "%s (%d)",
			 strerror(err), err);

	if (alsa_play->pcm) {
		snd_pcm_close(alsa_play->pcm);
		alsa_play->pcm = NULL;
	}

	if (alsa_play->bufs) {
		free(alsa_play->bufs);
		alsa_play->bufs = NULL;
	}
}

int alsa_play_read_callback(glc_thread_state_t *state)
{
	alsa_play_t alsa_play = (alsa_play_t ) state->ptr;

	if (state->header.type == GLC_MESSAGE_AUDIO_FORMAT)
		return alsa_play_hw(alsa_play, (glc_audio_format_message_t *) state->read_data);
	else if (state->header.type == GLC_MESSAGE_AUDIO)
		return alsa_play_play(alsa_play, (glc_audio_header_t *) state->read_data,
				       &state->read_data[GLC_AUDIO_HEADER_SIZE]);

	return 0;
}

int alsa_play_hw(alsa_play_t alsa_play, glc_audio_format_message_t *fmt_msg)
{
	snd_pcm_hw_params_t *hw_params = NULL;
	snd_pcm_access_t access;
	snd_pcm_uframes_t max_buffer_size;
	unsigned int min_periods;
	int dir, ret = 0;

	if (fmt_msg->audio != alsa_play->audio_i)
		return 0;

	alsa_play->flags = fmt_msg->flags;
	alsa_play->rate = fmt_msg->rate;
	alsa_play->channels = fmt_msg->channels;

	if (alsa_play->pcm) /* re-open */
		snd_pcm_close(alsa_play->pcm);

	if (alsa_play->flags & GLC_AUDIO_INTERLEAVED)
		access = SND_PCM_ACCESS_RW_INTERLEAVED;
	else
		access = SND_PCM_ACCESS_RW_NONINTERLEAVED;

	if ((ret = snd_pcm_open(&alsa_play->pcm, alsa_play->device,
				SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK)) < 0) {
		if (ret != -ENOENT)
			goto err;

		/* omg... */
		glc_log(alsa_play->glc, GLC_WARNING, "alsa_play",
			"pcm %s not found, trying again...", alsa_play->device);
		if ((ret = snd_pcm_open(&alsa_play->pcm, alsa_play->device,
					SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK)) < 0)
			goto err;
	}
	if ((ret = snd_pcm_hw_params_malloc(&hw_params)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_any(alsa_play->pcm, hw_params)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_set_access(alsa_play->pcm,
						hw_params, access)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_set_format(alsa_play->pcm, hw_params,
						glc_fmt_to_pcm_fmt(alsa_play->flags))) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_set_channels(alsa_play->pcm, hw_params,
						  alsa_play->channels)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_set_rate(alsa_play->pcm, hw_params,
					      alsa_play->rate, 0)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_get_buffer_size_max(hw_params,
							 &max_buffer_size)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_set_buffer_size(alsa_play->pcm,
						     hw_params, max_buffer_size)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_get_periods_min(hw_params, &min_periods, &dir)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params_set_periods(alsa_play->pcm, hw_params,
						 min_periods < 2 ? 2 : min_periods, dir)) < 0)
		goto err;
	if ((ret = snd_pcm_hw_params(alsa_play->pcm, hw_params)) < 0)
		goto err;

	alsa_play->bufs = (void **) malloc(sizeof(void *) * alsa_play->channels);

	glc_log(alsa_play->glc, GLC_INFORMATION, "alsa_play",
		"opened pcm %s for playback", alsa_play->device);

	snd_pcm_hw_params_free(hw_params);
	return 0;
err:
	glc_log(alsa_play->glc, GLC_ERROR, "alsa_play",
		"can't initialize pcm %s: %s (%d)",
		alsa_play->device, snd_strerror(ret), ret);
	if (hw_params)
		snd_pcm_hw_params_free(hw_params);
	return -ret;
}

int alsa_play_play(alsa_play_t alsa_play, glc_audio_header_t *audio_hdr, char *data)
{
	snd_pcm_uframes_t frames, rem;
	snd_pcm_sframes_t ret = 0;
	unsigned int c;

	if (audio_hdr->audio != alsa_play->audio_i)
		return 0;

	if (!alsa_play->pcm) {
		glc_log(alsa_play->glc, GLC_ERROR, "alsa_play", "broken stream %d",
			 alsa_play->audio_i);
		return EINVAL;
	}

	frames = snd_pcm_bytes_to_frames(alsa_play->pcm, audio_hdr->size);
	glc_utime_t time = glc_state_time(alsa_play->glc);
	glc_utime_t duration = (1000000 * frames) / alsa_play->rate;

	if (time + alsa_play->silence_threshold + duration < audio_hdr->timestamp)
		usleep(audio_hdr->timestamp - time - duration);
	else if (time > audio_hdr->timestamp) {
		glc_log(alsa_play->glc, GLC_DEBUG, "alsa_play", "dropped packet");
		return 0;
	}

	rem = frames;

	while (rem > 0) {
		/* alsa is horrible... */
		/*snd_pcm_wait(alsa_play->pcm, duration);*/

		if (alsa_play->flags & GLC_AUDIO_INTERLEAVED)
			ret = snd_pcm_writei(alsa_play->pcm,
					    &data[snd_pcm_frames_to_bytes(alsa_play->pcm, frames - rem)],
					    rem);
		else {
			for (c = 0; c < alsa_play->channels; c++)
				alsa_play->bufs[c] =
					&data[snd_pcm_samples_to_bytes(alsa_play->pcm, frames)
					      * c + snd_pcm_samples_to_bytes(alsa_play->pcm, frames - rem)];
			ret = snd_pcm_writen(alsa_play->pcm, alsa_play->bufs, rem);
		}

		if (ret == 0)
			break;

		if ((ret == -EBUSY) | (ret == -EAGAIN))
			break;
		else if (ret < 0) {
			if ((ret = alsa_play_xrun(alsa_play, ret))) {
				glc_log(alsa_play->glc, GLC_ERROR, "alsa_play",
					 "xrun recovery failed: %s", snd_strerror(-ret));
				return ret;
			}
		} else
			rem -= ret;
	}

	return 0;
}

int alsa_play_xrun(alsa_play_t alsa_play, int err)
{
	glc_log(alsa_play->glc, GLC_DEBUG, "alsa_play", "xrun");
	if (err == -EPIPE) {
		if ((err = snd_pcm_prepare(alsa_play->pcm)) < 0)
			return -err;
		return 0;
	} else if (err == -ESTRPIPE) {
		while ((err = snd_pcm_resume(alsa_play->pcm)) == -EAGAIN)
			sched_yield();
		if (err < 0) {
			if ((err = snd_pcm_prepare(alsa_play->pcm)) < 0)
				return -err;
			return 0;
		}
	}
	return -err;
}

/**  \} */
