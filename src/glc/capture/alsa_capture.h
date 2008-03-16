/**
 * \file glc/capture/alsa_capture.h
 * \brief audio capture
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup capture
 *  \{
 * \defgroup alsa_capture audio capture
 *  \{
 */

#ifndef _AUDIO_CAPTURE_H
#define _AUDIO_CAPTURE_H

#include <packetstream.h>
#include <glc/common/glc.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief alsa_capture object
 */
typedef struct alsa_capture_s* alsa_capture_t;

/**
 * \brief initialize alsa_capture object
 * \param alsa_capture alsa_capture object
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
__PUBLIC int alsa_capture_init(alsa_capture_t *alsa_capture, glc_t *glc);

/**
 * \brief set target buffer
 * \param alsa_capture alsa_capture object
 * \param buffer target buffer
 * \return 0 on success otherwise an error code
 */
__PUBLIC int alsa_capture_set_buffer(alsa_capture_t alsa_capture, ps_buffer_t *buffer);

/**
 * \brief set capture device
 *
 * Default ALSA capture device is 'default'.
 * \param alsa_capture alsa_capture object
 * \param device ALSA device
 * \return 0 on success otherwise an error code
 */
__PUBLIC int alsa_capture_set_device(alsa_capture_t alsa_capture, const char *device);

/**
 * \brief set capture rate
 *
 * Default capture rate is 44100Hz
 * \param alsa_capture alsa_capture object
 * \param rate rate in Hz
 * \return 0 on success otherwise an error code
 */
__PUBLIC int alsa_capture_set_rate(alsa_capture_t alsa_capture, unsigned int rate);

/**
 * \brief set number of channels
 *
 * Default number of channels is 2
 * \param alsa_capture alsa_capture object
 * \param channels number of channels
 * \return 0 on success otherwise an error code
 */
__PUBLIC int alsa_capture_set_channels(alsa_capture_t alsa_capture, unsigned int channels);

/**
 * \brief start capturing
 * \param alsa_capture alsa_capture object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int alsa_capture_start(alsa_capture_t alsa_capture);

/**
 * \brief stop capturing
 * \param alsa_capture alsa_capture object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int alsa_capture_stop(alsa_capture_t alsa_capture);

/**
 * \brief destroy alsa_capture object
 * \param alsa_capture alsa_capture object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int alsa_capture_destroy(alsa_capture_t alsa_capture);

#ifdef __cplusplus
}
#endif

#endif

/**  \} */
/**  \} */
