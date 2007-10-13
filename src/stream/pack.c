/**
 * \file src/stream/pack.c
 * \brief stream compression
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* pack.c -- stream compression
 * Copyright (C) 2007 Pyry Haulos
 * For conditions of distribution and use, see copyright notice in glc.h
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../common/glc.h"
#include "../common/thread.h"
#include "../common/util.h"
#include "pack.h"

#ifdef __MINILZO
# include <minilzo.h>
# define __lzo_compress lzo1x_1_compress
# define __lzo_decompress lzo1x_decompress
# define __lzo_mem(size) size + (size / 16) + 64 + 3
# define __lzo_wrk_mem LZO1X_1_MEM_COMPRESS
#else
# include <lzo/lzo1x.h>
# define __lzo_compress lzo1x_1_11_compress
# define __lzo_decompress lzo1x_decompress
# define __lzo_mem(size) size + (size / 16) + 64 + 3
# define __lzo_wrk_mem LZO1X_1_11_MEM_COMPRESS
#endif

/**
 * \addtogroup stream
 *  \{
 */

/**
 * \defgroup pack stream compression
 *  \{
 */

struct pack_private_s {
	glc_t *glc;
	glc_thread_t thread;
	char *lzo_wrk_mem;
	size_t lzo_compress_min;
	
	glc_lzo_header_t lzo_header;
};

int pack_read_callback(glc_thread_state_t *state);
int pack_write_callback(glc_thread_state_t *state);
void pack_finish_callback(void *ptr, int err);

int unpack_read_callback(glc_thread_state_t *state);
int unpack_write_callback(glc_thread_state_t *state);
void unpack_finish_callback(void *ptr, int err);

int pack_init(glc_t *glc, ps_buffer_t *from, ps_buffer_t *to)
{
	struct pack_private_s *pack = malloc(sizeof(struct pack_private_s));
	memset(pack, 0, sizeof(struct pack_private_s));
	
	pack->glc = glc;
	pack->lzo_wrk_mem = (char *) malloc(__lzo_wrk_mem);
	pack->lzo_compress_min = 100;
	
	pack->thread.flags = GLC_THREAD_WRITE | GLC_THREAD_READ;
	pack->thread.ptr = pack;
	pack->thread.read_callback = &pack_read_callback;
	pack->thread.write_callback = &pack_write_callback;
	pack->thread.finish_callback = &pack_finish_callback;
	pack->thread.threads = 1; /* can't currently take advantage of threading */
	
	lzo_init();
	return glc_thread_create(glc, &pack->thread, from, to);
}

void pack_finish_callback(void *ptr, int err)
{
	struct pack_private_s *pack = (struct pack_private_s *) ptr;
	
	if (err)
		fprintf(stderr, "pack failed: %s (%d)\n", strerror(err), err);
	
	sem_post(&pack->glc->signal[GLC_SIGNAL_PACK_FINISHED]);
	free(pack->lzo_wrk_mem);
	free(pack);
}

int pack_read_callback(glc_thread_state_t *state)
{
	struct pack_private_s *pack = (struct pack_private_s *) state->ptr;
	
	if (state->read_size > pack->lzo_compress_min) {
		memcpy(&pack->lzo_header.header, &state->header, GLC_MESSAGE_HEADER_SIZE);
		state->header.type = GLC_MESSAGE_LZO;
		
		state->write_size = GLC_LZO_HEADER_SIZE + __lzo_mem(state->read_size);
		state->flags |= GLC_THREAD_STATE_UNKNOWN_FINAL_SIZE; /* don't acquire dma */
	} else
		state->flags |= GLC_THREAD_COPY; /* too small, wont compress */

	return 0;
}

int pack_write_callback(glc_thread_state_t *state)
{
	struct pack_private_s *pack = (struct pack_private_s *) state->ptr;

	__lzo_compress((unsigned char *) state->read_data, state->read_size,
		       (unsigned char *) &state->write_data[GLC_LZO_HEADER_SIZE], &state->write_size,
		       pack->lzo_wrk_mem);

	pack->lzo_header.size = (glc_size_t) state->read_size;
	memcpy(state->write_data, &pack->lzo_header, GLC_LZO_HEADER_SIZE);

	state->write_size += GLC_LZO_HEADER_SIZE;

	return 0;
}

int unpack_init(glc_t *glc, ps_buffer_t *from, ps_buffer_t *to)
{
	struct pack_private_s *pack = malloc(sizeof(struct pack_private_s));
	memset(pack, 0, sizeof(struct pack_private_s));
	
	pack->glc = glc;
	
	pack->thread.flags = GLC_THREAD_WRITE | GLC_THREAD_READ;
	pack->thread.ptr = pack;
	pack->thread.read_callback = &unpack_read_callback;
	pack->thread.write_callback = &unpack_write_callback;
	pack->thread.finish_callback = &unpack_finish_callback;
	pack->thread.threads = util_cpus();
	
	lzo_init();
	return glc_thread_create(glc, &pack->thread, from, to);
}

void unpack_finish_callback(void *ptr, int err)
{
	struct pack_private_s *pack = (struct pack_private_s *) ptr;
	
	if (err)
		fprintf(stderr, "unpack failed: %s (%d)\n", strerror(err), err);
	
	sem_post(&pack->glc->signal[GLC_SIGNAL_PACK_FINISHED]);
	free(pack);
}

int unpack_read_callback(glc_thread_state_t *state)
{
	struct pack_private_s *pack = (struct pack_private_s *) state->ptr;

	if (state->header.type == GLC_MESSAGE_LZO) {
		memcpy(&pack->lzo_header, state->read_data, GLC_LZO_HEADER_SIZE);

		state->write_size = pack->lzo_header.size;
		memcpy(&state->header, &pack->lzo_header.header, GLC_MESSAGE_HEADER_SIZE);
	} else
		state->flags |= GLC_THREAD_COPY;

	return 0;
}

int unpack_write_callback(glc_thread_state_t *state)
{
	__lzo_decompress((unsigned char *) &state->read_data[GLC_LZO_HEADER_SIZE],
			 state->read_size - GLC_LZO_HEADER_SIZE,
			 (unsigned char *) state->write_data, &state->write_size,
			 NULL);
	return 0;
}


/**  \} */
/**  \} */
