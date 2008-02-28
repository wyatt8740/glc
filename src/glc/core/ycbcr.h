/**
 * \file glc/core/ycbcr.h
 * \brief convert BGR to Y'CbCr and scale
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup core
 *  \{
 * \defgroup ycbcr convert BGR to Y'CbCr and scale
 *  \{
 */

#ifndef _YCBCR_H
#define _YCBCR_H

#include <glc/common/glc.h>
#include <packetstream.h>

/**
 * \brief ycbcr object
 */
typedef struct ycbcr_s* ycbcr_t;

/**
 * \brief initialize ycbcr object
 * \param ycbcr ycbcr object
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
__PUBLIC int ycbcr_init(ycbcr_t *ycbcr, glc_t *glc);

/**
 * \brief set scaling factor
 * \param ycbcr ycbr object
 * \param scale scale factor
 * \return 0 on success otherwise an error code
 */
__PUBLIC int ycbcr_set_scale(ycbcr_t ycbcr, double scale);

/**
 * \brief process data and transfer between buffers
 *
 * ycbcr process converts all BGR and BGRA frames into
 * YCBCR_420JPEG and optionally does rescaling. Downscaling
 * is cheap operation and mostly makes actual conversion much
 * faster since smaller amount of data has to be converted.
 *
 * This returns immediately. Actual processing is done in
 * different thread.
 * \param ycbcr ycbcr object
 * \param from source buffer
 * \param to target buffer
 * \return 0 on success otherwise an error code
 */
__PUBLIC int ycbcr_process_start(ycbcr_t ycbcr, ps_buffer_t *from,
				 ps_buffer_t *to);

/**
 * \brief block until current process has finished
 * \param ycbcr ycbcr object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int ycbcr_process_wait(ycbcr_t ycbcr);

/**
 * \brief destroy ycbcr object
 * \param ycbcr ycbcr object to destroy
 * \return 0 on success otherwise an error code
 */
__PUBLIC int ycbcr_destroy(ycbcr_t ycbcr);

#endif

/**  \} */
/**  \} */
