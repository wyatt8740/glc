/**
 * \file glc/play/alsa_play.h
 * \brief audio playback
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup play
 *  \{
 * \defgroup alsa_play audio playback
 *  \{
 */

#ifndef _AUDIO_PLAY_H
#define _AUDIO_PLAY_H

#include <packetstream.h>
#include <glc/common/glc.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief alsa_play object
 */
typedef struct alsa_play_s* alsa_play_t;

/**
 * \brief initialize alsa_play object
 * \param alsa_play alsa_play object
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
__PUBLIC int alsa_play_init(alsa_play_t *alsa_play, glc_t *glc);

/**
 * \brief destroy alsa_play object
 * \param alsa_play alsa_play object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int alsa_play_destroy(alsa_play_t alsa_play);

/**
 * \brief set audio stream number
 *
 * Default audio stream is 1.
 * \param alsa_play alsa_play object
 * \param audio audio stream number
 * \return 0 on success otherwise an error code
 */
__PUBLIC int alsa_play_set_stream_number(alsa_play_t alsa_play, glc_audio_i audio);

/**
 * \brief set ALSA playback device
 *
 * Default ALSA playback device is "default".
 * \param alsa_play alsa_play object
 * \param device ALSA playback device
 * \return 0 on success otherwise an error code
 */
__PUBLIC int alsa_play_set_alsa_playback_device(alsa_play_t alsa_play,
						 const char *device);

/**
 * \brief start alsa_play process
 *
 * alsa_play plays audio data from selected audio stream.
 * \param alsa_play alsa_play object
 * \param from source buffer
 * \return 0 on success otherwise an error code
 */
__PUBLIC int alsa_play_process_start(alsa_play_t alsa_play, ps_buffer_t *from);

/**
 * \brief block until process has finished
 * \param alsa_play alsa_play object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int alsa_play_process_wait(alsa_play_t alsa_play);

#ifdef __cplusplus
}
#endif

#endif

/**  \} */
/**  \} */
