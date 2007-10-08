/**
 * \file src/stream/file.c
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
#include "file.h"

/**
 * \addtogroup stream
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
	file->to = fopen(file->glc->stream_file, "w");
	
	if (!file->to) {
		fprintf(stderr, "glc: can't open %s\n", file->glc->stream_file);
		file->glc->flags |= GLC_CANCEL;
		ps_buffer_cancel(from);
		sem_post(&file->glc->signal[GLC_SIGNAL_FILE_FINISHED]);
		return EAGAIN;
	}
	
	if (!file->glc->info) {
		fprintf(stderr, "glc: stream info not available\n");
		file->glc->flags |= GLC_CANCEL;
		ps_buffer_cancel(from);
		sem_post(&file->glc->signal[GLC_SIGNAL_FILE_FINISHED]);
		return EINVAL;
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
}

void file_finish_callback(void *ptr, int err)
{
	struct file_private_s *file = (struct file_private_s *) ptr;
	
	if (err)
		fprintf(stderr, "file failed: %s (%d)\n", strerror(err), err);
	
	fclose(file->to);
	
	sem_post(&file->glc->signal[GLC_SIGNAL_FILE_FINISHED]);
	free(file);
}

int file_read_callback(glc_thread_state_t *state)
{
	struct file_private_s *file = (struct file_private_s *) state->ptr;
	glc_size_t glc_size = (glc_size_t) state->read_size;
	
	fwrite(&state->header, 1, GLC_MESSAGE_HEADER_SIZE, file->to);
	fwrite(&glc_size, 1, GLC_SIZE_SIZE, file->to);
	fwrite(state->read_data, 1, state->read_size, file->to);
	
	return 0;
}

int file_read(glc_t *glc, ps_buffer_t *to)
{
	glc_stream_info_t *info;
	glc_message_header_t header;
	size_t packet_size, mem_size = 1024 * 1024;
	FILE *from;
	ps_packet_t packet;
	char *mem, *dma;
	glc_size_t glc_ps;
	
	from = fopen(glc->stream_file, "r");
	if (from == NULL) {
		fprintf(stderr, "can't open %s\n", glc->stream_file);
		return -1;
	}
	
	info = (glc_stream_info_t *) malloc(sizeof(glc_stream_info_t));
	memset(info, 0, sizeof(glc_stream_info_t));
	fread(info, 1, GLC_STREAM_INFO_SIZE, from);
	
	if (info->signature != GLC_SIGNATURE) {
		fprintf(stderr, "file: signature does not match\n");
		goto err;
	}
	
	if (info->version != GLC_STREAM_VERSION) {
		fprintf(stderr, "file: unsupported stream version 0x%02x\n", glc->info->version);
		goto err;
	}
	
	if (info->name_size > 0)
		fseek(from, info->name_size, SEEK_CUR);
	if (info->date_size > 0)
		fseek(from, info->date_size, SEEK_CUR);

	free(info);
	
	ps_packet_init(&packet, to);
	mem = malloc(mem_size);

	do {
		fread(&header, 1, GLC_MESSAGE_HEADER_SIZE, from);
		fread(&glc_ps, 1, sizeof(glc_size_t), from);
		packet_size = glc_ps;
		
		if (ps_packet_open(&packet, PS_PACKET_WRITE))
			break;
		
		if (ps_packet_write(&packet, &header, GLC_MESSAGE_HEADER_SIZE))
			break;
		
		if (packet_size > mem_size) {
			mem_size = packet_size;
			mem = (char *) realloc(mem, mem_size);
		}
		
		if (!ps_packet_dma(&packet, (void *) &dma, packet_size, 0))
			fread(dma, 1, packet_size, from);
		else {
			fread(mem, 1, packet_size, from);
			if (ps_packet_write(&packet, mem, packet_size))
				break;
		}
		
		ps_packet_close(&packet);
	} while ((header.type != GLC_MESSAGE_CLOSE) && (!(glc->flags & GLC_CANCEL)));
	
	free(mem);
	ps_packet_destroy(&packet);
	fclose(from);
	
	return 0;

err:
	fprintf(stderr, "unsupported file %s\n", glc->stream_file);
	fclose(from);
	return -1;
}


/**  \} */
/**  \} */
