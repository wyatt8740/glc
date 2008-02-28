/**
 * \file glc/play/gl_play.h
 * \brief OpenGL playback
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup play
 *  \{
 * \defgroup gl_play OpenGL playback
 *  \{
 */

#ifndef _GL_PLAY_H
#define _GL_PLAY_H

#include <packetstream.h>
#include <glc/common/glc.h>

/**
 * \brief gl_play object
 */
typedef struct gl_play_s* gl_play_t;

/**
 * \brief initialize gl_play object
 * \param gl_play gl_play object
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
__PUBLIC int gl_play_init(gl_play_t *gl_play, glc_t *glc);

/**
 * \brief destroy gl_play object
 * \param gl_play gl_play object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int gl_play_destroy(gl_play_t gl_play);

/**
 * \brief set video stream number
 *
 * Default video stream is 1.
 * \param gl_play gl_play object
 * \param ctx video stream number
 * \return 0 on success otherwise an error code
 */
__PUBLIC int gl_play_set_stream_number(gl_play_t gl_play, glc_ctx_i ctx);

/**
 * \brief start gl_play process
 *
 * gl_play plays RGB (BGR) video data from selected video stream.
 * \param gl_play gl_play object
 * \param from source buffer
 * \return 0 on success otherwise an error code
 */
__PUBLIC int gl_play_process_start(gl_play_t gl_play, ps_buffer_t *from);

/**
 * \brief block until process has finished
 * \param gl_play gl_play object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int gl_play_process_wait(gl_play_t gl_play);

#endif

/**  \} */
/**  \} */
