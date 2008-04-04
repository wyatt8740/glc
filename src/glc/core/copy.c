/**
 * \file glc/core/copy.c
 * \brief generic stream demuxer
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup copy
 *  \{
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <packetstream.h>
#include <errno.h>

#include <glc/common/glc.h>
#include <glc/common/core.h>
#include <glc/common/log.h>
#include <glc/common/state.h>
#include <glc/common/util.h>

#include "copy.h"

struct copy_target_s {
	ps_buffer_t *buffer;
	ps_packet_t packet;
	glc_message_type_t type;

	struct copy_target_s *next;
};

struct copy_s {
	glc_t *glc;
	ps_buffer_t *from;

	pthread_t copy_thread;
	int running;

	struct copy_target_s *copy_target;
};

void *copy_thread(void *argptr);

int copy_init(copy_t *copy, glc_t *glc)
{
	*copy = (copy_t) malloc(sizeof(struct copy_s));
	memset(*copy, 0, sizeof(struct copy_s));

	(*copy)->glc = glc;

	return 0;
}

int copy_destroy(copy_t copy)
{
	struct copy_target_s *del;

	while (copy->copy_target != NULL) {
		del = copy->copy_target;
		copy->copy_target = copy->copy_target->next;

		ps_packet_destroy(&del->packet);
		free(del);
	}

	free(copy);
	return 0;
}

int copy_add(copy_t copy, ps_buffer_t *target, glc_message_type_t type)
{
	struct copy_target_s *newtarget = malloc(sizeof(struct copy_target_s));
	memset(newtarget, 0, sizeof(struct copy_target_s));

	newtarget->buffer = target;
	newtarget->type = type;

	/** \todo one packet per buffer */
	ps_packet_init(&newtarget->packet, newtarget->buffer);

	newtarget->next = copy->copy_target;
	copy->copy_target = newtarget;

	return 0;
}

int copy_process_start(copy_t copy, ps_buffer_t *from)
{
	int ret;
	pthread_attr_t attr;

	if (copy->running)
		return EALREADY;

	copy->from = from;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	if ((ret = pthread_create(&copy->copy_thread, &attr, copy_thread, copy)))
		return ret;

	copy->running = 1;

	pthread_attr_destroy(&attr);
	return 0;
}

int copy_process_wait(copy_t copy)
{
	if (!copy->running)
		return EAGAIN;

	pthread_join(copy->copy_thread, NULL);
	copy->running = 0;

	return 0;
}

void *copy_thread(void *argptr)
{
	copy_t copy = (copy_t) argptr;
	struct copy_target_s *target;
	glc_message_header_t msg_hdr;
	size_t data_size;
	void *data;
	int ret = 0;

	ps_packet_t read;

	if ((ret = ps_packet_init(&read, copy->from)))
		goto err;

	do {
		if ((ret = ps_packet_open(&read, PS_PACKET_READ)))
			goto err;

		if ((ret = ps_packet_read(&read, &msg_hdr, sizeof(glc_message_header_t))))
			goto err;
		if ((ret = ps_packet_getsize(&read, &data_size)))
			goto err;
		data_size -= sizeof(glc_message_header_t);
		if ((ret = ps_packet_dma(&read, &data, data_size, PS_ACCEPT_FAKE_DMA)))
			goto err;

		target = copy->copy_target;
		while (target != NULL) {
			if ((target->type == 0) |
			    (target->type == msg_hdr.type)) {
				if ((ret = ps_packet_open(&target->packet, PS_PACKET_WRITE)))
					goto err;
				if ((ret = ps_packet_write(&target->packet, &msg_hdr,
							   sizeof(glc_message_header_t))))
					goto err;
				if ((ret = ps_packet_write(&target->packet, data, data_size)))
					goto err;
				if ((ret = ps_packet_close(&target->packet)))
					goto err;
			}
			target = target->next;
		}

		ps_packet_close(&read);
	} while ((!glc_state_test(copy->glc, GLC_STATE_CANCEL)) &&
		 (msg_hdr.type != GLC_MESSAGE_CLOSE));

finish:
	ps_packet_destroy(&read);

	if (glc_state_test(copy->glc, GLC_STATE_CANCEL)) {
		ps_buffer_cancel(copy->from);

		target = copy->copy_target;
		while (target != NULL) {
			ps_buffer_cancel(target->buffer);
			target = target->next;
		}
	}

	return NULL;
err:
	if (ret != EINTR) {
		glc_log(copy->glc, GLC_ERROR, "copy", "%s (%d)", strerror(ret), ret);
		glc_state_set(copy->glc, GLC_STATE_CANCEL);
	}
	goto finish;
}

/**  \} */
