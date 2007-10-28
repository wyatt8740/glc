/**
 * \file src/stream/info.h
 * \brief stream information
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* info.h -- stream information
 * Copyright (C) 2007 Pyry Haulos
 * For conditions of distribution and use, see copyright notice in glc.h
 */

#ifndef _INFO_H
#define _INFO_H

#include <packetstream.h>
#include "../common/glc.h"

/**
 * \addtogroup info
 *  \{
 */

__PUBLIC int info_init(glc_t *glc, ps_buffer_t *from);

/**  \} */

#endif

