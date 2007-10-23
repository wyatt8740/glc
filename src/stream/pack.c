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
#include <errno.h>
#include <stdint.h>

#include "../common/glc.h"
#include "../common/thread.h"
#include "../common/util.h"
#include "pack.h"

/**
 * \addtogroup stream
 *  \{
 */

/**
 * \defgroup pack stream compression
 *  \{
 */

#ifdef __MINILZO
# include <minilzo.h>
# define __lzo_compress lzo1x_1_compress
# define __lzo_decompress lzo1x_decompress
# define __lzo_worstcase(size) size + (size / 16) + 64 + 3
# define __lzo_wrk_mem LZO1X_1_MEM_COMPRESS
#else
# include <lzo/lzo1x.h>
# define __lzo_compress lzo1x_1_11_compress
# define __lzo_decompress lzo1x_decompress
# define __lzo_worstcase(size) size + (size / 16) + 64 + 3
# define __lzo_wrk_mem LZO1X_1_11_MEM_COMPRESS
#endif

#define __quicklz_worstcase(size) (size) + ((size) / 8) + 1
#define __quicklz_hashtable sizeof(uintptr_t) * 4096

struct pack_private_s {
	glc_t *glc;
	glc_thread_t thread;
	size_t compress_min;
};

int quicklz_compress(const unsigned char *from, unsigned char *to,
		     size_t uncompressed_size, size_t *compressed_size,
		     uintptr_t *hashtable);
int quicklz_decompress(const unsigned char *from, unsigned char *to,
		       size_t compressed_size);

int pack_thread_create_callback(void *ptr, void **threadptr);
void pack_thread_finish_callback(void *ptr, void *threadptr, int err);
int pack_read_callback(glc_thread_state_t *state);
int pack_quicklz_write_callback(glc_thread_state_t *state);
int pack_lzo_write_callback(glc_thread_state_t *state);
void pack_finish_callback(void *ptr, int err);

int unpack_read_callback(glc_thread_state_t *state);
int unpack_write_callback(glc_thread_state_t *state);
void unpack_finish_callback(void *ptr, int err);

int pack_init(glc_t *glc, ps_buffer_t *from, ps_buffer_t *to)
{
	struct pack_private_s *pack = malloc(sizeof(struct pack_private_s));
	memset(pack, 0, sizeof(struct pack_private_s));

	pack->glc = glc;
	pack->compress_min = 1024;

	pack->thread.flags = GLC_THREAD_WRITE | GLC_THREAD_READ;
	pack->thread.ptr = pack;
	pack->thread.thread_create_callback = &pack_thread_create_callback;
	pack->thread.thread_finish_callback = &pack_thread_finish_callback;
	pack->thread.read_callback = &pack_read_callback;
	pack->thread.finish_callback = &pack_finish_callback;
	pack->thread.threads = 1; /* compression can't currently take advantage of threading */

	if (pack->glc->flags & GLC_COMPRESS_QUICKLZ)
		pack->thread.write_callback = &pack_quicklz_write_callback;
	else if (pack->glc->flags & GLC_COMPRESS_LZO) {
		pack->thread.write_callback = &pack_lzo_write_callback;
		lzo_init();
	} else {
		fprintf(stderr, "pack: no compression selected\n");
		return EINVAL;
	}

	return glc_thread_create(glc, &pack->thread, from, to);
}

void pack_finish_callback(void *ptr, int err)
{
	struct pack_private_s *pack = (struct pack_private_s *) ptr;

	if (err)
		fprintf(stderr, "pack failed: %s (%d)\n", strerror(err), err);

	sem_post(&pack->glc->signal[GLC_SIGNAL_PACK_FINISHED]);
	free(pack);
}

int pack_thread_create_callback(void *ptr, void **threadptr)
{
	struct pack_private_s *pack = ptr;

	if (pack->glc->flags & GLC_COMPRESS_QUICKLZ)
		*threadptr = malloc(__quicklz_hashtable);
	else
		*threadptr = malloc(__lzo_wrk_mem);

	return 0;
}

void pack_thread_finish_callback(void *ptr, void *threadptr, int err)
{
	if (threadptr)
		free(threadptr);
}

int pack_read_callback(glc_thread_state_t *state)
{
	struct pack_private_s *pack = (struct pack_private_s *) state->ptr;

	/* compress only audio and pictures */
	if ((state->read_size > pack->compress_min) &&
	    ((state->header.type == GLC_MESSAGE_PICTURE) |
	     (state->header.type == GLC_MESSAGE_AUDIO))) {
		if (pack->glc->flags & GLC_COMPRESS_QUICKLZ)
			state->write_size = GLC_QUICKLZ_HEADER_SIZE
					    + __quicklz_worstcase(state->read_size);
		else
			state->write_size = GLC_LZO_HEADER_SIZE
			                    + __lzo_worstcase(state->read_size);
		state->flags |= GLC_THREAD_STATE_UNKNOWN_FINAL_SIZE;
	} else
		state->flags |= GLC_THREAD_COPY;

	return 0;
}

int pack_lzo_write_callback(glc_thread_state_t *state)
{
	glc_lzo_header_t *lzo_header = (glc_lzo_header_t *) state->write_data;

	__lzo_compress((unsigned char *) state->read_data, state->read_size,
		       (unsigned char *) &state->write_data[GLC_LZO_HEADER_SIZE],
		       (lzo_uintp) &state->write_size,
		       state->threadptr);

	lzo_header->size = (glc_size_t) state->read_size;
	memcpy(&lzo_header->header, &state->header, GLC_MESSAGE_HEADER_SIZE);
	state->header.type = GLC_MESSAGE_LZO;

	state->write_size += GLC_LZO_HEADER_SIZE;

	return 0;
}

int pack_quicklz_write_callback(glc_thread_state_t *state)
{
	glc_quicklz_header_t *quicklz_header = (glc_quicklz_header_t *) state->write_data;

	quicklz_compress((const unsigned char *) state->read_data,
			 (unsigned char *) &state->write_data[GLC_QUICKLZ_HEADER_SIZE],
			 state->read_size, &state->write_size,
			 state->threadptr);

	quicklz_header->size = (glc_size_t) state->read_size;
	memcpy(&quicklz_header->header, &state->header, GLC_MESSAGE_HEADER_SIZE);
	state->header.type = GLC_MESSAGE_QUICKLZ;

	state->write_size += GLC_QUICKLZ_HEADER_SIZE;

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
	if (state->header.type == GLC_MESSAGE_LZO)
		state->write_size = ((glc_lzo_header_t *) state->read_data)->size;
	else if (state->header.type == GLC_MESSAGE_QUICKLZ)
		state->write_size = ((glc_quicklz_header_t *) state->read_data)->size;
	else
		state->flags |= GLC_THREAD_COPY;

	return 0;
}

int unpack_write_callback(glc_thread_state_t *state)
{
	if (state->header.type == GLC_MESSAGE_LZO) {
		memcpy(&state->header, &((glc_lzo_header_t *) state->read_data)->header,
		       GLC_MESSAGE_HEADER_SIZE);
		__lzo_decompress((unsigned char *) &state->read_data[GLC_LZO_HEADER_SIZE],
				state->read_size - GLC_LZO_HEADER_SIZE,
				(unsigned char *) state->write_data,
				(lzo_uintp) &state->write_size,
				NULL);
	} else if (state->header.type == GLC_MESSAGE_QUICKLZ) {
		memcpy(&state->header, &((glc_quicklz_header_t *) state->read_data)->header,
		       GLC_MESSAGE_HEADER_SIZE);
		quicklz_decompress((const unsigned char *) &state->read_data[GLC_QUICKLZ_HEADER_SIZE],
				   (unsigned char *) state->write_data,
				   state->write_size);
	} else {
		fprintf(stderr, "unsupported compression: 0x%02x\n", state->header.type);
		return ENOTSUP;
	}

	return 0;
}

/**  \} */
/**  \} */
