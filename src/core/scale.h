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

__PUBLIC int scale_init(glc_t *glc, ps_buffer_t *from, ps_buffer_t *to);

#endif

/**  \} */
