/**
 * \file src/core/ycbcr.h
 * \brief convert BGR to Y'CbCr and scale
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup ycbcr
 *  \{
 */

#ifndef _YCBCR_H
#define _YCBCR_H

#include "../common/glc.h"
#include <packetstream.h>

__PUBLIC void *ycbcr_init(glc_t *glc, ps_buffer_t *from, ps_buffer_t *to);
__PUBLIC int ycbcr_wait(void *ycbcrpriv);

#endif

/**  \} */
