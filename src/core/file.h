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

__PUBLIC void *file_init(glc_t *glc, ps_buffer_t *from);
__PUBLIC int file_wait(void *filepriv);

/**
 * \brief Read stream from file and write it into buffer.
 *
 * This function assumes that stream starts immediately at current
 * read position.
 * \param glc glc
 * \param to buffer
 */
__PUBLIC int file_read(glc_t *glc, ps_buffer_t *to);

#endif

/**  \} */
