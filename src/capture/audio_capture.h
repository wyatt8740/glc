/* audio_capture.h -- audio capture
 * Copyright (C) 2007 Pyry Haulos
 * For conditions of distribution and use, see copyright notice in glc.h
 */

#ifndef _AUDIO_CAPTURE_H
#define _AUDIO_CAPTURE_H

#include <packetstream.h>
#include "../common/glc.h"

/**
 * \addtogroup capture
 *  \{
 */

__PUBLIC void *audio_capture_init(glc_t *glc, ps_buffer_t *to,
				  const char *device,
				  unsigned int rate,
				  unsigned int channels);
__PUBLIC int audio_capture_close(void *audiopriv);

/**  \} */

#endif
