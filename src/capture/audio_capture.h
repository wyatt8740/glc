/**
 * \file src/capture/audio_capture.h
 * \brief audio capture
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup audio_capture
 *  \{
 */

#ifndef _AUDIO_CAPTURE_H
#define _AUDIO_CAPTURE_H

#include <packetstream.h>
#include "../common/glc.h"

__PUBLIC void *audio_capture_init(glc_t *glc, ps_buffer_t *to,
				  const char *device,
				  unsigned int rate,
				  unsigned int channels);
__PUBLIC int audio_capture_pause(void *audiopriv);
__PUBLIC int audio_capture_resume(void *audiopriv);
__PUBLIC int audio_capture_close(void *audiopriv);

#endif

/**  \} */
