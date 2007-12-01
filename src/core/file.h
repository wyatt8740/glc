/**
 * \file src/core/file.h
 * \brief file io
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup file
 *  \{
 */

#ifndef _FILE_H
#define _FILE_H

#include <packetstream.h>
#include "../common/glc.h"

__PUBLIC int file_init(glc_t *glc, ps_buffer_t *from);

__PUBLIC int file_read(glc_t *glc, ps_buffer_t *to);

#endif

/**  \} */
