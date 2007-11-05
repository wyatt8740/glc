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

int gamma_capture(glc_t *glc, ps_buffer_t *to, float red, float green, float blue)
{
	ps_packet_t packet;
	int ret = 0;
	glc_message_header_t msg_hdr;
	glc_gamma_message_t msg;

	msg_hdr.type = GLC_MESSAGE_GAMMA;
	msg.red = red;
	msg.green = green;
	msg.blue = blue;

	if ((ret = ps_packet_init(&packet, to)))
		goto err;
	if ((ret = ps_packet_open(&packet, PS_PACKET_WRITE)))
		goto err;
	if ((ret = ps_packet_write(&packet, &msg_hdr, GLC_MESSAGE_HEADER_SIZE)))
		goto err;
	if ((ret = ps_packet_write(&packet, &msg, GLC_GAMMA_MESSAGE_SIZE)))
		goto err;
	if ((ret = ps_packet_close(&packet)))
		goto err;
	if ((ret = ps_packet_destroy(&packet)))
		goto err;

	return 0;
err:
	ps_packet_cancel(&packet);

	util_log(glc, GLC_ERROR, "gamma",  "can't write gamma correction information to buffer: %s (%d)\n",
		 strerror(ret), ret);
	return ret;
}

/**  \} */
/**  \} */
