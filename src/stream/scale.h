/**
 * \file src/stream/scale.h
 * \brief bilinear filtering
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* scale.h -- bilinear filtering
 * Copyright (C) 2007 Pyry Haulos
 * For conditions of distribution and use, see copyright notice in glc.h
 */

#ifndef _SCALE_H
#define _SCALE_H

#include <packetstream.h>
#include "../common/glc.h"

/**
 * \addtogroup scale
 *  \{
 */

__PUBLIC int scale_init(glc_t *glc, ps_buffer_t *from, ps_buffer_t *to);

/**  \} */

#endif
