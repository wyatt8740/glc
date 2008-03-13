/**
 * \file glc/export/wav.h
 * \brief export audio to wav
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup export
 *  \{
 * \defgroup wav export audio to wav
 *  \{
 */

#ifndef _WAV_H
#define _WAV_H

#include <packetstream.h>
#include <glc/common/glc.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief wav object
 */
typedef struct wav_s* wav_t;

/**
 * \brief initialize wav object
 * \param wav wav object
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
__PUBLIC int wav_init(wav_t *wav, glc_t *glc);

/**
 * \brief destroy wav object
 * \param wav wav object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int wav_destroy(wav_t wav);

/**
 * \brief set filename format
 *
 * WAV format doesn't support changing data format so
 * if audio stream configuration changes, wav has to start
 * a new stream file.
 *
 * %d in filename is substituted with counter.
 *
 * Default format is "audio%02d.wav"
 * \param wav wav object
 * \param filename filename format
 * \return 0 on success otherwise an error code
 */
__PUBLIC int wav_set_filename(wav_t wav, const char *filename);

/**
 * \brief set audio stream number
 *
 * Only audio from one audio stream will be written. Default
 * stream number is 1.
 * \param wav wav object
 * \param audio audio stream number
 * \return 0 on success otherwise an error code
 */
__PUBLIC int wav_set_stream_number(wav_t wav, glc_audio_i audio);

/**
 * \brief set interpolation
 *
 * By default silence is written if audio data is missing
 * to preserve a/v sync.
 * \param wav wav object
 * \param interpolate 1 means missing data is interpolated (silence),
 *                    0 disables interpolation and a/v sync is lost.
 * \return 0 on success otherwise an error code
 */
__PUBLIC int wav_set_interpolation(wav_t wav, int interpolate);

/**
 * \brief set silence threshold
 *
 * Default silence threshold is 200 000 usec = 0.2s.
 * \param wav wav object
 * \param silence_threshold silence threshold
 * \return 0 on success otherwise an error code
 */
__PUBLIC int wav_set_silence_threshold(wav_t wav, glc_utime_t silence_threshold);

/**
 * \brief start wav process
 *
 * wav writes audio data from selected audio stream into
 * WAV-formatted file.
 * \param wav wav object
 * \param from source buffer
 * \return 0 on success otherwise an error code
 */
__PUBLIC int wav_process_start(wav_t wav, ps_buffer_t *from);

/**
 * \brief block until process has finished
 * \param wav wav object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int wav_process_wait(wav_t wav);

#ifdef __cplusplus
}
#endif

#endif

/**  \} */
/**  \} */
