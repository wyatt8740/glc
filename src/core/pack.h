/**
 * \file src/core/pack.h
 * \brief stream compression
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup pack
 *  \{
 */

#ifndef _PACK_H
#define _PACK_H

#include <packetstream.h>
#include "../common/glc.h"

__PUBLIC int pack_init(glc_t *glc, ps_buffer_t *from, ps_buffer_t *to);

__PUBLIC int unpack_init(glc_t *glc, ps_buffer_t *from, ps_buffer_t *to);

#endif

/**  \} */
