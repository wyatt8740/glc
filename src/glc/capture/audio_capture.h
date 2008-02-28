/**
 * \file glc/capture/audio_capture.h
 * \brief audio capture
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup capture
 *  \{
 * \defgroup audio_capture audio capture
 *  \{
 */

#ifndef _AUDIO_CAPTURE_H
#define _AUDIO_CAPTURE_H

#include <packetstream.h>
#include <glc/common/glc.h>

/**
 * \brief audio_capture object
 */
typedef struct audio_capture_s* audio_capture_t;

/**
 * \brief initialize audio_capture object
 * \param audio_capture audio_capture object
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
__PUBLIC int audio_capture_init(audio_capture_t *audio_capture, glc_t *glc);

/**
 * \brief set target buffer
 * \param audio_capture audio_capture object
 * \param buffer target buffer
 * \return 0 on success otherwise an error code
 */
__PUBLIC int audio_capture_set_buffer(audio_capture_t audio_capture, ps_buffer_t *buffer);

/**
 * \brief set capture device
 *
 * Default ALSA capture device is 'default'.
 * \param audio_capture audio_capture object
 * \param device ALSA device
 * \return 0 on success otherwise an error code
 */
__PUBLIC int audio_capture_set_device(audio_capture_t audio_capture, const char *device);

/**
 * \brief set capture rate
 *
 * Default capture rate is 44100Hz
 * \param audio_capture audio_capture object
 * \param rate rate in Hz
 * \return 0 on success otherwise an error code
 */
__PUBLIC int audio_capture_set_rate(audio_capture_t audio_capture, unsigned int rate);

/**
 * \brief set number of channels
 *
 * Default number of channels is 2
 * \param audio_capture audio_capture object
 * \param channels number of channels
 * \return 0 on success otherwise an error code
 */
__PUBLIC int audio_capture_set_channels(audio_capture_t audio_capture, unsigned int channels);

/**
 * \brief start capturing
 * \param audio_capture audio_capture object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int audio_capture_start(audio_capture_t audio_capture);

/**
 * \brief stop capturing
 * \param audio_capture audio_capture object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int audio_capture_stop(audio_capture_t audio_capture);

/**
 * \brief destroy audio_capture object
 * \param audio_capture audio_capture object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int audio_capture_destroy(audio_capture_t audio_capture);

#endif

/**  \} */
/**  \} */
