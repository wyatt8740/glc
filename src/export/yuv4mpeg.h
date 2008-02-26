/**
 * \file src/export/yuv4mpeg.h
 * \brief yuv4mpeg output
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup yuv4mpeg
 *  \{
 */

#ifndef _YUV4MPEG_H
#define _YUV4MPEG_H

/**
 * \brief yuv4mpeg object
 */
typedef struct yuv4mpeg_s* yuv4mpeg_t;

/**
 * \brief initialize yuv4mpeg object
 * \param yuv4mpeg yuv4mpeg object
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
__PUBLIC int yuv4mpeg_init(yuv4mpeg_t *yuv4mpeg, glc_t *glc);

/**
 * \brief destroy yuv4mpeg object
 * \param yuv4mpeg yuv4mpeg object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int yuv4mpeg_destroy(yuv4mpeg_t yuv4mpeg);

/**
 * \brief set filename format
 *
 * Y4M format doesn't support changing picture sizes so
 * if stream configuration changes, yuv4mpeg has to start a new
 * stream file. %d in filename is substituted with counter.
 *
 * Default format is "video%02d.y4m"
 * \param yuv4mpeg yuv4mpeg object
 * \param filename filename format
 * \return 0 on success otherwise an error code
 */
__PUBLIC int yuv4mpeg_set_filename(yuv4mpeg_t yuv4mpeg, const char *filename);

/**
 * \brief set video stream number
 *
 * Only frames from one video stream will be written. Default
 * stream number is 1.
 * \param yuv4mpeg yuv4mpeg object
 * \param ctx video stream number
 * \return 0 on success otherwise an error code
 */
__PUBLIC int yuv4mpeg_set_stream_number(yuv4mpeg_t yuv4mpeg, glc_ctx_i ctx);

/**
 * \brief set fps
 *
 * Default fps is 30.
 * \param yuv4mpeg yuv4mpeg object
 * \param fps fps
 * \return 0 on success otherwise an error code
 */
__PUBLIC int yuv4mpeg_set_fps(yuv4mpeg_t yuv4mpeg, double fps);

/**
 * \brief set interpolation
 *
 * By default missing frames are copied from previous frame
 * to preserve a/v sync.
 * \param yuv4mpeg yuv4mpeg object
 * \param interpolate 1 means missing frames are interpolated,
 *                    0 disables interpolation and a/v sync is lost.
 * \return 0 on success otherwise an error code
 */
__PUBLIC int yuv4mpeg_set_interpolation(yuv4mpeg_t yuv4mpeg, int interpolate);

/**
 * \brief start yuv4mpeg process
 *
 * yuv4mpeg writes Y'CbCr frames in selected video stream
 * into yuv4mpeg formatted file.
 * \param yuv4mpeg yuv4mpeg object
 * \param from source buffer
 * \return 0 on success otherwise an error code
 */
__PUBLIC int yuv4mpeg_process_start(yuv4mpeg_t yuv4mpeg, ps_buffer_t *from);

/**
 * \brief block until process has finished
 * \param yuv4mpeg yuv4mpeg object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int yuv4mpeg_process_wait(yuv4mpeg_t yuv4mpeg);

#endif

/**  \} */
