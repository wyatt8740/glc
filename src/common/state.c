/**
 * \file src/common/state.c
 * \brief glc state
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup state
 *  \{
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#include "glc.h"
#include "core.h"
#include "log.h"
#include "state.h"

struct glc_state_ctx_s {
	glc_ctx_i ctx_i;

	struct glc_state_ctx_s *next;
};

struct glc_state_audio_s {
	glc_audio_i audio_i;

	struct glc_state_audio_s *next;
};

struct glc_state_s {
	pthread_rwlock_t state_rwlock;

	pthread_rwlock_t time_rwlock;
	glc_stime_t time_difference;

	pthread_rwlock_t ctx_rwlock;
	struct glc_state_ctx_s *ctx;
	glc_ctx_i ctx_count;

	pthread_rwlock_t audio_rwlock;
	struct glc_state_audio_s *audio;
	glc_audio_i audio_count;
};

int glc_state_init(glc_t *glc)
{
	glc->state_flags = 0;
	glc->state = (glc_state_t) malloc(sizeof(struct glc_state_s));
	memset(glc->state, 0, sizeof(struct glc_state_s));

	pthread_rwlock_init(&glc->state->state_rwlock, NULL);
	pthread_rwlock_init(&glc->state->time_rwlock, NULL);

	pthread_rwlock_init(&glc->state->ctx_rwlock, NULL);
	pthread_rwlock_init(&glc->state->audio_rwlock, NULL);

	return 0;
}

int glc_state_destroy(glc_t *glc)
{
	struct glc_state_ctx_s *ctx_del;
	struct glc_state_audio_s *audio_del;

	while (glc->state->ctx != NULL) {
		ctx_del = glc->state->ctx;
		glc->state->ctx = glc->state->ctx->next;

		free(ctx_del);
	}

	while (glc->state->audio != NULL) {
		audio_del = glc->state->audio;
		glc->state->audio = glc->state->audio->next;

		free(audio_del);
	}

	pthread_rwlock_destroy(&glc->state->state_rwlock);
	pthread_rwlock_destroy(&glc->state->time_rwlock);

	pthread_rwlock_destroy(&glc->state->ctx_rwlock);
	pthread_rwlock_destroy(&glc->state->audio_rwlock);

	free(glc->state);
	glc->state_flags = 0;

	return 0;
}

int glc_state_ctx_new(glc_t *glc, glc_ctx_i *ctx_i,
		      glc_state_ctx_t *ctx)
{
	*ctx = (glc_state_ctx_t) malloc(sizeof(struct glc_state_ctx_s));
	memset(*ctx, 0, sizeof(struct glc_state_ctx_s));

	pthread_rwlock_wrlock(&glc->state->ctx_rwlock);
	(*ctx)->ctx_i = ++glc->state->ctx_count;
	(*ctx)->next = glc->state->ctx;
	glc->state->ctx = *ctx;
	pthread_rwlock_unlock(&glc->state->ctx_rwlock);

	*ctx_i = (*ctx)->ctx_i;
	return 0;
}

int glc_state_audio_new(glc_t *glc, glc_audio_i *audio_i,
			glc_state_audio_t *audio)
{
	*audio = (glc_state_audio_t) malloc(sizeof(struct glc_state_audio_s));
	memset(*audio, 0, sizeof(struct glc_state_audio_s));

	pthread_rwlock_wrlock(&glc->state->audio_rwlock);
	(*audio)->audio_i = ++glc->state->audio_count;
	(*audio)->next = glc->state->audio;
	glc->state->audio = *audio;
	pthread_rwlock_unlock(&glc->state->audio_rwlock);

	*audio_i = (*audio)->audio_i;
	return 0;
}

int glc_state_set(glc_t *glc, int flag)
{
	pthread_rwlock_wrlock(&glc->state->state_rwlock);
	glc->state_flags |= flag;
	pthread_rwlock_unlock(&glc->state->state_rwlock);
	return 0;
}

int glc_state_clear(glc_t *glc, int flag)
{
	pthread_rwlock_wrlock(&glc->state->state_rwlock);
	glc->state_flags &= ~flag;
	pthread_rwlock_unlock(&glc->state->state_rwlock);
	return 0;
}

int glc_state_test(glc_t *glc, int flag)
{
	return (glc->state_flags & flag);
}

glc_utime_t glc_state_time(glc_t *glc)
{
	return glc_time(glc) - glc->state->time_difference;
}

int glc_state_time_add_diff(glc_t *glc, glc_stime_t diff)
{
	glc_log(glc, GLC_DEBUG, "state", "applying %ld usec time difference", diff);
	pthread_rwlock_wrlock(&glc->state->time_rwlock);
	glc->state->time_difference += diff;
	pthread_rwlock_unlock(&glc->state->time_rwlock);
	return 0;
}

/**  \} */
