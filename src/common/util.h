/**
 * \file src/common/util.h
 * \brief utility functions interface
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* util.h -- utility functions
 * Copyright (C) 2007 Pyry Haulos
 * For conditions of distribution and use, see copyright notice in glc.h
 */

#ifndef _UTIL_H
#define _UTIL_H

#include <packetstream.h>
#include "glc.h"

/**
 * \addtogroup common
 *  \{
 */

/**
 * \defgroup util utility functions
 *  \{
 */

int glc_create(glc_t **glc);
int glc_destroy(glc_t *glc);

int util_init(glc_t *glc);
int util_free(glc_t *glc);

glc_utime_t util_timestamp(glc_t *glc);
int util_timediff(glc_t *glc, glc_stime_t diff);

int util_load_info(glc_t *glc, const char *filename);
int util_create_info(glc_t *glc);
int util_init_info(glc_t *glc);
int util_free_info(glc_t *glc);

long int util_cpus();

int util_write_end_of_stream(glc_t *glc, ps_buffer_t *to);

/**  \} */
/**  \} */

#endif
