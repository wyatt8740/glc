/**
 * \file src/stream/color.c
 * \brief color correction
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* color.c -- color correction
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
#include "color.h"

/**
 * \addtogroup stream
 *  \{
 */

/**
 * \defgroup color color correction
 *  \{
 */

#define LOOKUP_BITS 7
#define LOOKUP_POS(A, B, C) \
	(((((A) >> (8 - LOOKUP_BITS)) << (LOOKUP_BITS * 2)) + \
	  (((B) >> (8 - LOOKUP_BITS)) << LOOKUP_BITS) + \
	  ( (C) >> (8 - LOOKUP_BITS))) * 3)

struct color_private_s;
struct color_ctx_s;

typedef void (*color_proc)(struct color_private_s *color,
			   struct color_ctx_s *ctx,
			   unsigned char *from, unsigned char *to);

struct color_ctx_s {
	glc_ctx_i ctx_i;
	unsigned int w, h;

	unsigned char *lookup_table;
	color_proc proc;

	pthread_rwlock_t update;
	struct color_ctx_s *next;
};

struct color_private_s {
	glc_t *glc;
	glc_thread_t thread;
	struct color_ctx_s *ctx;
};

int color_read_callback(glc_thread_state_t *state);
int color_write_callback(glc_thread_state_t *state);
void color_finish_callback(void *ptr, int err);

void color_get_ctx(struct color_private_s *color, glc_ctx_i ctx_i,
		   struct color_ctx_s **ctx);

int color_color_msg(struct color_private_s *color, glc_color_message_t *msg);

void color_ycbcr(struct color_private_s *color,
		 struct color_ctx_s *ctx,
		 unsigned char *from, unsigned char *to);
void color_rgb(struct color_private_s *color,
	       struct color_ctx_s *ctx,
	       unsigned char *from, unsigned char *to);

int color_init(glc_t *glc, ps_buffer_t *from, ps_buffer_t *to)
{
	struct color_private_s *color = malloc(sizeof(struct color_private_s));
	memset(color, 0, sizeof(struct color_private_s));

	color->glc = glc;

	color->thread.flags = GLC_THREAD_READ | GLC_THREAD_WRITE;
	color->thread.read_callback = &color_read_callback;
	color->thread.write_callback = &color_write_callback;
	color->thread.ptr = color;
	color->thread.threads = util_cpus();

	return glc_thread_create(glc, &color->thread, from, to);
}

void color_finish_callback(void *ptr, int err)
{
	struct color_private_s *color = ptr;
	struct color_ctx_s *del;

	if (err)
		util_log(color->glc, GLC_ERROR, "color", "%s (%d)", strerror(err), err);

	while (color->ctx != NULL) {
		del = color->ctx;
		color->ctx = color->ctx->next;

		pthread_rwlock_destroy(&del->update);
		if (del->lookup_table)
			free(del->lookup_table);
		free(del);
	}

	sem_post(&color->glc->signal[GLC_SIGNAL_COLOR_FINISHED]);
	free(color);
}

int color_read_callback(glc_thread_state_t *state)
{
	struct color_private_s *color = state->ptr;
	struct color_ctx_s *ctx;
	glc_picture_header_t *pic_hdr;

	if (state->header.type == GLC_MESSAGE_COLOR) {
		color_color_msg(color, (glc_color_message_t *) state->read_data);

		/* color correction should be done */
		state->flags |= GLC_THREAD_STATE_SKIP_WRITE;
		return 0;
	}

	if (state->header.type == GLC_MESSAGE_PICTURE) {
		pic_hdr = (glc_picture_header_t *) state->read_data;
		color_get_ctx(color, pic_hdr->ctx, &ctx);
		state->threadptr = ctx;

		pthread_rwlock_rdlock(&ctx->update);

		if (ctx->proc == NULL) {
			state->flags |= GLC_THREAD_COPY;
			pthread_rwlock_unlock(&ctx->update);
		}
	} else
		state->flags |= GLC_THREAD_COPY;

	return 0;
}

int color_write_callback(glc_thread_state_t *state)
{
	struct color_ctx_s *ctx = state->threadptr;

	memcpy(state->write_data, state->read_data, GLC_PICTURE_HEADER_SIZE);
	ctx->proc(state->ptr, ctx,
		  (unsigned char *) &state->read_data[GLC_PICTURE_HEADER_SIZE],
		  (unsigned char *) &state->write_data[GLC_PICTURE_HEADER_SIZE]);

	pthread_rwlock_unlock(&ctx->update);
	return 0;
}

void color_get_ctx(struct color_private_s *color, glc_ctx_i ctx_i,
		   struct color_ctx_s **ctx)
{
	/* this function is called from read callback so it is never
	   called in parallel */
	*ctx = color->ctx;

	while (*ctx != NULL) {
		if ((*ctx)->ctx_i == ctx_i)
			break;
		*ctx = (*ctx)->next;
	}

	if (*ctx == NULL) {
		*ctx = malloc(sizeof(struct color_ctx_s));
		memset(*ctx, 0, sizeof(struct color_ctx_s));

		(*ctx)->next = color->ctx;
		color->ctx = *ctx;
		(*ctx)->ctx_i = ctx_i;
		pthread_rwlock_init(&(*ctx)->update, NULL);
	}
}

int color_color_msg(struct color_private_s *color, glc_color_message_t *msg)
{
	return 0;
}

void color_ycbcr(struct color_private_s *color,
		 struct color_ctx_s *ctx,
		 unsigned char *from, unsigned char *to)
{

}

void color_rgb(struct color_private_s *color,
	       struct color_ctx_s *ctx,
	       unsigned char *from, unsigned char *to)
{

}

/**  \} */
/**  \} */
