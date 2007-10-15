/**
 * \file src/stream/demux.h
 * \brief audio/picture stream demuxer
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* demux.h -- audio/picture stream demuxer
 * Copyright (C) 2007 Pyry Haulos
 * For conditions of distribution and use, see copyright notice in glc.h
 */

#ifndef _DEMUX_H
#define _DEMUX_H

#include <packetstream.h>
#include "../common/glc.h"

/**
 * \addtogroup demux
 *  \{
 */

int demux_init(glc_t *glc, ps_buffer_t *from);

/**  \} */

#endif

