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
# define __lzo_mem(size) size + (size / 16) + 64 + 3
# define __lzo_wrk_mem LZO1X_1_MEM_COMPRESS
#else
# include <lzo/lzo1x.h>
# define __lzo_compress lzo1x_1_11_compress
# define __lzo_decompress lzo1x_decompress
# define __lzo_mem(size) size + (size / 16) + 64 + 3
# define __lzo_wrk_mem LZO1X_1_11_MEM_COMPRESS
#endif

#define __quicklz_worstcase(size) (size) + ((size) / 8) + 1

struct pack_private_s {
	glc_t *glc;
	glc_thread_t thread;
	/* char *lzo_wrk_mem; */
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
	/* pack->lzo_wrk_mem = (char *) malloc(__lzo_wrk_mem); */
	pack->compress_min = 100;

	pack->thread.flags = GLC_THREAD_WRITE | GLC_THREAD_READ;
	pack->thread.ptr = pack;
	pack->thread.thread_create_callback = &pack_thread_create_callback;
	pack->thread.thread_finish_callback = &pack_thread_finish_callback;
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
	/*free(pack->lzo_wrk_mem);*/
	free(pack);
}

int pack_thread_create_callback(void *ptr, void **threadptr)
{
	*threadptr = malloc(sizeof(uintptr_t) * 4096);
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

	/* currently only pictures compress well */
	if ((state->read_size > pack->compress_min) &&
	    (state->header.type == GLC_MESSAGE_PICTURE)) {
		state->write_size = GLC_QUICKLZ_HEADER_SIZE + __quicklz_worstcase(state->read_size);
		state->flags |= GLC_THREAD_STATE_UNKNOWN_FINAL_SIZE;
	} else
		state->flags |= GLC_THREAD_COPY;

	return 0;
}

/*int pack_write_callback(glc_thread_state_t *state)
{
	struct pack_private_s *pack = (struct pack_private_s *) state->ptr;
	glc_lzo_header_t *lzo_header = (glc_quicklz_header_t *) state->write_data;

	__lzo_compress((unsigned char *) state->read_data, state->read_size,
		       (unsigned char *) &state->write_data[GLC_LZO_HEADER_SIZE], &state->write_size,
		       pack->lzo_wrk_mem);

	lzo_header->size = (glc_size_t) state->read_size;
	memcpy(&lzo_header->header, &state->header, GLC_MESSAGE_HEADER_SIZE);
	state->header.type = GLC_MESSAGE_LZO;

	state->write_size += GLC_LZO_HEADER_SIZE;

	return 0;
}*/

int pack_write_callback(glc_thread_state_t *state)
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
				(unsigned char *) state->write_data, &state->write_size,
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

#define u8ptr(ptr) ((u_int8_t *) ptr)
#define u8(ptr) *u8ptr(ptr)
#define u16ptr(ptr) ((u_int16_t *) (ptr))
#define u16(ptr) *u16ptr(ptr)
#define u32ptr(ptr) ((u_int32_t *) (ptr))
#define u32(ptr) *u32ptr(ptr)
#define quicklz_hash(val) (((val) >> 12) ^ (val)) & 0x0fff

static void quicklz_expand(uintptr_t to, uintptr_t from, size_t len)
{
	uintptr_t end = to + len;

	if (from + len > to) {
		while (to < end)
			u8(to++) = u8(from++);
	} else
		memcpy((void *) to, (void *) from, len);
}

int quicklz_compress(const unsigned char *_from, unsigned char *_to,
		     size_t uncompressed_size, size_t *compressed_size,
		     uintptr_t *hashtable)
{
	unsigned char counter = 0;
	unsigned char *cbyte;
	uintptr_t from, to, orig, len, offs, end;
	unsigned int i;
	unsigned char val;
	unsigned short hash;

	from = (uintptr_t) _from;
	to = (uintptr_t) _to;
	end = from + uncompressed_size;

	cbyte = u8ptr(to++);
	for (i = 0; i < 4096; i++)
		hashtable[i] = from;

	/* RLE test assumes that 5 bytes are available */
	while (from < end - 5) {
		if (u32(from) == u32(from + 1)) {
			/*
			 at least 5 subsequent bytes are same => use RLE
			 RLE: 1111 aaaa  aaaa aaaa  bbbb bbbb (24b)
			 where a (12b) is length (-5) and
			       b (8b) is value
			*/
			val = u8(from);
			from += 5;
			orig = from;
			/* max length is 0x0fff (12b) */
			len = (orig + 0x0fff) < end ? (orig + 0x0fff) : end;
			while ((from < len) && (val == u8(from)))
				from++;
			u8(to++) = 0xf0 | ((from - orig) >> 8);
			u8(to++) = (from - orig);
			u8(to++) = val;
			*cbyte = (*cbyte << 1) | 1;
		} else {
			/*
			 hash table is used to find possible match for some
			 at least 24- or 32-bit string
			*/
			hash = quicklz_hash(u32(from));
			orig = hashtable[hash]; /* fetch possible match */
			hashtable[hash] = from; /* store this position */

			offs = from - orig;
			if ((offs < 131072) && /* max lz offs is 17b */
			    (offs > 3) && /* we test at least 4 bytes */
			    ((u32(orig) & 0xffffff) == (u32(from) & 0xffffff))) {
			    	/* at least 3 bytes match */
				if (u32(orig) == u32(from)) {
					/* at least 4 bytes match */
					*cbyte = (*cbyte << 1) | 1;
					len = 0;

					while ((u8(orig + len + 4) == u8(from + len + 4)) &&
					       (len < 2047) && /* max len is 11b */
					       (from + len + 4 < end))
						len++;
					from += len + 4;

					if ((len < 8) && (offs < 1024)) {
						/*
						 LZ: 101a aabb  bbbb bbbb (16b)
						 where a (3b) is length (-4) and
						       b (10b) is offs
						*/
						u8(to++) = 0xa0 | (len << 2) | (offs >> 8);
						u8(to++) = offs;
					} else if ((len < 32) && (offs < 65536)) {
						/*
						 LZ: 110a aaaa  bbbb bbbb  bbbb bbbb (24b)
						 where a (5b) is length (-4) and
						       b (16b) is offs
						*/
						u8(to++) = 0xc0 | len;
						u8(to++) = (offs >> 8);
						u8(to++) = offs;
					} else {
						/*
						 LZ: 1110 aaaa  aaaa aaab  bbbb bbbb  bbbb bbbb (32b)
						 where a (11b) is length (-4) and
						       b (17b) is offs
						*/
						u8(to++) = 0xe0 | (len >> 7);
						u8(to++) = (len << 1) | (offs >> 16);
						u8(to++) = (offs >> 8);
						u8(to++) = offs;
					}
				} else {
					/* just 3 bytes match */
					if (offs < 128) {
						/*
						 LZ: 0aaa aaaa (8b)
						 where a (7b) is offs and
						       length is always 3
						*/
						u8(to++) = offs;
						*cbyte = (*cbyte << 1) | 1;
						from += 3;
					} else if (offs < 8192) {
						/*
						 LZ: 100a aaaa  aaaa aaaa (16b)
						 where a (13b) is offs and
						       length is always 3
						*/
						u8(to++) = 0x80 | offs >> 8;
						u8(to++) = offs;
						*cbyte = (*cbyte << 1) | 1;
						from += 3;
					} else {
						/* plain value */
						u8(to++) = u8(from++);
						*cbyte = (*cbyte << 1);
					}
				}
			} else {
				/* plain value */
				u8(to++) = u8(from++);
				*cbyte = (*cbyte << 1);
			}
		}

		/*
		 There is a control byte for every 8 commands or
		 plain values. 0 means plain value and 1 RLE or LZ msg.
		*/
		if (++counter == 8) {
			cbyte = u8ptr(to++);
			counter = 0;
		}
	}

	/* last bytes */
	while (from < end) {
		u8(to++) = u8(from++);
		*cbyte = (*cbyte << 1);

		if (++counter == 8) {
			cbyte = u8ptr(to++);
			counter = 0;
		}
	}

	*cbyte = (*cbyte << (8 - counter));

	*compressed_size = to - (uintptr_t) _to;
	return 0;
}

int quicklz_decompress(const unsigned char *_from, unsigned char *_to,
		       size_t uncompressed_size)
{
	uintptr_t end, from, to, offs, len;
	unsigned char cbyte, counter;

	from = (uintptr_t) _from;
	to = (uintptr_t) _to;
	end = to + uncompressed_size;
	cbyte = u8(from++);
	counter = 0;

	while (to < end - 5) {
		if (cbyte & (1 << 7)) { /* LZ match or RLE sequence */
			if ((u8(from) & 0x80) == 0) { /* 7bits offs */
				offs = u8(from);
				quicklz_expand(to, to - offs, 3);
				to += 3;
				from += 1;
			} else if ((u8(from) & 0x60) == 0) { /* 13bits offs */
				offs = ((u8(from) & 0x1f) << 8) | u8(from + 1);
				quicklz_expand(to, to - offs, 3);
				to += 3;
				from += 2;
			} else if ((u8(from) & 0x40) == 0) { /* 10bits offs, 3bits length */
				len = ((u8(from) >> 2) & 7) + 4;
				offs = ((u8(from) & 0x03) << 8) | u8(from + 1);
				quicklz_expand(to, to - offs, len);
				to += len;
				from += 2;
			} else if ((u8(from) & 0x20) == 0) { /* 16bits offs, 5bits length */
				len = (u8(from) & 0x1f) + 4;
				offs = (u8(from + 1) << 8) | u8(from + 2);
				quicklz_expand(to, to - offs, len);
				to += len;
				from += 3;
			} else if ((u8(from) & 0x10) == 0) { /* 17bits offs, 11bits length */
				len = (((u8(from) & 0x0f) << 7) | (u8(from + 1) >> 1)) + 4;
				offs = ((u8(from + 1) & 0x01) << 16) | (u8(from + 2) << 8) | (u8(from + 3));
				quicklz_expand(to, to - offs, len);
				to += len;
				from += 4;
			} else { /* RLE sequence */
				len = (((u8(from) & 0x0f) << 8) | u8(from + 1)) + 5;
				memset(u8ptr(to), u8(from + 2), len);
				to += len;
				from += 3;
			}
		} else /* literal */
			u8(to++) = u8(from++);

		cbyte <<= 1;
		if (++counter == 8) { /* fetch control byte */
			cbyte = u8(from++);
			counter = 0;
		}
	}

	while (to < end) {
		u8(to++) = u8(from++);
		if (++counter == 8) {
			counter = 0;
			++from;
		}
	}

	return 0;
}

/**  \} */
/**  \} */
