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
#include <stdarg.h>
#include "glc.h"

/**
 * \addtogroup common
 *  \{
 */

/**
 * \defgroup util utility functions
 *  \{
 */

__PUBLIC int glc_create(glc_t **glc);
__PUBLIC int glc_destroy(glc_t *glc);

__PUBLIC int util_init(glc_t *glc);
__PUBLIC int util_free(glc_t *glc);

__PUBLIC int util_log_init(glc_t *glc);
__PUBLIC __attribute__((format(printf, 4, 5)))
	void util_log(glc_t *glc, int level, const char *module, const char *format, ...);
__PUBLIC int util_log_close(glc_t *glc);

__PUBLIC glc_utime_t util_time(glc_t *glc);
__PUBLIC int util_timediff(glc_t *glc, glc_stime_t diff);

__PUBLIC int util_load_info(glc_t *glc, const char *filename);
__PUBLIC int util_create_info(glc_t *glc);
__PUBLIC int util_init_info(glc_t *glc);
__PUBLIC int util_free_info(glc_t *glc);

__PUBLIC void util_log_info(glc_t *glc);

__PUBLIC long int util_cpus();

__PUBLIC glc_audio_i util_audio_stream_id(glc_t *glc);

__PUBLIC int util_write_end_of_stream(glc_t *glc, ps_buffer_t *to);

/**  \} */
/**  \} */

#endif
