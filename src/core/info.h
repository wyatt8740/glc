/**
 * \file src/core/info.h
 * \brief stream information
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup info
 *  \{
 */

#ifndef _INFO_H
#define _INFO_H

#include <packetstream.h>
#include "../common/glc.h"

__PUBLIC void *info_init(glc_t *glc, ps_buffer_t *from);
__PUBLIC int info_wait(void *infopriv);

/**  \} */

#endif
