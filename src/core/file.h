/**
 * \file src/stream/file.h
 * \brief file io
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* file.h -- file io
 * Copyright (C) 2007 Pyry Haulos
 * For conditions of distribution and use, see copyright notice in glc.h
 */

#ifndef _FILE_H
#define _FILE_H

#include <packetstream.h>
#include "../common/glc.h"

/**
 * \addtogroup file
 *  \{
 */

__PUBLIC int file_init(glc_t *glc, ps_buffer_t *from);

__PUBLIC int file_read(glc_t *glc, ps_buffer_t *to);

/**  \} */

#endif
