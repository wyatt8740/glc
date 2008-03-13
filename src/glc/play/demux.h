/**
 * \file glc/play/demux.h
 * \brief audio/picture stream demuxer
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup play
 *  \{
 * \defgroup demux audio/picture stream demuxer
 *  \{
 */

#ifndef _DEMUX_H
#define _DEMUX_H

#include <packetstream.h>
#include <glc/common/glc.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief demux object
 */
typedef struct demux_s* demux_t;

/**
 * \brief initialize demux object
 * \param demux demux object
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
__PUBLIC int demux_init(demux_t *demux, glc_t *glc);

/**
 * \brief destroy demux object
 * \param demux demux object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int demux_destroy(demux_t demux);

/**
 * \brief set video stream buffer size
 *
 * Default buffer size for video streams is 10 MiB
 * \param demux demux object
 * \param size video stream buffer size
 * \return 0 on success otherwise an error code
 */
__PUBLIC int demux_set_video_buffer_size(demux_t demux, size_t size);

/**
 * \brief set audio stream buffer size
 *
 * Default buffer size for audio streams is 1 MiB
 * \param demux demux object
 * \param size audio stream buffer size
 * \return 0 on success otherwise an error code
 */
__PUBLIC int demux_set_audio_buffer_size(demux_t demux, size_t size);

/**
 * \brief set ALSA playback device
 *
 * Default ALSA playback device is "default".
 * \param demux demux object
 * \param device ALSA playback device
 * \return 0 on success otherwise an error code
 */
__PUBLIC int demux_set_alsa_playback_device(demux_t demux, const char *device);

/**
 * \brief start demux process
 *
 * demux demuxes glc stream. For each video and audio stream is
 * created own gl_play/audio_play object and stream buffer.
 *
 * demux takes care of initializing and cleaning up gl_play/audio_play
 * objects.
 * \param demux demux object
 * \param from source buffer
 * \return 0 on success otherwise an error code
 */
__PUBLIC int demux_process_start(demux_t demux, ps_buffer_t *from);

/**
 * \brief block until process has finished
 * \param demux demux object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int demux_process_wait(demux_t demux);

#ifdef __cplusplus
}
#endif

#endif

/**  \} */
/**  \} */
