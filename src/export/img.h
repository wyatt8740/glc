/**
 * \file src/export/img.h
 * \brief export to images
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup img
 *  \{
 */

#ifndef _IMG_H
#define _IMG_H

#include <packetstream.h>
#include "../common/glc.h"

__PUBLIC int img_init(glc_t *glc, ps_buffer_t *from);

#endif

/**  \} */
