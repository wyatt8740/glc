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

/* FILE* ? */
#include <stdio.h>
#include <packetstream.h>
#include "../common/glc.h"

/**
 * \brief rgb object
 */
typedef struct info_s* info_t;

/**
 * \brief initialize info object
 * \param info info object
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
__PUBLIC int info_init(info_t *info, glc_t *glc);

/**
 * \brief destroy info object
 * \param info info object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int info_destroy(info_t info);

/**
 * \brief set verbosity level
 *
 * Default verbosity level is 1. Higher level means
 * more verbose output.
 * \param info info object
 * \param level verbosity level, starting from 1
 * \return 0 on success otherwise an error code
 */
__PUBLIC int info_set_level(info_t info, int level);

/**
 * \brief set output stream
 *
 * By default info writes into standard output.
 * \param info info object
 * \param stream output stream
 * \return 0 on success otherwise an error code
 */
__PUBLIC int info_set_stream(info_t info, FILE *stream);

/**
 * \brief start info process
 *
 * info collects and prints out detailed stream information according to
 * selected verbosity level.
 * \param info info object
 * \param from source buffer
 * \return 0 on success otherwise an error code
 */
__PUBLIC int info_process_start(info_t info, ps_buffer_t *from);

/**
 * \brief block until process has finished
 * \param info info object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int info_process_wait(info_t info);

#endif

/**  \} */
