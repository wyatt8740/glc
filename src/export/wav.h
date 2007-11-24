/**
 * \file src/export/wav.h
 * \brief export audio to wav
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* wav.h -- export audio to wav
 * Copyright (C) 2007 Pyry Haulos
 * For conditions of distribution and use, see copyright notice in glc.h
 */

#ifndef _WAV_H
#define _WAV_H

#include <packetstream.h>
#include "../common/glc.h"

/**
 * \addtogroup wav
 *  \{
 */

__PUBLIC int wav_init(glc_t *glc, ps_buffer_t *from);

/**  \} */

#endif
