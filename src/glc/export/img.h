/**
 * \file glc/export/img.h
 * \brief export to images
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup export
 *  \{
 * \defgroup img export to images
 *  \{
 */

#ifndef _IMG_H
#define _IMG_H

#include <packetstream.h>
#include <glc/common/glc.h>

#ifdef __cplusplus
extern "C" {
#endif

/** BMP format */
#define IMG_BMP     0x1
/** PNG format */
#define IMG_PNG     0x2

/**
 * \brief img object
 */
typedef struct img_s* img_t;

/**
 * \brief initialize img object
 * \param img img object
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
__PUBLIC int img_init(img_t *img, glc_t *glc);

/**
 * \brief destroy img object
 * \param img img object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int img_destroy(img_t img);

/**
 * \brief set filename format
 *
 * %d in filename is substituted with frame number.
 *
 * Default format is "frame%08d.png"
 * \param img img object
 * \param filename filename format
 * \return 0 on success otherwise an error code
 */
__PUBLIC int img_set_filename(img_t img, const char *filename);

/**
 * \brief set video stream number
 *
 * Only frames from one video stream will be written. Default
 * stream number is 1.
 * \param img img object
 * \param id video stream id
 * \return 0 on success otherwise an error code
 */
__PUBLIC int img_set_stream_id(img_t img, glc_stream_id_t id);

/**
 * \brief set fps
 *
 * Default fps is 30.
 * \param img img object
 * \param fps fps
 * \return 0 on success otherwise an error code
 */
__PUBLIC int img_set_fps(img_t img, double fps);

/**
 * \brief set format
 *
 * Currently BMP (IMG_BMP) and PNG (IMG_PNG) are supported.
 *
 * Default format is PNG.
 * \param img img object
 * \param format image format
 * \return 0 on success otherwise an error code
 */
__PUBLIC int img_set_format(img_t img, int format);

/**
 * \brief start img process
 *
 * img writes RGB (BGR only) frames in selected video stream
 * into separate image files.
 * \param img img object
 * \param from source buffer
 * \return 0 on success otherwise an error code
 */
__PUBLIC int img_process_start(img_t img, ps_buffer_t *from);

/**
 * \brief block until process has finished
 * \param img img object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int img_process_wait(img_t img);

#ifdef __cplusplus
}
#endif

#endif

/**  \} */
/**  \} */
