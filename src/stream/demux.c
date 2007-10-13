/**
 * \file src/stream/demux.c
 * \brief audio/picture stream demuxer
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* demux.c -- audio/picture stream demuxer
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

/**
 * \addtogroup stream
 *  \{
 */

/**
 * \defgroup demux audio/picture stream demuxer
 *  \{
 */

struct demux_private_s {
	glc_t *glc;
	ps_buffer_t *from, *audio, *picture;

	pthread_t thread;
};

int demux_close(struct demux_private_s *demux);
void *demux_thread(void *argptr);

int demux_init(glc_t *glc, ps_buffer_t *from, ps_buffer_t *audio, ps_buffer_t *picture)
{
	struct demux_private_s *demux = (struct demux_private_s *) malloc(sizeof(struct demux_private_s));
	memset(demux, 0, sizeof(struct demux_private_s));

	demux->glc = glc;
	demux->from = from;
	demux->audio = audio;
	demux->picture = picture;

	return pthread_create(&demux->thread, NULL, demux_thread, demux);
}

int demux_close(struct demux_private_s *demux)
{
	sem_post(&demux->glc->signal[GLC_SIGNAL_DEMUX_FINISHED]);
	free(demux);
	return 0;
}

void *demux_thread(void *argptr)
{
	struct demux_private_s *demux = (struct demux_private_s *) argptr;
	glc_message_header_t msg_hdr;
	size_t data_size;
	char *data;
	int ret = 0;
	
	ps_packet_t read, audio, picture;
	
	if ((ret = ps_packet_init(&read, demux->from)))
		goto err;
	if ((ps_packet_init(&audio, demux->audio)))
		goto err;
	if ((ps_packet_init(&picture, demux->picture)))
		goto err;
	
	do {
		if ((ret = ps_packet_open(&read, PS_PACKET_READ)))
			goto err;
		
		if ((ret = ps_packet_read(&read, &msg_hdr, GLC_MESSAGE_HEADER_SIZE)))
			goto err;
		if ((ret = ps_packet_getsize(&read, &data_size)))
			goto err;
		data_size -= GLC_MESSAGE_HEADER_SIZE;
		if ((ret = ps_packet_dma(&read, (void *) &data, data_size, PS_ACCEPT_FAKE_DMA)))
			goto err;

		if ((msg_hdr.type == GLC_MESSAGE_CLOSE) |
		    (msg_hdr.type == GLC_MESSAGE_PICTURE) |
		    (msg_hdr.type == GLC_MESSAGE_CTX)) {
			if ((ret = ps_packet_open(&picture, PS_PACKET_WRITE)))
				goto err;
			if ((ret = ps_packet_write(&picture, &msg_hdr, GLC_MESSAGE_HEADER_SIZE)))
				goto err;
			if ((ret = ps_packet_write(&picture, data, data_size)))
				goto err;
			ps_packet_close(&picture);
		}

		if ((msg_hdr.type == GLC_MESSAGE_CLOSE) |
		    (msg_hdr.type == GLC_MESSAGE_AUDIO_FORMAT) |
		    (msg_hdr.type == GLC_MESSAGE_AUDIO)) {
			if ((ret = ps_packet_open(&audio, PS_PACKET_WRITE)))
				goto err;
			if ((ret = ps_packet_write(&audio, &msg_hdr, GLC_MESSAGE_HEADER_SIZE)))
				goto err;
			if ((ret = ps_packet_write(&audio, data, data_size)))
				goto err;
			ps_packet_close(&audio);
		}

		ps_packet_close(&read);
	} while ((!(demux->glc->flags & GLC_CANCEL)) && (msg_hdr.type != GLC_MESSAGE_CLOSE));

finish:
	ps_packet_destroy(&read);
	ps_packet_destroy(&audio);
	ps_packet_destroy(&picture);
	
	if (demux->glc->flags & GLC_CANCEL) {
		ps_buffer_cancel(demux->from);

		/* make sure both 'clients' finish */
		ps_buffer_cancel(demux->audio);
		ps_buffer_cancel(demux->picture);
	}

	demux_close(demux);
	return NULL;
err:
	if (ret == EINTR) /* just _cancel() */
		ret = 0;
	else
		fprintf(stderr, "demux(): %s (%d)\n", strerror(ret), ret);
	goto finish;
}


/**  \} */
/**  \} */
