/**
 * \file glc/core/color.h
 * \brief color correction
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup core
 *  \{
 * \defgroup color color correction
 *  \{
 */

#ifndef _COLOR_H
#define _COLOR_H

#include <packetstream.h>
#include <glc/common/glc.h>

/**
 * \brief color object
 */
typedef struct color_s* color_t;

/**
 * \brief initialize color object
 * \param color color object
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
__PUBLIC int color_init(color_t *color, glc_t *glc);

/**
 * \brief destroy color object
 * \param color color object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int color_destroy(color_t color);

/**
 * \brief override color correction
 * \param color color object
 * \param brightness brightness value
 * \param contrast contrast value
 * \param red red gamma
 * \param green green gamma
 * \param blue blue gamma
 * \return 0 on success otherwise an error code
 */
__PUBLIC int color_override(color_t color, float brightness, float contrast,
			    float red, float green, float blue);

/**
 * \brief clear override
 * \param color color object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int color_override_clear(color_t color);

/**
 * \brief start color process
 *
 * color applies color corretion to all Y'CbCr and RGB
 * frames in stream.
 * \param color color object
 * \param from source buffer
 * \param to target buffer
 * \return 0 on success otherwise an error code
 */
__PUBLIC int color_process_start(color_t color, ps_buffer_t *from, ps_buffer_t *to);

/**
 * \brief block until process has finished
 * \param color color object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int color_process_wait(color_t color);

#endif

/**  \} */
/**  \} */
