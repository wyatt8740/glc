/**
 * \file src/play/audio_play.h
 * \brief audio playback
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup audio_play
 *  \{
 */

#ifndef _AUDIO_PLAY_H
#define _AUDIO_PLAY_H

#include <packetstream.h>
#include "../common/glc.h"

__PUBLIC int audio_play_init(glc_t *glc, ps_buffer_t *from, glc_audio_i audio, sem_t *finished);

#endif

/**  \} */
