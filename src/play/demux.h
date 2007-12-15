/**
 * \file src/play/demux.h
 * \brief audio/picture stream demuxer
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup demux
 *  \{
 */

#ifndef _DEMUX_H
#define _DEMUX_H

#include <packetstream.h>
#include "../common/glc.h"

__PUBLIC void *demux_init(glc_t *glc, ps_buffer_t *from);
__PUBLIC int demux_wait(void *demuxpriv);

#endif

/**  \} */

