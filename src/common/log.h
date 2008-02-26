/**
 * \file src/common/log.h
 * \brief glc log interface
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup common
 *  \{
 * \defgroup log logging
 *  \{
 */

#ifndef _LOG_H
#define _LOG_H

#include <stdarg.h>
#include <stdio.h>
#include "glc.h"

/**
 * \brief initialize log
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
__PRIVATE int glc_log_init(glc_t *glc);

/**
 * \brief destroy log
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
__PRIVATE int glc_log_destroy(glc_t *glc);

/**
 * \brief set log level
 *
 * Messages with level <= current log level will be written
 * into log.
 * \param glc glc
 * \param level log level
 * \return 0 on success otherwise an error code
 */
__PUBLIC int glc_log_set_level(glc_t *glc, int level);

/**
 * \brief open file for log
 * \note this calls glc_log_set_stream()
 * \param glc glc
 * \param filename log file name
 * \return 0 on success otherwise an error code
 */
__PUBLIC int glc_log_open_file(glc_t *glc, const char *filename);

/**
 * \brief set log stream
 *
 * Default log target is stderr.
 * \param glc glc
 * \param stream log stream
 * \return 0 on success otherwise an error code
 */
__PUBLIC int glc_log_set_stream(glc_t *glc, FILE *stream);

/**
 * \brief close current log stream
 *
 * Log file is set to stderr.
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
__PUBLIC int glc_log_close(glc_t *glc);

/**
 * \brief write message to log
 *
 * Message is actually written to log if level is
 * lesser than, or equal to current log verbosity level and
 * logging is enabled.
 * \param glc glc
 * \param level message level
 * \param module module
 * \param format passed to fprintf()
 * \param ... passed to fprintf()
 */
__PUBLIC __attribute__((format(printf, 4, 5)))
	void glc_log(glc_t *glc, int level, const char *module, const char *format, ...);

#endif

/**  \} */
/**  \} */
