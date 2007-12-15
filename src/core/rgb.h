/**
 * \file src/core/rgb.h
 * \brief convert Y'CbCr to BGR
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup rgb
 *  \{
 */

#ifndef _BGR_H
#define _BGR_H

#include <packetstream.h>
#include "../common/glc.h"

__PUBLIC void *rgb_init(glc_t *glc, ps_buffer_t *from, ps_buffer_t *to);
__PUBLIC int rgb_wait(void *rgbpriv);

#endif

/**  \} */
