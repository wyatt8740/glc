/**
 * \file glc/common/util.h
 * \brief utility functions interface
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup common
 *  \{
 * \defgroup util utility functions
 *  \{
 */

#ifndef _UTIL_H
#define _UTIL_H

#include <packetstream.h>
#include <stdarg.h>
#include <glc/common/glc.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief initialize utilities
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
__PRIVATE int glc_util_init(glc_t *glc);

/**
 * \brief destroy utilities
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
__PRIVATE int glc_util_destroy(glc_t *glc);

/**
 * \brief set fps hint for stream information
 * \param glc glc
 * \param fps fps
 * \return 0 on success otherwise an error code
 */
__PUBLIC int glc_util_info_fps(glc_t *glc, double fps);

/**
 * \brief create stream information
 * \param glc glc
 * \param stream_info returned stream information structure
 * \param info_name returned application name
 * \param info_date returned date
 * \return 0 on success otherwise an error code
 */
__PUBLIC int glc_util_info_create(glc_t *glc, glc_stream_info_t **stream_info,
				  char **info_name, char **info_date);

/**
 * \brief write version message into log
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
__PUBLIC int glc_util_log_version(glc_t *glc);

/**
 * \brief write system information into log
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
__PUBLIC int glc_util_log_info(glc_t *glc);

/**
 * \brief write 'end of stream'-packet into buffer
 * \param glc glc
 * \param to target buffer
 * \return 0 on success otherwise an error code
 */
__PUBLIC int glc_util_write_end_of_stream(glc_t *glc, ps_buffer_t *to);

/**
 * \brief replace all occurences of string with another string
 * \param str string to manipulate
 * \param find string to find
 * \param replace string to replace occurences with
 * \return new string
 */
__PUBLIC char *glc_util_str_replace(const char *str, const char *find, const char *replace);

/**
 * \brief create filename based on current date, time, app name, pid etc.
 *
 * Available tags in format are:
 *  %app%	binary name without path
 *  %pid%	process id
 *  %capture%	N'th capture (given as argument)
 *  %year%	4-digit year
 *  %month%	2-digit month
 *  %day%	2-digit day
 *  %hour%	2-digit hour
 *  %min%	2-digit minute
 *  %sec%	2-digit second
 * \param format format string
 * \param capture N'th capture
 * \return new filename string
 */
__PUBLIC char *glc_util_format_filename(const char *fmt, unsigned int capture);

#ifdef __cplusplus
}
#endif

#endif

/**  \} */
/**  \} */
