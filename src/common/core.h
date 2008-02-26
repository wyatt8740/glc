/**
 * \file src/common/core.h
 * \brief glc core interface
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup common
 *  \{
 * \defgroup core core
 *  \{
 */

#ifndef _CORE_H
#define _CORE_H

#include "glc.h"

/**
 * \brief initialize glc
 *
 * This function initializes core, log and util components.
 * State is not initialized.
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
__PUBLIC int glc_init(glc_t *glc);

/**
 * \brief destroy glc
 *
 * This cleans up core, log and util. State must be destroyed
 * before calling this function.
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
__PUBLIC int glc_destroy(glc_t *glc);

/**
 * \brief current time in microseconds since initialization
 * \param glc glc
 * \return time elapsed since initialization
 */
__PUBLIC glc_utime_t glc_time(glc_t *glc);

/**
 * \brief thread count hint
 *
 * All processing filters that can employ multiple threads use
 * this function to determine how many threads to create. By default
 * this returns number of processors online, but custom value can
 * be set via glc_set_threads_hint().
 * \param glc glc
 * \return thread count hint
 */
__PUBLIC long int glc_threads_hint(glc_t *glc);

/**
 * \brief set thread count hint
 *
 * Default value is number of processors.
 * \param glc glc
 * \param count thread count hint
 * \return 0 on success otherwise an error code
 */
__PUBLIC int glc_set_threads_hint(glc_t *glc, long int count);

#endif

/**  \} */
/**  \} */
