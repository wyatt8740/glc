/**
 * \file src/core/file.c
 * \brief file io
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* file.c -- file io
 * Copyright (C) 2007 Pyry Haulos
 * For conditions of distribution and use, see copyright notice in glc.h
 */

#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <packetstream.h>
#include <errno.h>

#include "../common/glc.h"
#include "../common/thread.h"
#include "../common/util.h"
#include "file.h"

/**
 * \addtogroup core
 *  \{
 */

/**
 * \defgroup file file io
 *  \{
 */

struct file_private_s {
	glc_t *glc;
	glc_thread_t thread;
	FILE *to;
};

void file_finish_callback(void *ptr, int err);
int file_read_callback(glc_thread_state_t *state);

int file_init(glc_t *glc, ps_buffer_t *from)
{
	struct file_private_s *file;

	file = malloc(sizeof(struct file_private_s));
	memset(file, 0, sizeof(struct file_private_s));

	file->glc = glc;

	util_log(file->glc, GLC_INFORMATION, "file",
		 "opening %s for stream", file->glc->stream_file);

	file->to = fopen(file->glc->stream_file, "w");

	if (!file->to) {
		util_log(file->glc, GLC_ERROR, "file", "can't open %s", file->glc->stream_file);
		goto cancel;
	}

	if (!file->glc->info) {
		util_log(file->glc, GLC_ERROR, "file", "stream info not available");
		goto cancel;
	}

	fwrite(file->glc->info, 1, GLC_STREAM_INFO_SIZE, file->to);
	fwrite(file->glc->info_name, 1, file->glc->info->name_size, file->to);
	fwrite(file->glc->info_date, 1, file->glc->info->date_size, file->to);

	file->thread.flags = GLC_THREAD_READ;
	file->thread.ptr = file;
	file->thread.read_callback = &file_read_callback;
	file->thread.finish_callback = &file_finish_callback;
	file->thread.threads = 1;

	return glc_thread_create(glc, &file->thread, from, NULL);

cancel:
	file->glc->flags |= GLC_CANCEL;
	ps_buffer_cancel(from);
	sem_post(&file->glc->signal[GLC_SIGNAL_FILE_FINISHED]);
	return EAGAIN;
}

void file_finish_callback(void *ptr, int err)
{
	struct file_private_s *file = (struct file_private_s *) ptr;

	if (err)
		util_log(file->glc, GLC_ERROR, "file", "%s (%d)", strerror(err), err);

	if (fclose(file->to))
		util_log(file->glc, GLC_ERROR, "file",
			 "can't close file: %s (%d)", strerror(errno), errno);

	sem_post(&file->glc->signal[GLC_SIGNAL_FILE_FINISHED]);
	free(file);
}

int file_read_callback(glc_thread_state_t *state)
{
	struct file_private_s *file = (struct file_private_s *) state->ptr;
	glc_container_message_t *container;
	glc_size_t glc_size;

	if (state->header.type == GLC_MESSAGE_CONTAINER) {
		container = (glc_container_message_t *) state->read_data;

		if (fwrite(&container->header, 1, GLC_MESSAGE_HEADER_SIZE, file->to)
		    != GLC_MESSAGE_HEADER_SIZE)
			return ENOSTR;
		if (fwrite(&container->size, 1, GLC_SIZE_SIZE, file->to) != GLC_SIZE_SIZE)
			return ENOSTR;
		if (fwrite(&state->read_data[GLC_CONTAINER_MESSAGE_SIZE], 1, container->size, file->to)
		    != container->size)
			return ENOSTR;
	} else {
		if (fwrite(&state->header, 1, GLC_MESSAGE_HEADER_SIZE, file->to) != GLC_MESSAGE_HEADER_SIZE)
			return ENOSTR;
		glc_size = state->read_size;
		if (fwrite(&glc_size, 1, GLC_SIZE_SIZE, file->to) != GLC_SIZE_SIZE)
			return ENOSTR;
		if (fwrite(state->read_data, 1, state->read_size, file->to) != state->read_size)
			return ENOSTR;
	}

	return 0;
}

int file_read(glc_t *glc, ps_buffer_t *to)
{
	int ret = 0;
	glc_stream_info_t *info;
	glc_message_header_t header;
	size_t packet_size;
	FILE *from;
	ps_packet_t packet;
	char *dma;
	glc_size_t glc_ps;

	from = fopen(glc->stream_file, "r");
	if (from == NULL) {
		util_log(glc, GLC_ERROR, "file",
			 "can't open %s: %s (%d)", glc->stream_file, strerror(errno), errno);
		return -1;
	}

	info = (glc_stream_info_t *) malloc(sizeof(glc_stream_info_t));
	memset(info, 0, sizeof(glc_stream_info_t));
	fread(info, 1, GLC_STREAM_INFO_SIZE, from);

	if (info->signature != GLC_SIGNATURE) {
		util_log(glc, GLC_ERROR, "file", "signature does not match");
		goto err;
	}

	if (info->version != GLC_STREAM_VERSION) {
		util_log(glc, GLC_ERROR, "file",
			 "unsupported stream version 0x%02x", glc->info->version);
		goto err;
	}

	if (info->name_size > 0)
		fseek(from, info->name_size, SEEK_CUR);
	if (info->date_size > 0)
		fseek(from, info->date_size, SEEK_CUR);

	free(info);

	ps_packet_init(&packet, to);

	do {
		if (fread(&header, 1, GLC_MESSAGE_HEADER_SIZE, from) != GLC_MESSAGE_HEADER_SIZE)
			goto send_eof;
		if (fread(&glc_ps, 1, GLC_SIZE_SIZE, from) != GLC_SIZE_SIZE)
			goto send_eof;

		packet_size = glc_ps;

		if ((ret = ps_packet_open(&packet, PS_PACKET_WRITE)))
			goto err;
		if ((ret = ps_packet_write(&packet, &header, GLC_MESSAGE_HEADER_SIZE)))
			goto err;
		if ((ret = ps_packet_dma(&packet, (void *) &dma, packet_size, PS_ACCEPT_FAKE_DMA)))
			goto err;

		if (fread(dma, 1, packet_size, from) != packet_size)
			goto read_fail;

		if ((ret = ps_packet_close(&packet)))
			goto err;
	} while ((header.type != GLC_MESSAGE_CLOSE) && (!(glc->flags & GLC_CANCEL)));

finish:
	ps_packet_destroy(&packet);
	fclose(from);

	return 0;

send_eof:
	header.type = GLC_MESSAGE_CLOSE;
	ps_packet_open(&packet, PS_PACKET_WRITE);
	ps_packet_write(&packet, &header, GLC_MESSAGE_HEADER_SIZE);
	ps_packet_close(&packet);

	util_log(glc, GLC_ERROR, "file", "unexpected EOF");
	goto finish;

read_fail:
	ret = EBADMSG;
err:
	if (ret == EINTR)
		goto finish; /* just cancel */

	util_log(glc, GLC_ERROR, "file", "%s (%d)", strerror(ret), ret);
	fclose(from);
	ps_buffer_cancel(to);
	return ret;
}


/**  \} */
/**  \} */
