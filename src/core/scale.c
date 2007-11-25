/**
 * \file src/core/scale.c
 * \brief software scaler
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
#include <pthread.h>

#include "../common/glc.h"
#include "../common/thread.h"
#include "../common/util.h"
#include "scale.h"

/**
 * \addtogroup core
 *  \{
 */

/**
 * \defgroup scale software scaler
 *  \{
 */

struct scale_ctx_s;
struct scale_private_s;

typedef void (*scale_proc)(struct scale_private_s *scale,
			   struct scale_ctx_s *ctx,
			   unsigned char *from,
			   unsigned char *to);

struct scale_ctx_s {
	glc_ctx_i ctx;
	glc_flags_t flags;
	size_t size;
	unsigned int w, h, sw, sh, bpp;
	unsigned int row;
	double scale;

	unsigned int rw, rh, rx, ry;

	unsigned int *pos;
	float *factor;

	scale_proc proc;

	pthread_rwlock_t update;
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
int scale_ctx_msg(struct scale_private_s *scale, glc_ctx_message_t *ctx_msg, glc_thread_state_t *state);
int scale_get_ctx(struct scale_private_s *scale, glc_ctx_i ctx_i, struct scale_ctx_s **ctx);

int scale_generate_rgb_map(struct scale_private_s *scale, struct scale_ctx_s *ctx);
int scale_generate_ycbcr_map(struct scale_private_s *scale, struct scale_ctx_s *ctx);

void scale_rgb_convert(struct scale_private_s *scale, struct scale_ctx_s *ctx,
		       unsigned char *from, unsigned char *to);
void scale_rgb_half(struct scale_private_s *scale, struct scale_ctx_s *ctx,
		    unsigned char *from, unsigned char *to);
void scale_rgb_scale(struct scale_private_s *scale, struct scale_ctx_s *ctx,
		     unsigned char *from, unsigned char *to);

void scale_ycbcr_half(struct scale_private_s *scale, struct scale_ctx_s *ctx,
		      unsigned char *from, unsigned char *to);
void scale_ycbcr_scale(struct scale_private_s *scale, struct scale_ctx_s *ctx,
		       unsigned char *from, unsigned char *to);

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
		util_log(scale->glc, GLC_ERROR, "scale", "%s (%d)", strerror(err), err);

	while (scale->ctx != NULL) {
		del = scale->ctx;
		scale->ctx = scale->ctx->next;

		if (del->pos)
			free(del->pos);
		if (del->factor)
			free(del->factor);

		pthread_rwlock_destroy(&del->update);
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
		return scale_ctx_msg(scale, (glc_ctx_message_t *) state->read_data, state);

	if (state->header.type == GLC_MESSAGE_PICTURE) {
		pic_header = (glc_picture_header_t *) state->read_data;
		scale_get_ctx(scale, pic_header->ctx, &ctx);
		state->threadptr = ctx;

		pthread_rwlock_rdlock(&ctx->update);

		if (ctx->proc)
			state->write_size = ctx->size + GLC_PICTURE_HEADER_SIZE;
		else {
			state->flags |= GLC_THREAD_COPY;
			pthread_rwlock_unlock(&ctx->update);
		}
	} else
		state->flags |= GLC_THREAD_COPY;

	return 0;
}

int scale_write_callback(glc_thread_state_t *state) {
	struct scale_private_s *scale = (struct scale_private_s *) state->ptr;
	struct scale_ctx_s *ctx = state->threadptr;

	memcpy(state->write_data, state->read_data, GLC_PICTURE_HEADER_SIZE);
	ctx->proc(scale, ctx,
		  (unsigned char *) &state->read_data[GLC_PICTURE_HEADER_SIZE],
		  (unsigned char *) &state->write_data[GLC_PICTURE_HEADER_SIZE]);
	pthread_rwlock_unlock(&ctx->update);

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
		pthread_rwlock_init(&list->update, NULL);
	}

	*ctx = list;
	return 0;
}

void scale_rgb_convert(struct scale_private_s *scale, struct scale_ctx_s *ctx,
		       unsigned char *from, unsigned char *to)
{
	unsigned int x, y, ox, oy, op, tp;
	unsigned int swi = ctx->sw * 3;
	unsigned int shi = ctx->sh * 3;
	ox = oy = 0;

	/* just convert from different bpp to 3 */
	for (y = 0; y < shi; y += 3) {
		for (x = 0; x < swi; x += 3) {
			tp = x + y * ctx->sw;
			op = ox + oy * ctx->row;

			to[tp + 0] = from[op + 0];
			to[tp + 1] = from[op + 1];
			to[tp + 2] = from[op + 2];

			ox += ctx->bpp;
		}
		oy++;
		ox = 0;
	}
}

void scale_rgb_half(struct scale_private_s *scale, struct scale_ctx_s *ctx,
		    unsigned char *from, unsigned char *to)
{
	unsigned int x, y, ox, oy, tp, op1, op2, op3, op4;
	unsigned int swi = ctx->sw * 3;
	unsigned int shi = ctx->sh * 3;
	ox = oy = 0;

	for (y = 0; y < shi; y += 3) {
		for (x = 0; x < swi; x += 3) {
			tp = x + y * ctx->sw;
			op1 = (ox +        0) + (oy + 0) * ctx->row;
			op2 = (ox + ctx->bpp) + (oy + 0) * ctx->row;
			op3 = (ox +        0) + (oy + 1) * ctx->row;
			op4 = (ox + ctx->bpp) + (oy + 1) * ctx->row;
			ox += 2 * ctx->bpp;

			to[tp + 0] = (from[op1 + 0] +
				      from[op2 + 0] +
				      from[op3 + 0] +
				      from[op4 + 0]) >> 2;
			to[tp + 1] = (from[op1 + 1] +
				      from[op2 + 1] +
				      from[op3 + 1] +
				      from[op4 + 1]) >> 2;
			to[tp + 2] = (from[op1 + 2] +
				      from[op2 + 2] +
				      from[op3 + 2] +
				      from[op4 + 2]) >> 2;

		}
		oy += 2;
		ox = 0;
	}
}

void scale_rgb_scale(struct scale_private_s *scale, struct scale_ctx_s *ctx,
		     unsigned char *from, unsigned char *to)
{
	unsigned int x, y, tp, sp;

	if (scale->glc->flags & GLC_SCALE_SIZE)
		memset(to, 0, ctx->size);

	for (y = 0; y < ctx->sh; y++) {
		for (x = 0; x < ctx->sw; x++) {
			sp = (x + y * ctx->sw) * 4;
			tp = ((x + ctx->rx) + (y + ctx->ry) * ctx->rw) * 3;

			to[tp + 0] = from[ctx->pos[sp + 0] + 0] * ctx->factor[sp + 0] +
				     from[ctx->pos[sp + 1] + 0] * ctx->factor[sp + 1] +
				     from[ctx->pos[sp + 2] + 0] * ctx->factor[sp + 2] +
				     from[ctx->pos[sp + 3] + 0] * ctx->factor[sp + 3];
			to[tp + 1] = from[ctx->pos[sp + 0] + 1] * ctx->factor[sp + 0] +
				     from[ctx->pos[sp + 1] + 1] * ctx->factor[sp + 1] +
				     from[ctx->pos[sp + 2] + 1] * ctx->factor[sp + 2] +
				     from[ctx->pos[sp + 3] + 1] * ctx->factor[sp + 3];
			to[tp + 2] = from[ctx->pos[sp + 0] + 2] * ctx->factor[sp + 0] +
				     from[ctx->pos[sp + 1] + 2] * ctx->factor[sp + 1] +
				     from[ctx->pos[sp + 2] + 2] * ctx->factor[sp + 2] +
				     from[ctx->pos[sp + 3] + 2] * ctx->factor[sp + 3];
		}
	}
}

void scale_ycbcr_half(struct scale_private_s *scale, struct scale_ctx_s *ctx,
		      unsigned char *from, unsigned char *to)
{
	unsigned int x, y, ox, oy, cw_from, ch_from, cw_to, ch_to, op1, op2, op3, op4;
	unsigned char *Y_to, *Cb_to, *Cr_to;
	unsigned char *Y_from, *Cb_from, *Cr_from;

	cw_from = ctx->w / 2;
	ch_from = ctx->h / 2;
	Y_from = from;
	Cb_from = &from[ctx->w * ctx->h];
	Cr_from = &Cb_from[cw_from * ch_from];

	cw_to = ctx->sw / 2;
	ch_to = ctx->sh / 2;
	Y_to = to;
	Cb_to = &to[ctx->sw * ctx->sh];
	Cr_to = &Cb_to[cw_to * ch_to];

	ox = oy = 0;
	for (y = 0; y < ch_to; y++) {
		for (x = 0; x < cw_to; x++) {
			op1 = (oy + 0) * cw_from + (ox + 0);
			op2 = (oy + 0) * cw_from + (ox + 1);
			op3 = (oy + 1) * cw_from + (ox + 0);
			op4 = (oy + 1) * cw_from + (ox + 1);

			Cb_to[y * cw_to + x] = (Cb_from[op1] +
						Cb_from[op2] +
						Cb_from[op3] +
						Cb_from[op4]) >> 2;
			Cr_to[y * cw_to + x] = (Cr_from[op1] +
						Cr_from[op2] +
						Cr_from[op3] +
						Cr_from[op4]) >> 2;

			ox += 2;
		}
		ox = 0;
		oy += 2;
	}

	ox = oy = 0;
	for (y = 0; y < ctx->sh; y++) {
		for (x = 0; x < ctx->sw; x++) {
			op1 = (oy + 0) * ctx->w + (ox + 0);
			op2 = (oy + 0) * ctx->w + (ox + 1);
			op3 = (oy + 1) * ctx->w + (ox + 0);
			op4 = (oy + 1) * ctx->w + (ox + 1);

			Y_to[y * ctx->sw + x] = (Y_from[op1] +
						 Y_from[op2] +
						 Y_from[op3] +
						 Y_from[op4]) >> 2;

			ox += 2;
		}
		ox = 0;
		oy += 2;
	}
}

void scale_ycbcr_scale(struct scale_private_s *scale, struct scale_ctx_s *ctx,
		       unsigned char *from, unsigned char *to)
{
	unsigned int x, y, sp, cw, ch;
	unsigned char *Y_to, *Cb_to, *Cr_to;
	unsigned char *Y_from, *Cb_from, *Cr_from;

	Y_from = from;
	Cb_from = &from[ctx->w * ctx->h];
	Cr_from = &Cb_from[(ctx->w / 2) * (ctx->h / 2)];

	cw = ctx->sw / 2;
	ch = ctx->sh / 2;
	Y_to = to;
	Cb_to = &to[ctx->rw * ctx->rh];
	Cr_to = &Cb_to[(ctx->rw / 2) * (ctx->rh / 2)];

	if (scale->glc->flags & GLC_SCALE_SIZE) {
		memset(Y_to, 0, ctx->rw * ctx->rh);
		memset(Cb_to, 128, (ctx->rw / 2) * (ctx->rh / 2));
		memset(Cr_to, 128, (ctx->rw / 2) * (ctx->rh / 2));
	}

	for (y = 0; y < ctx->sh; y++) {
		for (x = 0; x < ctx->sw; x++) {
			sp = (x + y * ctx->sw) * 4;

			Y_to[(x + ctx->rx) + (y + ctx->ry) * ctx->rw] =
				Y_from[ctx->pos[sp + 0]] * ctx->factor[sp + 0] +
				Y_from[ctx->pos[sp + 1]] * ctx->factor[sp + 1] +
				Y_from[ctx->pos[sp + 2]] * ctx->factor[sp + 2] +
				Y_from[ctx->pos[sp + 3]] * ctx->factor[sp + 3];
		}
	}

	for (y = 0; y < ch; y++) {
		for (x = 0; x < cw; x++) {
			sp = ctx->sw * ctx->sh * 4 + (x + y * cw) * 4;

			Cb_to[(x + ctx->rx / 2) + (y + ctx->ry / 2) * (ctx->rw / 2)] =
				Cb_from[ctx->pos[sp + 0]] * ctx->factor[sp + 0] +
				Cb_from[ctx->pos[sp + 1]] * ctx->factor[sp + 1] +
				Cb_from[ctx->pos[sp + 2]] * ctx->factor[sp + 2] +
				Cb_from[ctx->pos[sp + 3]] * ctx->factor[sp + 3];

			Cr_to[(x + ctx->rx / 2) + (y + ctx->ry / 2) * (ctx->rw / 2)] =
				Cr_from[ctx->pos[sp + 0]] * ctx->factor[sp + 0] +
				Cr_from[ctx->pos[sp + 1]] * ctx->factor[sp + 1] +
				Cr_from[ctx->pos[sp + 2]] * ctx->factor[sp + 2] +
				Cr_from[ctx->pos[sp + 3]] * ctx->factor[sp + 3];
		}
	}
}

int scale_ctx_msg(struct scale_private_s *scale, glc_ctx_message_t *ctx_msg, glc_thread_state_t *state)
{
	struct scale_ctx_s *ctx;
	glc_flags_t old_flags;

	scale_get_ctx(scale, ctx_msg->ctx, &ctx);
	pthread_rwlock_wrlock(&ctx->update);

	old_flags = ctx->flags;
	ctx->flags = ctx_msg->flags;
	ctx->w = ctx_msg->w;
	ctx->h = ctx_msg->h;

	if (scale->glc->flags & GLC_SCALE_SIZE) {
		ctx->rw = scale->glc->scale_width;
		ctx->rh = scale->glc->scale_height;

		if ((float) ctx->rw / (float) ctx->w < (float) ctx->rh / (float) ctx->h)
			ctx->scale = (float) ctx->rw / (float) ctx->w;
		else
			ctx->scale = (float) ctx->rh / (float) ctx->h;

		ctx->sw = ctx->scale * ctx->w;
		ctx->sh = ctx->scale * ctx->h;
		ctx->rx = (ctx->rw - ctx->sw) / 2;
		ctx->ry = (ctx->rh - ctx->sh) / 2;
		util_log(scale->glc, GLC_DEBUG, "scale",
			 "real size is %ux%u, scaled picture starts at %ux%u",
			 ctx->rw, ctx->rh, ctx->rx, ctx->ry);
	} else {
		ctx->scale = scale->glc->scale;
		ctx->sw = ctx->scale * ctx->w;
		ctx->sh = ctx->scale * ctx->h;

		ctx->rx = ctx->ry = 0;
		ctx->rw = ctx->sw;
		ctx->rh = ctx->sh;
	}

	if ((ctx_msg->flags & GLC_CTX_BGRA) | (ctx_msg->flags & GLC_CTX_BGR)) {
		if (ctx_msg->flags & GLC_CTX_BGRA)
			ctx->bpp = 4;
		else
			ctx->bpp = 3;

		ctx->row = ctx->w * ctx->bpp;

		if (ctx_msg->flags & GLC_CTX_DWORD_ALIGNED) {
			if (ctx->row % 8 != 0)
				ctx->row += 8 - ctx->row % 8;
			ctx_msg->flags &= ~GLC_CTX_DWORD_ALIGNED;
		}
	}

	ctx->proc = NULL; /* do not try anything stupid... */

	if ((ctx_msg->flags & GLC_CTX_BGR) | (ctx_msg->flags & GLC_CTX_BGRA)) {
		if ((ctx->scale == 0.5) && !(scale->glc->flags & GLC_SCALE_SIZE)) {
			util_log(scale->glc, GLC_DEBUG, "scale",
				 "scaling RGB data to half-size (from %ux%u to %ux%u)",
				 ctx->w, ctx->h, ctx->sw, ctx->sh);
			ctx->proc = scale_rgb_half;
		} else if ((ctx->scale == 1.0) && (ctx_msg->flags & GLC_CTX_BGRA)) {
			util_log(scale->glc, GLC_DEBUG, "scale", "converting BGRA to BGR");
			ctx->proc = scale_rgb_convert;
		} else if (ctx->scale != 1.0) {
			util_log(scale->glc, GLC_DEBUG, "scale",
				 "scaling RGB data with factor %f (from %ux%u to %ux%u)",
				 ctx->scale, ctx->w, ctx->h, ctx->sw, ctx->sh);
			ctx->proc = scale_rgb_scale;
			scale_generate_rgb_map(scale, ctx);
		}

		ctx_msg->flags &= ~GLC_CTX_BGRA; /* conversion is always done */
		ctx_msg->flags |= GLC_CTX_BGR;
		ctx_msg->w = ctx->rw;
		ctx_msg->h = ctx->rh;
		ctx->size = ctx->rw * ctx->rh * 3;

		if ((scale->glc->flags & GLC_SCALE_SIZE) &&
		    (ctx_msg->flags & GLC_CTX_UPDATE) &&
		    ((old_flags & ~(GLC_CTX_CREATE | GLC_CTX_UPDATE)) ==
		     (ctx_msg->flags & ~(GLC_CTX_CREATE | GLC_CTX_UPDATE))))
			state->flags |= GLC_THREAD_STATE_SKIP_WRITE;
	} else if (ctx_msg->flags & GLC_CTX_YCBCR_420JPEG) {
		ctx->sw -= ctx->sw % 2;
		ctx->sh -= ctx->sh % 2;
		ctx_msg->w = ctx->rw;
		ctx_msg->h = ctx->rh;
		ctx->size = ctx->rw * ctx->rh + 2 * ((ctx->rw / 2) * (ctx->rh / 2));

		if ((ctx->scale == 0.5) && !(scale->glc->flags & GLC_SCALE_SIZE)) {
			util_log(scale->glc, GLC_DEBUG, "scale",
				 "scaling Y'CbCr data to half-size (from %ux%u to %ux%u)",
				 ctx->w, ctx->h, ctx->sw, ctx->sh);
			ctx->proc = scale_ycbcr_half;
		} else if (ctx->scale != 1.0) {
			util_log(scale->glc, GLC_DEBUG, "scale",
				 "scaling Y'CbCr data with factor %f (from %ux%u to %ux%u)",
				 ctx->scale, ctx->w, ctx->h, ctx->sw, ctx->sh);
			ctx->proc = scale_ycbcr_scale;
			scale_generate_ycbcr_map(scale, ctx);
		}

		if ((scale->glc->flags & GLC_SCALE_SIZE) &&
		    (ctx_msg->flags & GLC_CTX_UPDATE) &&
		    ((old_flags & ~(GLC_CTX_CREATE | GLC_CTX_UPDATE)) ==
		     (ctx_msg->flags & ~(GLC_CTX_CREATE | GLC_CTX_UPDATE))))
			state->flags |= GLC_THREAD_STATE_SKIP_WRITE;
	}

	state->flags |= GLC_THREAD_COPY;

	pthread_rwlock_unlock(&ctx->update);
	return 0;
}

int scale_generate_rgb_map(struct scale_private_s *scale, struct scale_ctx_s *ctx)
{
	float ofx, ofy, fx0, fx1, fy0, fy1;
	unsigned int tp, x, y, r;
	float d;
	size_t smap_size = ctx->sw * ctx->sh * 3 * 4;

	util_log(scale->glc, GLC_DEBUG, "scale", "generating %zd + %zd byte scale map for ctx %d",
		 smap_size * sizeof(unsigned int), smap_size * sizeof(float), ctx->ctx);

	if (ctx->pos)
		ctx->pos = (unsigned int *) realloc(ctx->pos, sizeof(unsigned int) * smap_size);
	else
		ctx->pos = (unsigned int *) malloc(sizeof(unsigned int) * smap_size);
	if (ctx->factor)
		ctx->factor = (float *) realloc(ctx->factor, sizeof(float) * smap_size);
	else
		ctx->factor = (float *) malloc(sizeof(float) * smap_size);

	r = 0;
	do {
		d = (float) (ctx->w - r++) / (float) ctx->sw;
		util_log(scale->glc, GLC_DEBUG, "scale", "d = %f", d);
	} while ((d * (float) (ctx->sh - 1) + 1.0 > ctx->h) |
		 (d * (float) (ctx->sw - 1) + 1.0 > ctx->w));

	ofx = ofy = 0;
	for (y = 0; y < ctx->sh; y++) {
		for (x = 0; x < ctx->sw; x++) {
			tp = (x + y * ctx->sw) * 4;

			ctx->pos[tp + 0] = ((unsigned int) ofx + 0) * ctx->bpp +
			                   ((unsigned int) ofy + 0) * ctx->row;
			ctx->pos[tp + 1] = ((unsigned int) ofx + 1) * ctx->bpp +
			                   ((unsigned int) ofy + 0) * ctx->row;
			ctx->pos[tp + 2] = ((unsigned int) ofx + 0) * ctx->bpp +
			                   ((unsigned int) ofy + 1) * ctx->row;
			ctx->pos[tp + 3] = ((unsigned int) ofx + 1) * ctx->bpp +
			                   ((unsigned int) ofy + 1) * ctx->row;

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

int scale_generate_ycbcr_map(struct scale_private_s *scale, struct scale_ctx_s *ctx)
{
	float ofx, ofy, fx0, fx1, fy0, fy1;
	unsigned int tp, x, y, r, cw, ch;
	float d;
	size_t smap_size = ctx->sw * ctx->sh * 5; /* yw*yh*4 + (ch*cw)*4, ch = yh/2, cw = yw/2 */

	util_log(scale->glc, GLC_DEBUG, "scale", "generating %zd B + %zd B scale map for ctx %d",
		 smap_size * sizeof(unsigned int), smap_size * sizeof(float), ctx->ctx);

	if (ctx->pos)
		ctx->pos = (unsigned int *) realloc(ctx->pos, sizeof(unsigned int) * smap_size);
	else
		ctx->pos = (unsigned int *) malloc(sizeof(unsigned int) * smap_size);
	if (ctx->factor)
		ctx->factor = (float *) realloc(ctx->factor, sizeof(float) * smap_size);
	else
		ctx->factor = (float *) malloc(sizeof(float) * smap_size);

	r = 0;
	do {
		d = (float) (ctx->w - r++) / (float) ctx->sw;
		util_log(scale->glc, GLC_DEBUG, "scale", "Y: d = %f", d);
	} while ((d * (float) (ctx->sh - 1) + 1.0 > ctx->h) |
		 (d * (float) (ctx->sw - 1) + 1.0 > ctx->w));

	ofx = ofy = 0;
	for (y = 0; y < ctx->sh; y++) {
		for (x = 0; x < ctx->sw; x++) {
			tp = (x + y * ctx->sw) * 4;

			ctx->pos[tp + 0] = ((unsigned int) ofx + 0) +
			                   ((unsigned int) ofy + 0) * ctx->w;
			ctx->pos[tp + 1] = ((unsigned int) ofx + 1) +
			                   ((unsigned int) ofy + 0) * ctx->w;
			ctx->pos[tp + 2] = ((unsigned int) ofx + 0) +
			                   ((unsigned int) ofy + 1) * ctx->w;
			ctx->pos[tp + 3] = ((unsigned int) ofx + 1) +
			                   ((unsigned int) ofy + 1) * ctx->w;

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

	cw = ctx->sw / 2;
	ch = ctx->sh / 2;
	r = (r < 2) ? (0) : (r - 2);
	do {
		d = (float) ((ctx->w / 2) - r++) / (float) cw;
		util_log(scale->glc, GLC_DEBUG, "scale", "C: d = %f", d);
	} while ((d * (float) (ch - 1) + 1.0 > (ctx->h / 2)) |
		 (d * (float) (cw - 1) + 1.0 > (ctx->w / 2)));

	ofx = ofy = 0;
	for (y = 0; y < ch; y++) {
		for (x = 0; x < cw; x++) {
			tp = ctx->sw * ctx->sh * 4 + (x + y * cw) * 4;

			ctx->pos[tp + 0] = ((unsigned int) ofx + 0) +
			                   ((unsigned int) ofy + 0) * (ctx->w / 2);
			ctx->pos[tp + 1] = ((unsigned int) ofx + 1) +
			                   ((unsigned int) ofy + 0) * (ctx->w / 2);
			ctx->pos[tp + 2] = ((unsigned int) ofx + 0) +
			                   ((unsigned int) ofy + 1) * (ctx->w / 2);
			ctx->pos[tp + 3] = ((unsigned int) ofx + 1) +
			                   ((unsigned int) ofy + 1) * (ctx->w / 2);

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
