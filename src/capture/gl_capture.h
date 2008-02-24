/**
 * \file src/capture/gl_capture.h
 * \brief OpenGL capture
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup gl_capture
 *  \{
 */

#ifndef _GL_CAPTURE_H
#define _GL_CAPTURE_H

#include <X11/X.h>
#include <GL/glx.h>
#include <packetstream.h>
#include "../common/glc.h"

/**
 * \brief gl_capture object
 */
typedef struct gl_capture_s* gl_capture_t;

/**
 * \brief initialize gl_capture object
 *
 * Function initializes gl_capture object and binds it into given glc.
 * \param gl_capture gl_capture object
 * \param glc glc
 * \param to target buffer
 * \return 0 on success otherwise an error code
 */
__PUBLIC int gl_capture_init(gl_capture_t *gl_capture, glc_t *glc);

/**
 * \brief set target buffer
 * \param gl_capture gl_capture object
 * \param buffer target buffer
 * \return 0 on success otherwise an error code
 */
__PUBLIC int gl_capture_set_buffer(gl_capture_t gl_capture, ps_buffer_t *buffer);

/**
 * \brief set OpenGL read buffer for capturing
 * \param gl_capture gl_capture object
 * \param buffer GL_BACK or GL_FRONT
 * \return 0 on success otherwise an error code
 */
__PUBLIC int gl_capture_set_read_buffer(gl_capture_t gl_capture, GLenum buffer);

/**
 * \brief set fps
 * \param gl_capture gl_capture object
 * \param fps fps
 * \return 0 on success otherwise an error code
 */
__PUBLIC int gl_capture_set_fps(gl_capture_t gl_capture, double fps);

/**
 * \brief set GL_PACK_ALIGNMENT for OpenGL read calls
 * \param gl_capture gl_capture object
 * \param pack_alignment GL_PACK_ALIGNMENT
 * \return 0 on success otherwise an error code
 */
__PUBLIC int gl_capture_set_pack_alignment(gl_capture_t gl_capture,
					   GLint pack_alignment);

/**
 * \brief set PBO hint
 * \param gl_capture gl_capture object
 * \param try_pbo 1 means gl_capture tries to use PBO,
 *                0 disables PBO even if it is supported
 * \return 0 on success otherwise an error code
 */
__PUBLIC int gl_capture_try_pbo(gl_capture_t gl_capture, int try_pbo);

/**
 * \brief set pixel format
 * \param gl_capture gl_capture object
 * \param GLenum format
 * \return 0 on succcess otherwise an error code
 */
__PUBLIC int gl_capture_set_pixel_format(gl_capture_t gl_capture, GLenum format);

/**
 * \brief draw indicator when capturing
 *
 * Drawing indicator does not currently work well when capturing from
 * front buffer.
 * \param gl_capture gl_capture object
 * \param draw_indicator 1 means indicator is drawn when capturing,
 *                       0 disables indicator
 * \return 0 on success otherwise an error code
 */
__PUBLIC int gl_capture_draw_indicator(gl_capture_t gl_capture, int draw_indicator);

/**
 * \brief capture only selected area
 *
 * Calculated from top left corner. Use 0, 0, 0, 0 to disable cropping.
 * \param gl_capture gl_capture object
 * \param x x-coordinate
 * \param y y-coordinate
 * \param width width
 * \param height height
 * \return 0 on success otherwise an error code
 */
__PUBLIC int gl_capture_crop(gl_capture_t gl_capture, unsigned int x, unsigned int y,
			     unsigned int width, unsigned int height);

/**
 * \brief lock fps when capturing
 * \param gl_capture gl_capture object
 * \param lock_fps 1 means fps is locked, 0 disables fps cap
 * \return 0 on success otherwise an error code
 */
__PUBLIC int gl_capture_lock_fps(gl_capture_t gl_capture, int lock_fps);

/**
 * \brief start capturing
 * \param gl_capture gl_capture object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int gl_capture_start(gl_capture_t gl_capture);

/**
 * \brief stop capturing
 * \param gl_capture gl_capture object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int gl_capture_stop(gl_capture_t gl_capture);

/**
 * \brief destroy gl_capture object
 * \param gl_capture gl_capture object to destroy
 * \return 0 on success otherwise an error code
 */
__PUBLIC int gl_capture_destroy(gl_capture_t gl_capture);

/**
 * \brief process full frame
 *
 * Call this function when selected read buffer contains ready
 * frame for capturing. If gl_capture is not in capturing state,
 * this function does nothing.
 * \Example
 * \code
 * // init
 * ...
 * gl_capture_set_read_buffer(gl_capture, GL_FRONT);
 * gl_capture_start(gl_capture);
 * ...
 * // main loop
 * glXSwapBuffers(dpy, drawable);
 * gl_capture_frame(gl_capture, dpy, drawable);
 * \endcode
 * \param gl_capture gl_capture object
 * \param dpy X Display
 * \param drawable GLX Drawable
 * \return 0 on success otherwise an error code
 */
__PUBLIC int gl_capture_frame(gl_capture_t gl_capture, Display *dpy, GLXDrawable drawable);

/**
 * \brief refresh color correction information
 * \param gl_capture gl_capture object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int gl_capture_refresh_color_correction(gl_capture_t gl_capture);

/**  \} */

#endif
