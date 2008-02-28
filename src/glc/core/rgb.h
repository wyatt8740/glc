/**
 * \file glc/core/rgb.h
 * \brief convert Y'CbCr to BGR
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup core
 *  \{
 * \defgroup rgb convert Y'CbCr to BGR
 *  \{
 */

#ifndef _BGR_H
#define _BGR_H

#include <packetstream.h>
#include <glc/common/glc.h>

/**
 * \brief rgb object
 */
typedef struct rgb_s* rgb_t;

/**
 * \brief initialize rgb object
 * \param rgb rgb object
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
__PUBLIC int rgb_init(rgb_t *rgb, glc_t *glc);

/**
 * \brief destroy rgb object
 * \param rgb rgb object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int rgb_destroy(rgb_t rgb);

/**
 * \brief start rgb process
 *
 * rgb converts all Y'CbCr frames into RGB (BGR).
 * No scaling is currently supported.
 * \param rgb rgb object
 * \param from source buffer
 * \param to target buffer
 * \return 0 on success otherwise an error code
 */
__PUBLIC int rgb_process_start(rgb_t rgb, ps_buffer_t *from, ps_buffer_t *to);

/**
 * \brief block until process has finished
 * \param rgb rgb object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int rgb_process_wait(rgb_t rgb);

#endif

/**  \} */
/**  \} */
