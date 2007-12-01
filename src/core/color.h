/**
 * \file src/core/color.h
 * \brief color correction
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup color
 *  \{
 */

#ifndef _COLOR_H
#define _COLOR_H

#include <packetstream.h>
#include "../common/glc.h"

__PUBLIC int color_init(glc_t *glc, ps_buffer_t *from, ps_buffer_t *to);

#endif

/**  \} */
