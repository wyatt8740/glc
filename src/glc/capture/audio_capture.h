/**
 * \file glc/capture/audio_capture.h
 * \brief generic audio capture interface
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup capture
 *  \{
 * \defgroup audio_capture generic audio capture
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
 * \brief audio_capture object
 */
typedef struct audio_capture_s* audio_capture_t;

/**
 * \brief initialize audio_capture
 *
 * Initializes audio_capture and binds it into given glc.
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
 * \brief set data format
 *
 * Currently supported flags are
 *  * GLC_AUDIO_INTERLEAVED	audio data is interleaved
 * 
 * and one of the following
 *  * GLC_AUDIO_S16_LE		16b little-endian samples
 *  * GLC_AUDIO_S24_LE		24b little-endian samples
 *  * GLC_AUDIO_S32_LE		32b little-endian samples
 * \param audio_capture audio_capture object
 * \param format_flags format flags
 * \return 0 on success otherwise an error code
 */
__PUBLIC int audio_capture_set_format(audio_capture_t audio_capture,
				      glc_flags_t format_flags);

/**
 * \brief set rate
 * \param audio_capture audio_capture object
 * \param rate rate in Hz
 * \return 0 on success otherwise an error code
 */
__PUBLIC int audio_capture_set_rate(audio_capture_t audio_capture,
				    u_int32_t rate);

/**
 * \brief set channels
 * \param audio_capture audio_capture object
 * \param channels number of channels
 * \return 0 on success otherwise an error code
 */
__PUBLIC int audio_capture_set_channels(audio_capture_t audio_capture,
					u_int32_t channels);

/**
 * \brief ignore time
 *
 * If glc state time is ignored, audio_capture uses internal time which
 * is incremented by (written frames)/rate seconds each time data is captured.
 * \param audio_capture audio_capture object
 * \param ignore_time 1 means state time is ignored,
 *		      0 enables regular time calculations
 * \return 0 on success otherwise an error code
 */
__PUBLIC int audio_capture_ignore_time(audio_capture_t audio_capture, int ignore_time);

/**
 * \brief start capturing
 *
 * If capturing is not active, all submitted data is discarded.
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

/**
 * \brief submit audio data
 * \param audio_capture audio_capture object
 * \param data audio data
 * \param size data size in bytes (not samples or frames)
 * \return 0 on success otherwise an error code
 */
__PUBLIC int audio_capture_data(audio_capture_t audio_capture,
				void *data, size_t size);

/**
 * \brief samples to bytes
 * \param audio_capture audio_capture object
 * \param samples samples
 * \return size in bytes
 */
__PUBLIC size_t audio_capture_samples_to_bytes(audio_capture_t audio_capture,
					       unsigned int samples);

/**
 * \brief frames to bytes
 * \param audio_capture audio_capture object
 * \param frames frames
 * \return size in bytes
 */
__PUBLIC size_t audio_capture_frames_to_bytes(audio_capture_t audio_capture,
					      unsigned int frames);

#ifdef __cplusplus
}
#endif

#endif

/**  \} */
/**  \} */
