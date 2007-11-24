/**
 * \file src/core/ycbcr.h
 * \brief convert BGR to Y'CbCr and scale
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* ycbcr.h -- convert BGR to Y'CbCr and scale
 * Copyright (C) 2007 Pyry Haulos
 * For conditions of distribution and use, see copyright notice in glc.h
 */

#ifndef _YCBCR_H
#define _YCBCR_H

#include "../common/glc.h"
#include <packetstream.h>

/**
 * \addtogroup ycbcr
 *  \{
 */

__PUBLIC int ycbcr_init(glc_t *glc, ps_buffer_t *from, ps_buffer_t *to);

/**  \} */

#endif
