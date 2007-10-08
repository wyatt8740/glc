/**
 * \file src/stream/scale.c
 * \brief bilinear filtering
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* scale.c -- bilinear filtering
 * Copyright (C) 2007 Pyry Haulos
 * For conditions of distribution and use, see copyright notice in glc.h
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <packetstream.h>

#include "../common/glc.h"
#include "../common/thread.h"
#include "../common/util.h"
#include "scale.h"

/**
 * \addtogroup stream
 *  \{
 */

/**
 * \defgroup scale bilinear filtering
 *  \{
 */

struct scale_ctx_s {
	glc_ctx_i ctx;
	glc_flags_t flags;
	unsigned int w, h, sw, sh, bpp;
	double scale;
	int process;
	
	unsigned int *pos;
	float *factor;
	
	struct scale_ctx_s *next;
};

struct scale_private_s {
	glc_t *glc;
	struct scale_ctx_s *ctx;
	glc_thread_t thread;
};

int scale_read_callback(glc_thread_state_t *state);
int scale_write_callback(glc_thread_state_t *state);
void scale_finish_callback(void *ptr, int err);

int scale_pic_msg(struct scale_private_s *scale, struct scale_ctx_s *ctx, unsigned char *from, unsigned char *to);
int scale_ctx_msg(struct scale_private_s *scale, glc_ctx_message_t *ctx_msg);
int scale_get_ctx(struct scale_private_s *scale, glc_ctx_i ctx_i, struct scale_ctx_s **ctx);

int scale_init(glc_t *glc, ps_buffer_t *from, ps_buffer_t *to)
{
	struct scale_private_s *scale = malloc(sizeof(struct scale_private_s));
	memset(scale, 0, sizeof(struct scale_private_s));
	
	scale->glc = glc;
	
	scale->thread.flags = GLC_THREAD_READ | GLC_THREAD_WRITE;
	scale->thread.read_callback = &scale_read_callback;
	scale->thread.write_callback = &scale_write_callback;
	scale->thread.finish_callback = &scale_finish_callback;
	scale->thread.ptr = scale;
	scale->thread.threads = util_cpus();
	
	return glc_thread_create(glc, &scale->thread, from, to);
}

void scale_finish_callback(void *ptr, int err)
{
	struct scale_private_s *scale = ptr;
	struct scale_ctx_s *del;

	if (err)
		fprintf(stderr, "scale failed: %s (%d)\n", strerror(err), err);
	
	while (scale->ctx != NULL) {
		del = scale->ctx;
		scale->ctx = scale->ctx->next;
		
		if (del->pos)
			free(del->pos);
		if (del->factor)
			free(del->factor);
		free(del);
	}
	
	sem_post(&scale->glc->signal[GLC_SIGNAL_SCALE_FINISHED]);
	free(scale);
}

int scale_read_callback(glc_thread_state_t *state) {
	struct scale_private_s *scale = (struct scale_private_s *) state->ptr;
	struct scale_ctx_s *ctx;
	glc_picture_header_t *pic_header;
	
	if (state->header.type == GLC_MESSAGE_CTX)
		scale_ctx_msg(scale, (glc_ctx_message_t *) state->read_data);
	
	if (state->header.type == GLC_MESSAGE_PICTURE) {
		pic_header = (glc_picture_header_t *) state->read_data;
		scale_get_ctx(scale, pic_header->ctx, &ctx);
		state->threadptr = ctx;
		
		if (ctx->process)
			state->write_size = ctx->sw * ctx->sh * 3 + GLC_PICTURE_HEADER_SIZE;
		else
			state->write_size = state->read_size;
	} else
		state->write_size = state->read_size;
	
	return 0;
}

int scale_write_callback(glc_thread_state_t *state) {
	struct scale_private_s *scale = (struct scale_private_s *) state->ptr;
	struct scale_ctx_s *ctx = state->threadptr;
	
	if (state->header.type == GLC_MESSAGE_PICTURE) {
		if (!ctx->process)
			goto copy;
		
		memcpy(state->write_data, state->read_data, GLC_PICTURE_HEADER_SIZE);
		scale_pic_msg(scale, ctx,
		               (unsigned char *) &state->read_data[GLC_PICTURE_HEADER_SIZE],
		               (unsigned char *) &state->write_data[GLC_PICTURE_HEADER_SIZE]);
		return 0;
	}
copy:
	memcpy(state->write_data, state->read_data, state->write_size);
	
	return 0;
}

int scale_get_ctx(struct scale_private_s *scale, glc_ctx_i ctx_i, struct scale_ctx_s **ctx)
{
	struct scale_ctx_s *list = scale->ctx;
	
	while (list != NULL) {
		if (list->ctx == ctx_i)
			break;
		list = list->next;
	}
	
	if (list == NULL) {
		list = (struct scale_ctx_s *) malloc(sizeof(struct scale_ctx_s));
		memset(list, 0, sizeof(struct scale_ctx_s));
		
		list->next = scale->ctx;
		scale->ctx = list;
		list->ctx = ctx_i;
	}
	
	*ctx = list;
	return 0;
}

int scale_pic_msg(struct scale_private_s *scale, struct scale_ctx_s *ctx, unsigned char *from, unsigned char *to)
{
	unsigned int x, y, ox, oy, tp, sp, op1, op2, op3, op4;
	unsigned int swi = ctx->sw * 3;
	unsigned int shi = ctx->sh * 3;
	ox = oy = 0;

	if ((ctx->scale == 1) && (ctx->flags & GLC_CTX_BGRA)) { /* just BGRA -> BGR */
		for (y = 0; y < shi; y += 3) {
			for (x = 0; x < swi; x += 3) {
				*((unsigned int *) &to[x + y * ctx->w]) =
					((unsigned int *) from)[ox + oy * ctx->w];
				/*to[x + y * ctx->w + 0] = from[ox + oy * ctx->w + 0];
				to[x + y * ctx->w + 1] = from[ox + oy * ctx->w + 1];
				to[x + y * ctx->w + 2] = from[ox + oy * ctx->w + 2];*/
				ox++;
			}
			oy++;
			ox = 0;
		}
		return 0;
	}
	
	if (ctx->scale == 0.5) { /* special case... */
		for (y = 0; y < shi; y += 3) {
			for (x = 0; x < swi; x += 3) {
				tp = x + y * ctx->sw;
				op1 = (ox +        0) + (oy +        0) * ctx->w;
				op2 = (ox + ctx->bpp) + (oy +        0) * ctx->w;
				op3 = (ox +        0) + (oy + ctx->bpp) * ctx->w;
				op4 = (ox + ctx->bpp) + (oy + ctx->bpp) * ctx->w;
				ox += 2 * ctx->bpp;
				
				to[tp + 0] = (from[op1 + 0]
					    + from[op2 + 0]
					    + from[op3 + 0]
					    + from[op4 + 0]) >> 2;
				to[tp + 1] = (from[op1 + 1]
					    + from[op2 + 1]
					    + from[op3 + 1]
					    + from[op4 + 1]) >> 2;
				to[tp + 2] = (from[op1 + 2]
					    + from[op2 + 2]
					    + from[op3 + 2]
					    + from[op4 + 2]) >> 2;
				
			}
			oy += 2 * ctx->bpp;
			ox = 0;
		}
		
		return 0;
	}
	
	for (y = 0; y < ctx->sh; y++) {
		for (x = 0; x < ctx->sw; x++) {
			sp = (x + y * ctx->sw) * 4;
			tp = (x + y * ctx->sw) * 3;
			
			to[tp + 0] = from[ctx->pos[sp + 0] + 0] * ctx->factor[sp + 0]
				   + from[ctx->pos[sp + 1] + 0] * ctx->factor[sp + 1]
				   + from[ctx->pos[sp + 2] + 0] * ctx->factor[sp + 2]
				   + from[ctx->pos[sp + 3] + 0] * ctx->factor[sp + 3];
			to[tp + 1] = from[ctx->pos[sp + 0] + 1] * ctx->factor[sp + 0]
				   + from[ctx->pos[sp + 1] + 1] * ctx->factor[sp + 1]
				   + from[ctx->pos[sp + 2] + 1] * ctx->factor[sp + 2]
				   + from[ctx->pos[sp + 3] + 1] * ctx->factor[sp + 3];
			to[tp + 2] = from[ctx->pos[sp + 0] + 2] * ctx->factor[sp + 0]
				   + from[ctx->pos[sp + 1] + 2] * ctx->factor[sp + 1]
				   + from[ctx->pos[sp + 2] + 2] * ctx->factor[sp + 2]
				   + from[ctx->pos[sp + 3] + 2] * ctx->factor[sp + 3];
		}
	}
	
	return 0;
}

int scale_ctx_msg(struct scale_private_s *scale, glc_ctx_message_t *ctx_msg)
{
	struct scale_ctx_s *ctx;
	scale_get_ctx(scale, ctx_msg->ctx, &ctx);
	
	ctx->flags = ctx_msg->flags;
	ctx->w = ctx_msg->w;
	ctx->h = ctx_msg->h;
	
	if (ctx_msg->flags & GLC_CTX_BGRA) {
		ctx_msg->flags &= ~GLC_CTX_BGR; /* do at least conversion */
		ctx_msg->flags |= GLC_CTX_BGR;
		ctx->bpp = 4;
		printf("scale: converting from BGRA to BGR\n");
	} else if ((scale->glc->scale == 1) && (ctx->flags & GLC_CTX_BGR)) {
		ctx->sw = ctx->w; /* skip scaling */
		ctx->sh = ctx->h;
		ctx->scale = 1;
		return 0;
	} else if (ctx_msg->flags & GLC_CTX_BGR)
		ctx->bpp = 3; /* just scale */

	ctx->process = 1;
	ctx->scale = scale->glc->scale;
	ctx->sw = ctx->scale * ctx->w;
	ctx->sh = ctx->scale * ctx->h;
	
	ctx_msg->w *= scale->glc->scale;
	ctx_msg->h *= scale->glc->scale;
	
	if ((ctx->scale == 0.5) | (ctx->scale == 1.0))
		return 0; /* don't generate scale maps */
	
	size_t smap_size = ctx->sw * ctx->sh * 3 * 4;
	if (ctx->pos)
		ctx->pos = (unsigned int *) realloc(ctx->pos, sizeof(unsigned int) * smap_size);
	else
		ctx->pos = (unsigned int *) malloc(sizeof(unsigned int) * smap_size);
	if (ctx->factor)
		ctx->factor = (float *) realloc(ctx->factor, sizeof(float) * smap_size);
	else
		ctx->factor = (float *) malloc(sizeof(float) * smap_size);
	
	unsigned int tp, x, y;
	float d = 1.0 / ctx->scale;
	float ofx, ofy, fx0, fx1, fy0, fy1;
	ofx = ofy = 0;
	for (y = 0; y < ctx->sh; y++) {
		for (x = 0; x < ctx->sw; x++) {
			tp = (x + y * ctx->sw) * 4;
			
			ctx->pos[tp + 0] = (((unsigned int) ofx + 0) +
			                    ((unsigned int) ofy + 0) * ctx->w) * ctx->bpp;
			ctx->pos[tp + 1] = (((unsigned int) ofx + 1) +
			                    ((unsigned int) ofy + 0) * ctx->w) * ctx->bpp;
			ctx->pos[tp + 2] = (((unsigned int) ofx + 0) +
			                    ((unsigned int) ofy + 1) * ctx->w) * ctx->bpp;
			ctx->pos[tp + 3] = (((unsigned int) ofx + 1) +
			                    ((unsigned int) ofy + 1) * ctx->w) * ctx->bpp;
			
			fx1 = (float) x * d - (float) ((unsigned int) ofx);
			fx0 = 1.0 - fx1;
			fy1 = (float) y * d - (float) ((unsigned int) ofy);
			fy0 = 1.0 - fy1;
			
			ctx->factor[tp + 0] = fx0 * fy0;
			ctx->factor[tp + 1] = fx1 * fy0;
			ctx->factor[tp + 2] = fx0 * fy1;
			ctx->factor[tp + 3] = fx1 * fy1;
			
			ofx += d;
		}
		ofy += d;
		ofx = 0;
	}
	
	return 0;
}


/**  \} */
/**  \} */
