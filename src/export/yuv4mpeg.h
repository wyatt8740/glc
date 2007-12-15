/**
 * \file src/export/yuv4mpeg.h
 * \brief yuv4mpeg output
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup yuv4mpeg
 *  \{
 */

#ifndef _YUV4MPEG_H
#define _YUV4MPEG_H

__PUBLIC void *yuv4mpeg_init(glc_t *glc, ps_buffer_t *from);
__PUBLIC int yuv4mpeg_wait(void *yuv4mpegpriv);

#endif

/**  \} */
