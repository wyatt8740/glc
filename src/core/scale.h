/**
 * \file src/core/scale.h
 * \brief software scaler
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup scale
 *  \{
 */

#ifndef _SCALE_H
#define _SCALE_H

#include <packetstream.h>
#include "../common/glc.h"

/**
 * \brief scale object
 */
typedef struct scale_s* scale_t;

/**
 * \brief initialize scale object
 * \param scale scale object
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
__PUBLIC int scale_init(scale_t *scale, glc_t *glc);

/**
 * \brief set scaling factor
 *
 * If scaling factor is set, frame dimensions are multiplied
 * with scaling factor.
 * \param scale scale object
 * \param factor scaling factor
 * \return 0 on success otherwise an error code
 */
__PUBLIC int scale_set_scale(scale_t scale, double factor);

/**
 * \brief set scale size
 *
 * Scale frames to constant size. Aspect ratio is preserved so
 * black borders will appear if aspect ratio doesn't match.
 * \param scale scale object
 * \param width width
 * \param height height
 * \return 0 on success otherwise an error code
 */
__PUBLIC int scale_set_size(scale_t scale, unsigned int width,
			    unsigned int height);

/**
 * \brief process data
 *
 * scale rescales all YCBCR and RGB frames according to
 * active scaling configuration.
 * \param scale scale object
 * \param from source buffer
 * \param to target buffer
 * \return 0 on success otherwise an error code
 */
__PUBLIC int scale_process_start(scale_t scale, ps_buffer_t *from,
				 ps_buffer_t *to);

/**
 * \brief block until current process has finished
 * \param scale scale object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int scale_process_wait(scale_t scale);

/**
 * \brief destroy scale object
 * \param scale scale object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int scale_destroy(scale_t scale);

#endif

/**  \} */
