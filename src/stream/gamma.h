/**
 * \file src/stream/gamma.h
 * \brief gamma correction
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* gamma.h -- gamma correction
 * Copyright (C) 2007 Pyry Haulos
 * For conditions of distribution and use, see copyright notice in glc.h
 */

#ifndef _GAMMA_H
#define _GAMMA_H

#include <packetstream.h>
#include "../common/glc.h"

/**
 * \addtogroup gamma
 *  \{
 */

/** \TODO support GammaRamp */
int gamma_capture(glc_t *glc, ps_buffer_t *to, float red, float green, float blue);

/**  \} */

#endif
