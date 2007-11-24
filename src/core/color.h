/**
 * \file src/stream/color.h
 * \brief color correction
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* color.h -- color correction
 * Copyright (C) 2007 Pyry Haulos
 * For conditions of distribution and use, see copyright notice in glc.h
 */

#ifndef _COLOR_H
#define _COLOR_H

#include <packetstream.h>
#include "../common/glc.h"

/**
 * \addtogroup color
 *  \{
 */

__PUBLIC int color_init(glc_t *glc, ps_buffer_t *from, ps_buffer_t *to);

/**  \} */

#endif
