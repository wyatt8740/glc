/**
 * \file src/play/gl_play.h
 * \brief OpenGL playback
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup gl_play
 *  \{
 */

#ifndef _GL_PLAY_H
#define _GL_PLAY_H

#include <packetstream.h>
#include "../common/glc.h"

__PUBLIC int gl_play_init(glc_t *glc, ps_buffer_t *from, glc_ctx_i ctx, sem_t *finished);

#endif

/**  \} */
