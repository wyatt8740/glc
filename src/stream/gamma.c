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

#define LOOKUP_BITS 7
#define LOOKUP_POS(A, B, C) \
	(((((A) >> (8 - LOOKUP_BITS)) << (LOOKUP_BITS * 2)) + \
	  (((B) >> (8 - LOOKUP_BITS)) << LOOKUP_BITS) + \
	  ( (C) >> (8 - LOOKUP_BITS))) * 3)

struct gamma_private_s;
struct gamma_ctx_s;

typedef void (*gamma_proc)(struct gamma_private_s *gamma,
			   struct gamma_ctx_s *ctx,
			   unsigned char *from, unsigned char *to);

struct gamma_ctx_s {
	glc_ctx_i ctx_i;
	unsigned int w, h;

	unsigned char *lookup_table;
	gamma_proc proc;

	pthread_rwlock_t update;
	struct gamma_ctx_s *next;
};

struct gamma_private_s {
	glc_t *glc;
	glc_thread_t thread;
	struct gamma_ctx_s *ctx;
};

int gamma_read_callback(glc_thread_state_t *state);
int gamma_write_callback(glc_thread_state_t *state);
void gamma_finish_callback(void *ptr, int err);

void gamma_get_ctx(struct gamma_private_s *gamma, glc_ctx_i ctx_i,
		   struct gamma_ctx_s **ctx);

int gamma_gamma_msg(struct gamma_private_s *gamma, glc_gamma_message_t *msg);

int gamma_init(glc_t *glc, ps_buffer_t *from, ps_buffer_t *to)
{
	struct gamma_private_s *gamma = malloc(sizeof(struct gamma_private_s));
	memset(gamma, 0, sizeof(struct gamma_private_s));

	gamma->glc = glc;

	gamma->thread.flags = GLC_THREAD_READ | GLC_THREAD_WRITE;
	gamma->thread.read_callback = &gamma_read_callback;
	gamma->thread.write_callback = &gamma_write_callback;
	gamma->thread.ptr = gamma;
	gamma->thread.threads = util_cpus();

	return glc_thread_create(glc, &gamma->thread, from, to);
}

void gamma_finish_callback(void *ptr, int err)
{
	struct gamma_private_s *gamma = ptr;
	struct gamma_ctx_s *del;

	if (err)
		util_log(gamma->glc, GLC_ERROR, "gamma", "%s (%d)", strerror(err), err);

	while (gamma->ctx != NULL) {
		del = gamma->ctx;
		gamma->ctx = gamma->ctx->next;

		pthread_rwlock_destroy(&del->update);
		if (del->lookup_table)
			free(del->lookup_table);
		free(del);
	}

	sem_post(&gamma->glc->signal[GLC_SIGNAL_GAMMA_FINISHED]);
	free(gamma);
}

int gamma_read_callback(glc_thread_state_t *state)
{
	struct gamma_private_s *gamma = state->ptr;
	struct gamma_ctx_s *ctx;
	glc_picture_header_t *pic_hdr;

	if (state->header.type == GLC_MESSAGE_GAMMA) {
		gamma_gamma_msg(gamma, (glc_gamma_message_t *) state->read_data);

		/* gamma correction should be done */
		state->flags |= GLC_THREAD_STATE_SKIP_WRITE;
		return 0;
	}

	if (state->header.type == GLC_MESSAGE_PICTURE) {
		pic_hdr = (glc_picture_header_t *) state->read_data;
		gamma_get_ctx(gamma, pic_hdr->ctx, &ctx);
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

int gamma_write_callback(glc_thread_state_t *state)
{
	struct gamma_ctx_s *ctx = state->threadptr;

	memcpy(state->write_data, state->read_data, GLC_PICTURE_HEADER_SIZE);
	ctx->proc(state->ptr, ctx,
		  (unsigned char *) &state->read_data[GLC_PICTURE_HEADER_SIZE],
		  (unsigned char *) &state->write_data[GLC_PICTURE_HEADER_SIZE]);

	pthread_rwlock_unlock(&ctx->update);
	return 0;
}

void gamma_get_ctx(struct gamma_private_s *gamma, glc_ctx_i ctx_i,
		   struct gamma_ctx_s **ctx)
{
	/* this function is called from read callback so it is never
	   called in parallel */
	*ctx = gamma->ctx;

	while (*ctx != NULL) {
		if ((*ctx)->ctx_i == ctx_i)
			break;
		*ctx = (*ctx)->next;
	}

	if (*ctx == NULL) {
		*ctx = malloc(sizeof(struct gamma_ctx_s));
		memset(*ctx, 0, sizeof(struct gamma_ctx_s));

		(*ctx)->next = gamma->ctx;
		gamma->ctx = *ctx;
		(*ctx)->ctx_i = ctx_i;
		pthread_rwlock_init(&(*ctx)->update, NULL);
	}
}

int gamma_gamma_msg(struct gamma_private_s *gamma, glc_gamma_message_t *msg)
{
	return 0;
}

/**  \} */
/**  \} */
