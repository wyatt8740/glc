/**
 * \file src/stream/audio.h
 * \brief audio capture/playback
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* audio.h -- audio capture/playback
 * Copyright (C) 2007 Pyry Haulos
 * For conditions of distribution and use, see copyright notice in glc.h
 */

#ifndef _AUDIO_H
#define _AUDIO_H

#include <packetstream.h>
#include <alsa/asoundlib.h>
#include "../common/glc.h"

/**
 * \addtogroup audio
 *  \{
 */

void *audio_capture_init(glc_t *glc, ps_buffer_t *to);
int audio_capture_close(void *audiopriv);

int audio_capture_alsa_i(void *audiopriv, snd_pcm_t *pcm, const void *buffer, snd_pcm_uframes_t size);
int audio_capture_alsa_n(void *audiopriv, snd_pcm_t *pcm, void **bufs, snd_pcm_uframes_t size);

int audio_capture_alsa_mmap_begin(void *audiopriv, snd_pcm_t *pcm, const snd_pcm_channel_area_t *areas);
int audio_capture_alsa_mmap_commit(void *audiopriv, snd_pcm_t *pcm, snd_pcm_uframes_t offset, snd_pcm_uframes_t frames);

int audio_playback_init(glc_t *glc, ps_buffer_t *from);

#endif

/**  \} */
