/**
 * \file src/stream/rgb.h
 * \brief convert Y'CbCr to BGR
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* rgb.h -- convert Y'CbCr to BGR
 * Copyright (C) 2007 Pyry Haulos
 * For conditions of distribution and use, see copyright notice in glc.h
 */

#ifndef _BGR_H
#define _BGR_H

#include <packetstream.h>
#include "../common/glc.h"

/**
 * \addtogroup rgb
 *  \{
 */

__PUBLIC int rgb_init(glc_t *glc, ps_buffer_t *from, ps_buffer_t *to);

/**  \} */

#endif
