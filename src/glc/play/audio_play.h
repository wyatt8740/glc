/**
 * \file glc/play/audio_play.h
 * \brief audio playback
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup play
 *  \{
 * \defgroup audio_play audio playback
 *  \{
 */

#ifndef _AUDIO_PLAY_H
#define _AUDIO_PLAY_H

#include <packetstream.h>
#include <glc/common/glc.h>

/**
 * \brief audio_play object
 */
typedef struct audio_play_s* audio_play_t;

/**
 * \brief initialize audio_play object
 * \param audio_play audio_play object
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
__PUBLIC int audio_play_init(audio_play_t *audio_play, glc_t *glc);

/**
 * \brief destroy audio_play object
 * \param audio_play audio_play object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int audio_play_destroy(audio_play_t audio_play);

/**
 * \brief set audio stream number
 *
 * Default audio stream is 1.
 * \param audio_play audio_play object
 * \param audio audio stream number
 * \return 0 on success otherwise an error code
 */
__PUBLIC int audio_play_set_stream_number(audio_play_t audio_play, glc_audio_i audio);

/**
 * \brief set ALSA playback device
 *
 * Default ALSA playback device is "default".
 * \param audio_play audio_play object
 * \param device ALSA playback device
 * \return 0 on success otherwise an error code
 */
__PUBLIC int audio_play_set_alsa_playback_device(audio_play_t audio_play,
						 const char *device);

/**
 * \brief start audio_play process
 *
 * audio_play plays audio data from selected audio stream.
 * \param audio_play audio_play object
 * \param from source buffer
 * \return 0 on success otherwise an error code
 */
__PUBLIC int audio_play_process_start(audio_play_t audio_play, ps_buffer_t *from);

/**
 * \brief block until process has finished
 * \param audio_play audio_play object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int audio_play_process_wait(audio_play_t audio_play);

#endif

/**  \} */
/**  \} */
