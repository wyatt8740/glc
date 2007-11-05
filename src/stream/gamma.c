/**
 * \file src/stream/gamma.c
 * \brief gamma correction
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* gamma.c -- gamma correction
 * Copyright (C) 2007 Pyry Haulos
 * For conditions of distribution and use, see copyright notice in glc.h
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <packetstream.h>
#include <errno.h>

#include "../common/glc.h"
#include "../common/thread.h"
#include "../common/util.h"
#include "gamma.h"

/**
 * \addtogroup stream
 *  \{
 */

/**
 * \defgroup gamma gamma correction
 *  \{
 */

/**  \} */
/**  \} */
