/**
 * \file src/core/pack.c
 * \brief stream compression
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup core
 *  \{
 * \defgroup pack stream compression
 *  \{
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

#ifdef __MINILZO
# include <minilzo.h>
# define __lzo_compress lzo1x_1_compress
# define __lzo_decompress lzo1x_decompress
# define __lzo_worstcase(size) size + (size / 16) + 64 + 3
# define __lzo_wrk_mem LZO1X_1_MEM_COMPRESS
# define __LZO
#else
# ifdef __LZO
#  include <lzo/lzo1x.h>
#  define __lzo_compress lzo1x_1_11_compress
#  define __lzo_decompress lzo1x_decompress
#  define __lzo_worstcase(size) size + (size / 16) + 64 + 3
#  define __lzo_wrk_mem LZO1X_1_11_MEM_COMPRESS
# endif
#endif

#ifdef __QUICKLZ
# include <quicklz.h>
#endif

struct pack_s {
	glc_t *glc;
	glc_thread_t thread;
	size_t compress_min;
	int running;
	int compression;
};

struct unpack_s {
	glc_t *glc;
	glc_thread_t thread;
	int running;
};

int pack_thread_create_callback(void *ptr, void **threadptr);
void pack_thread_finish_callback(void *ptr, void *threadptr, int err);
int pack_read_callback(glc_thread_state_t *state);
int pack_quicklz_write_callback(glc_thread_state_t *state);
int pack_lzo_write_callback(glc_thread_state_t *state);
void pack_finish_callback(void *ptr, int err);

int unpack_read_callback(glc_thread_state_t *state);
int unpack_write_callback(glc_thread_state_t *state);
void unpack_finish_callback(void *ptr, int err);

int pack_init(pack_t *pack, glc_t *glc)
{
	*pack = (pack_t) malloc(sizeof(struct pack_s));
	memset(pack, 0, sizeof(struct pack_s));

	(*pack)->glc = glc;
	(*pack)->compress_min = 1024;

	(*pack)->thread.flags = GLC_THREAD_WRITE | GLC_THREAD_READ;
	(*pack)->thread.ptr = *pack;
	(*pack)->thread.thread_create_callback = &pack_thread_create_callback;
	(*pack)->thread.thread_finish_callback = &pack_thread_finish_callback;
	(*pack)->thread.read_callback = &pack_read_callback;
	(*pack)->thread.finish_callback = &pack_finish_callback;
	(*pack)->thread.threads = util_cpus();

#ifdef __QUICKLZ
	(*pack)->thread.write_callback = &pack_quicklz_write_callback;
	(*pack)->compression = PACK_QUICKLZ;
#else
# ifdef __LZO
	(*pack)->thread.write_callback = &pack_lzo_write_callback;
	(*pack)->compression = PACK_LZO;
# else
	util_log((*pack)->glc, GLC_ERROR, "pack",
		 "no supported compression algorithms found");
	return ENOTSUP;
# endif
#endif

	return 0;
}

int pack_set_compression(pack_t pack, int compression)
{
	if (pack->running)
		return EALREADY;

	if (compression == PACK_QUICKLZ) {
#ifdef __QUICKLZ
		pack->thread.write_callback = &pack_quicklz_write_callback;
		util_log(pack->glc, GLC_INFORMATION, "pack",
			 "compressing using QuickLZ");
#else
		util_log(pack->glc, GLC_ERROR, "pack",
			 "QuickLZ not supported");
		return ENOTSUP;
#endif
	} else if (compression == PACK_LZO) {
#ifdef __LZO
		pack->thread.write_callback = &pack_lzo_write_callback;
		util_log(pack->glc, GLC_INFORMATION, "pack",
			 "compressing using LZO");
		lzo_init();
#else
		util_log(pack->glc, GLC_ERROR, "pack",
			 "LZO not supported");
		return ENOTSUP;
#endif
	} else {
		util_log(pack->glc, GLC_ERROR, "pack",
			 "unknown/unsupported compression algorithm 0x%02x",
			 compression);
		return ENOTSUP;
	}

	pack->compression = compression;
	return 0;
}

int pack_set_minimum_size(pack_t pack, size_t min_size)
{
	if (pack->running)
		return EALREADY;

	if (min_size < 0)
		return EINVAL;

	pack->compress_min = min_size;
	return 0;
}

int pack_process_start(pack_t pack, ps_buffer_t *from, ps_buffer_t *to)
{
	int ret;
	if (pack->running)
		return EAGAIN;

	if ((ret = glc_thread_create(pack->glc, &pack->thread, from, to)))
		return ret;
	pack->running = 1;

	return 0;
}

int pack_process_wait(pack_t pack)
{
	if (!pack->running)
		return EAGAIN;

	glc_thread_wait(&pack->thread);
	pack->running = 0;

	return 0;
}

int pack_destroy(pack_t pack)
{
	free(pack);
	return 0;
}

void pack_finish_callback(void *ptr, int err)
{
	pack_t pack = (pack_t) ptr;

	if (err)
		util_log(pack->glc, GLC_ERROR, "pack", "%s (%d)", strerror(err), err);
}

int pack_thread_create_callback(void *ptr, void **threadptr)
{
	pack_t pack = (pack_t) ptr;

	if (pack->compression == PACK_QUICKLZ) {
#ifdef __QUICKLZ
		*threadptr = malloc(__quicklz_hashtable);
#endif
	} else if (pack->compression == PACK_LZO) {
#ifdef __LZO
		*threadptr = malloc(__lzo_wrk_mem);
#endif
	}

	return 0;
}

void pack_thread_finish_callback(void *ptr, void *threadptr, int err)
{
	if (threadptr)
		free(threadptr);
}

int pack_read_callback(glc_thread_state_t *state)
{
	pack_t pack = (pack_t) state->ptr;

	/* compress only audio and pictures */
	if ((state->read_size > pack->compress_min) &&
	    ((state->header.type == GLC_MESSAGE_PICTURE) |
	     (state->header.type == GLC_MESSAGE_AUDIO))) {
		if (pack->compression == PACK_QUICKLZ) {
#ifdef __QUICKLZ
			state->write_size = GLC_CONTAINER_MESSAGE_SIZE
					    + GLC_QUICKLZ_HEADER_SIZE
					    + __quicklz_worstcase(state->read_size);
#else
			goto copy;
#endif
		} else if (pack->compression == PACK_LZO) {
#ifdef __LZO
			state->write_size = GLC_CONTAINER_MESSAGE_SIZE
					    + GLC_LZO_HEADER_SIZE
			                    + __lzo_worstcase(state->read_size);
#else
			goto copy;
#endif
		} else
			goto copy;

		return 0;
	}
copy:
	state->flags |= GLC_THREAD_COPY;
	return 0;
}

int pack_lzo_write_callback(glc_thread_state_t *state)
{
#ifdef __LZO
	glc_container_message_t *container = (glc_container_message_t *) state->write_data;
	glc_lzo_header_t *lzo_header =
		(glc_lzo_header_t *) &state->write_data[GLC_CONTAINER_MESSAGE_SIZE];
	lzo_uint compressed_size;

	__lzo_compress((unsigned char *) state->read_data, state->read_size,
		       (unsigned char *) &state->write_data[GLC_LZO_HEADER_SIZE +
		       					    GLC_CONTAINER_MESSAGE_SIZE],
		       &compressed_size, (lzo_voidp) state->threadptr);

	lzo_header->size = (glc_size_t) state->read_size;
	memcpy(&lzo_header->header, &state->header, GLC_MESSAGE_HEADER_SIZE);

	container->size = compressed_size + GLC_LZO_HEADER_SIZE;
	container->header.type = GLC_MESSAGE_LZO;

	state->header.type = GLC_MESSAGE_CONTAINER;

	return 0;
#else
	return ENOTSUP;
#endif
}

int pack_quicklz_write_callback(glc_thread_state_t *state)
{
#ifdef __QUICKLZ
	glc_container_message_t *container = (glc_container_message_t *) state->write_data;
	glc_quicklz_header_t *quicklz_header =
		(glc_quicklz_header_t *) &state->write_data[GLC_CONTAINER_MESSAGE_SIZE];
	size_t compressed_size;

	quicklz_compress((const unsigned char *) state->read_data,
			 (unsigned char *) &state->write_data[GLC_QUICKLZ_HEADER_SIZE +
			 				      GLC_CONTAINER_MESSAGE_SIZE],
			 state->read_size, &compressed_size,
			 (uintptr_t *) state->threadptr);

	quicklz_header->size = (glc_size_t) state->read_size;
	memcpy(&quicklz_header->header, &state->header, GLC_MESSAGE_HEADER_SIZE);

	container->size = compressed_size + GLC_QUICKLZ_HEADER_SIZE;
	container->header.type = GLC_MESSAGE_QUICKLZ;

	state->header.type = GLC_MESSAGE_CONTAINER;

	return 0;
#else
	return ENOTSUP;
#endif
}

int unpack_init(unpack_t *unpack, glc_t *glc)
{
	*unpack = (unpack_t) malloc(sizeof(struct unpack_s));
	memset(*unpack, 0, sizeof(struct unpack_s));

	(*unpack)->glc = glc;

	(*unpack)->thread.flags = GLC_THREAD_WRITE | GLC_THREAD_READ;
	(*unpack)->thread.ptr = *unpack;
	(*unpack)->thread.read_callback = &unpack_read_callback;
	(*unpack)->thread.write_callback = &unpack_write_callback;
	(*unpack)->thread.finish_callback = &unpack_finish_callback;
	(*unpack)->thread.threads = util_cpus();

#ifdef __LZO
	lzo_init();
#endif

	return 0;
}

int unpack_process_start(unpack_t unpack, ps_buffer_t *from, ps_buffer_t *to)
{
	int ret;
	if (unpack->running)
		return EAGAIN;

	if ((ret = glc_thread_create(unpack->glc, &unpack->thread, from, to)))
		return ret;
	unpack->running = 1;

	return 0;
}

int unpack_process_wait(unpack_t unpack)
{
	if (!unpack->running)
		return EAGAIN;

	glc_thread_wait(&unpack->thread);
	unpack->running = 0;

	return 0;
}

int unpack_destroy(unpack_t unpack)
{
	free(unpack);
	return 0;
}

void unpack_finish_callback(void *ptr, int err)
{
	unpack_t unpack = (unpack_t) ptr;

	if (err)
		util_log(unpack->glc, GLC_ERROR, "unpack", "%s (%d)", strerror(err), err);
}

int unpack_read_callback(glc_thread_state_t *state)
{
	if (state->header.type == GLC_MESSAGE_LZO) {
#ifdef __LZO
		state->write_size = ((glc_lzo_header_t *) state->read_data)->size;
		return 0;
#else
		util_log(((struct pack_private_s *) state->ptr)->glc,
			 GLC_ERROR, "unpack", "LZO not supported");
		return ENOTSUP;
#endif
	} else if (state->header.type == GLC_MESSAGE_QUICKLZ) {
#ifdef __QUICKLZ
		state->write_size = ((glc_quicklz_header_t *) state->read_data)->size;
		return 0;
#else
		util_log(((struct pack_private_s *) state->ptr)->glc,
			 GLC_ERROR, "unpack", "unpack: QuickLZ not supported");
		return ENOTSUP;
#endif
	}

	state->flags |= GLC_THREAD_COPY;
	return 0;
}

int unpack_write_callback(glc_thread_state_t *state)
{
	if (state->header.type == GLC_MESSAGE_LZO) {
#ifdef __LZO
		memcpy(&state->header, &((glc_lzo_header_t *) state->read_data)->header,
		       GLC_MESSAGE_HEADER_SIZE);
		__lzo_decompress((unsigned char *) &state->read_data[GLC_LZO_HEADER_SIZE],
				state->read_size - GLC_LZO_HEADER_SIZE,
				(unsigned char *) state->write_data,
				(lzo_uintp) &state->write_size,
				NULL);
#else
		return ENOTSUP;
#endif
	} else if (state->header.type == GLC_MESSAGE_QUICKLZ) {
#ifdef __QUICKLZ
		memcpy(&state->header, &((glc_quicklz_header_t *) state->read_data)->header,
		       GLC_MESSAGE_HEADER_SIZE);
		quicklz_decompress((const unsigned char *) &state->read_data[GLC_QUICKLZ_HEADER_SIZE],
				   (unsigned char *) state->write_data,
				   state->write_size);
#else
		return ENOTSUP;
#endif
	} else
		return ENOTSUP;

	return 0;
}

/**  \} */
/**  \} */
