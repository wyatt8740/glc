/**
 * \file src/stream/ycbcr.c
 * \brief convert BGR to Y'CbCr and scale
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

 /* yuv.c -- convert BGR to Y'CbCr and scale
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
#include "ycbcr.h"

/**
 * \addtogroup stream
 *  \{
 */

/**
 * \defgroup ycbcr convert BGR to Y'CbCr and scale
 *  \{
 */

/*
http://en.wikipedia.org/wiki/YCbCr:
JPEG-Y'CbCr (601)

Y' =       + 0.299    * R'd + 0.587    * G'd + 0.114    * B'd
Cb = 128   - 0.168736 * R'd - 0.331264 * G'd + 0.5      * B'd
Cr = 128   + 0.5      * R'd - 0.418688 * G'd - 0.081312 * B'd
R'd, G'd, B'd   in {0, 1, 2, ..., 255}
Y', Cb, Cr      in {0, 1, 2, ..., 255}
*/

/*
#define RGB_TO_YCbCrJPEG_Y(Rd, Gd, Bd) \
	(    + 0.299    * (Rd) +    0.587 * (Gd) + 0.114 *    (Bd))
#define RGB_TO_YCbCrJPEG_Cb(Rd, Gd, Bd) \
	(128 - 0.168736 * (Rd) - 0.331264 * (Gd) + 0.5      * (Bd))
#define RGB_TO_YCbCrJPEG_Cr(Rd, Gd, Bd) \
	(128 + 0.5      * (Rd) - 0.418688 * (Gd) - 0.081312 * (Bd))
*/

#define RGB_TO_YCbCrJPEG_Y(Rd, Gd, Bd) \
	(    + ((306 * (Rd) + 601 * (Gd) + 117 * (Bd)) >> 10))
#define RGB_TO_YCbCrJPEG_Cb(Rd, Gd, Bd) \
	(128 - ((173 * (Rd) + 339 * (Gd) - 512 * (Bd)) >> 10))
#define RGB_TO_YCbCrJPEG_Cr(Rd, Gd, Bd) \
	(128 + ((512 * (Rd) - 429 * (Gd) -  83 * (Bd)) >> 10))

struct ycbcr_ctx_s;
struct ycbcr_private_s;

typedef void (*YCbCrConvertProc)(struct ycbcr_private_s *ycbcr,
				 struct ycbcr_ctx_s *ctx,
				 unsigned char *from,
				 unsigned char *to);

struct ycbcr_ctx_s {
	glc_ctx_i ctx_i;
	unsigned int w, h, bpp;
	unsigned int yw, yh;
	unsigned int cw, ch;
	unsigned int row;
	double scale;
	size_t size;

	unsigned int *pos;
	float *factor;

	YCbCrConvertProc convert;

	pthread_rwlock_t update;
	struct ycbcr_ctx_s *next;
};

struct ycbcr_private_s {
	glc_t *glc;
	glc_thread_t thread;

	struct ycbcr_ctx_s *ctx;
};

int ycbcr_read_callback(glc_thread_state_t *state);
int ycbcr_write_callback(glc_thread_state_t *state);
void ycbcr_finish_callback(void *ptr, int err);

int ycbcr_ctx_msg(struct ycbcr_private_s *ycbcr, glc_ctx_message_t *ctx_msg);
void ycbcr_get_ctx(struct ycbcr_private_s *ycbcr, glc_ctx_i ctx_i, struct ycbcr_ctx_s **ctx);

int ycbcr_generate_map(struct ycbcr_private_s *ycbcr, struct ycbcr_ctx_s *ctx);

void ycbcr_bgr_to_jpeg420(struct ycbcr_private_s *ycbcr, struct ycbcr_ctx_s *ctx,
			  unsigned char *from, unsigned char *to);
void ycbcr_bgr_to_jpeg420_half(struct ycbcr_private_s *ycbcr, struct ycbcr_ctx_s *ctx,
			       unsigned char *from, unsigned char *to);
void ycbcr_bgr_to_jpeg420_scale(struct ycbcr_private_s *ycbcr, struct ycbcr_ctx_s *ctx,
				unsigned char *from, unsigned char *to);

int ycbcr_init(glc_t *glc, ps_buffer_t *from, ps_buffer_t *to)
{
	struct ycbcr_private_s *ycbcr = malloc(sizeof(struct ycbcr_private_s));
	memset(ycbcr, 0, sizeof(struct ycbcr_private_s));

	ycbcr->glc = glc;

	ycbcr->thread.flags = GLC_THREAD_READ | GLC_THREAD_WRITE;
	ycbcr->thread.read_callback = &ycbcr_read_callback;
	ycbcr->thread.write_callback = &ycbcr_write_callback;
	ycbcr->thread.finish_callback = &ycbcr_finish_callback;
	ycbcr->thread.ptr = ycbcr;
	ycbcr->thread.threads = util_cpus();

	return glc_thread_create(glc, &ycbcr->thread, from, to);
}

void ycbcr_finish_callback(void *ptr, int err)
{
	struct ycbcr_private_s *ycbcr = ptr;
	struct ycbcr_ctx_s *del;

	if (err)
		fprintf(stderr, "ycbcr failed: %s (%d)\n", strerror(err), err);

	while (ycbcr->ctx != NULL) {
		del = ycbcr->ctx;
		ycbcr->ctx = ycbcr->ctx->next;

		if (del->pos)
			free(del->pos);
		if (del->factor)
			free(del->factor);

		pthread_rwlock_destroy(&del->update);
		free(del);
	}

	sem_post(&ycbcr->glc->signal[GLC_SIGNAL_YCBCR_FINISHED]);
	free(ycbcr);
}

int ycbcr_read_callback(glc_thread_state_t *state)
{
	struct ycbcr_private_s *ycbcr = state->ptr;
	struct ycbcr_ctx_s *ctx;
	glc_picture_header_t *pic_hdr;

	if (state->header.type == GLC_MESSAGE_CTX)
		ycbcr_ctx_msg(ycbcr, (glc_ctx_message_t *) state->read_data);

	if (state->header.type == GLC_MESSAGE_PICTURE) {
		pic_hdr = (glc_picture_header_t *) state->read_data;
		ycbcr_get_ctx(ycbcr, pic_hdr->ctx, &ctx);
		state->threadptr = ctx;

		pthread_rwlock_rdlock(&ctx->update);

		if (ctx->convert != NULL)
			state->write_size = GLC_PICTURE_HEADER_SIZE + ctx->size;
		else {
			state->flags |= GLC_THREAD_COPY;
			pthread_rwlock_unlock(&ctx->update);
		}
	} else
		state->flags |= GLC_THREAD_COPY;

	return 0;
}

int ycbcr_write_callback(glc_thread_state_t *state)
{
	struct ycbcr_private_s *ycbcr = state->ptr;
	struct ycbcr_ctx_s *ctx = state->threadptr;

	memcpy(state->write_data, state->read_data, GLC_PICTURE_HEADER_SIZE);
	ctx->convert(ycbcr, ctx,
		     (unsigned char *) &state->read_data[GLC_PICTURE_HEADER_SIZE],
		     (unsigned char *) &state->write_data[GLC_PICTURE_HEADER_SIZE]);
	pthread_rwlock_unlock(&ctx->update);

	return 0;
}

void ycbcr_get_ctx(struct ycbcr_private_s *ycbcr, glc_ctx_i ctx_i, struct ycbcr_ctx_s **ctx)
{
	*ctx = ycbcr->ctx;

	while (*ctx != NULL) {
		if ((*ctx)->ctx_i == ctx_i)
			break;
		*ctx = (*ctx)->next;
	}

	if (*ctx == NULL) {
		*ctx = malloc(sizeof(struct ycbcr_ctx_s));
		memset(*ctx, 0, sizeof(struct ycbcr_ctx_s));

		(*ctx)->next = ycbcr->ctx;
		ycbcr->ctx = *ctx;
		(*ctx)->ctx_i = ctx_i;
		pthread_rwlock_init(&(*ctx)->update, NULL);
	}
}

void ycbcr_bgr_to_jpeg420(struct ycbcr_private_s *ycbcr, struct ycbcr_ctx_s *ctx,
			  unsigned char *from, unsigned char *to)
{
	unsigned int Cpix;
	unsigned int op1, op2, op3, op4;
	unsigned char Rd, Gd, Bd;
	unsigned int ox, oy, Yy, Yx;
	unsigned char *Y, *Cb, *Cr;

	Y = to;
	Cb = &to[ctx->yw * ctx->yh];
	Cr = &to[ctx->yw * ctx->yh + ctx->cw * ctx->ch];

	Cpix = 0;
	oy = (ctx->h - 2) * ctx->row;
	ox = 0;

	for (Yy = 0; Yy < ctx->yh; Yy += 2) {
		for (Yx = 0; Yx < ctx->yw; Yx += 2) {
			op1 = ox + oy;
			op2 = ox + oy + ctx->bpp;
			op3 = ox + oy + ctx->row;
			op4 = ox + oy + ctx->row + ctx->bpp;
			Rd = (from[op1 + 2] + from[op2 + 2] + from[op3 + 2] + from[op4 + 2]) >> 2;
			Gd = (from[op1 + 1] + from[op2 + 1] + from[op3 + 1] + from[op4 + 1]) >> 2;
			Bd = (from[op1 + 0] + from[op2 + 0] + from[op3 + 0] + from[op4 + 0]) >> 2;

			/* CbCr */
			Cb[Cpix  ] = RGB_TO_YCbCrJPEG_Cb(Rd, Gd, Bd);
			Cr[Cpix++] = RGB_TO_YCbCrJPEG_Cr(Rd, Gd, Bd);

			/* Y' */
			Y[(Yx + 0) + (Yy + 0) * ctx->yw] = RGB_TO_YCbCrJPEG_Y(from[op3 + 2],
									      from[op3 + 1],
									      from[op3 + 0]);
			Y[(Yx + 1) + (Yy + 0) * ctx->yw] = RGB_TO_YCbCrJPEG_Y(from[op4 + 2],
									      from[op4 + 1],
									      from[op4 + 0]);
			Y[(Yx + 0) + (Yy + 1) * ctx->yw] = RGB_TO_YCbCrJPEG_Y(from[op1 + 2],
									      from[op1 + 1],
									      from[op1 + 0]);
			Y[(Yx + 1) + (Yy + 1) * ctx->yw] = RGB_TO_YCbCrJPEG_Y(from[op2 + 2],
									      from[op2 + 1],
									      from[op2 + 0]);
			ox += ctx->bpp * 2;
		}
		ox = 0;
		oy -= 2 * ctx->row;
	}
}

#define CALC_BILINEAR_RGB(x0, x1, y0, y1) \
	op1 = (ox + x0) + (oy + y0) * ctx->row; \
	op2 = (ox + x1) + (oy + y0) * ctx->row; \
	op3 = (ox + x0) + (oy + y1) * ctx->row; \
	op4 = (ox + x1) + (oy + y1) * ctx->row; \
	Rd = (from[op1 + 2] + from[op2 + 2] + from[op3 + 2] + from[op4 + 2]) >> 2; \
	Gd = (from[op1 + 1] + from[op2 + 1] + from[op3 + 1] + from[op4 + 1]) >> 2; \
	Bd = (from[op1 + 0] + from[op2 + 0] + from[op3 + 0] + from[op4 + 0]) >> 2;

void ycbcr_bgr_to_jpeg420_half(struct ycbcr_private_s *ycbcr, struct ycbcr_ctx_s *ctx,
			       unsigned char *from, unsigned char *to)
{
	unsigned int Cpix;
	unsigned int op1, op2, op3, op4;
	unsigned char Rd, Gd, Bd;
	unsigned int ox, oy, Yy, Yx;
	unsigned char *Y, *Cb, *Cr;

	Y = to;
	Cb = &to[ctx->yw * ctx->yh];
	Cr = &to[ctx->yw * ctx->yh + ctx->cw * ctx->ch];

	Cpix = 0;
	oy = (ctx->h - 4);
	ox = 0;

	for (Yy = 0; Yy < ctx->yh; Yy += 2) {
		for (Yx = 0; Yx < ctx->yw; Yx += 2) {
			/* CbCr */
			CALC_BILINEAR_RGB(ctx->bpp, ctx->bpp * 2, 1, 2)
			Cb[Cpix  ] = RGB_TO_YCbCrJPEG_Cb(Rd, Gd, Bd);
			Cr[Cpix++] = RGB_TO_YCbCrJPEG_Cr(Rd, Gd, Bd);

			/* Y' */
			CALC_BILINEAR_RGB(0, ctx->bpp, 2, 3)
			Y[(Yx + 0) + (Yy + 0) * ctx->yw] = RGB_TO_YCbCrJPEG_Y(Rd, Gd, Bd);

			CALC_BILINEAR_RGB(ctx->bpp * 2, ctx->bpp * 3, 2, 3)
			Y[(Yx + 1) + (Yy + 0) * ctx->yw] = RGB_TO_YCbCrJPEG_Y(Rd, Gd, Bd);

			CALC_BILINEAR_RGB(0, ctx->bpp, 0, 1)
			Y[(Yx + 0) + (Yy + 1) * ctx->yw] = RGB_TO_YCbCrJPEG_Y(Rd, Gd, Bd);

			CALC_BILINEAR_RGB(ctx->bpp * 2, ctx->bpp * 3, 0, 1)
			Y[(Yx + 1) + (Yy + 1) * ctx->yw] = RGB_TO_YCbCrJPEG_Y(Rd, Gd, Bd);

			ox += ctx->bpp * 4;
		}
		ox = 0;
		oy -= 4;
	}
}

#undef CALC_BILINEAR_RGB

void ycbcr_bgr_to_jpeg420_scale(struct ycbcr_private_s *ycbcr, struct ycbcr_ctx_s *ctx,
				unsigned char *from, unsigned char *to)
{
	unsigned int Cpix;
	unsigned char *Y, *Cb, *Cr;
	unsigned int Cmap, Yy, Yx;
	unsigned char Rd, Gd, Bd;

	Y = to;
	Cb = &to[ctx->yw * ctx->yh];
	Cr = &to[ctx->yw * ctx->yh + ctx->cw * ctx->ch];

	Cpix = 0;
	Cmap = ctx->yw * ctx->yh;

#define CALC_Rd(m) (from[ctx->pos[m + 0] + 2] * ctx->factor[m + 0] \
		  + from[ctx->pos[m + 1] + 2] * ctx->factor[m + 1] \
		  + from[ctx->pos[m + 2] + 2] * ctx->factor[m + 2] \
		  + from[ctx->pos[m + 3] + 2] * ctx->factor[m + 3])
#define CALC_Bd(m) (from[ctx->pos[m + 0] + 1] * ctx->factor[m + 0] \
		  + from[ctx->pos[m + 1] + 1] * ctx->factor[m + 1] \
		  + from[ctx->pos[m + 2] + 1] * ctx->factor[m + 2] \
		  + from[ctx->pos[m + 3] + 1] * ctx->factor[m + 3])
#define CALC_Gd(m) (from[ctx->pos[m + 0] + 0] * ctx->factor[m + 0] \
		  + from[ctx->pos[m + 1] + 0] * ctx->factor[m + 1] \
		  + from[ctx->pos[m + 2] + 0] * ctx->factor[m + 2] \
		  + from[ctx->pos[m + 3] + 0] * ctx->factor[m + 3])

#define CALC_RdBdGd(m) \
	Rd = CALC_Rd((m) * 4); \
	Gd = CALC_Bd((m) * 4); \
	Bd = CALC_Gd((m) * 4);

	for (Yy = 0; Yy < ctx->yh; Yy += 2) {
		for (Yx = 0; Yx < ctx->yw; Yx += 2) {
			/* CbCr */
			CALC_RdBdGd(Cmap + Cpix)
			Cb[Cpix  ] = RGB_TO_YCbCrJPEG_Cb(Rd, Gd, Bd);
			Cr[Cpix++] = RGB_TO_YCbCrJPEG_Cr(Rd, Gd, Bd);

			/* Y' */
			CALC_RdBdGd((Yx + 0) + (Yy + 0) * ctx->yw)
			Y[(Yx + 0) + (Yy + 0) * ctx->yw] = RGB_TO_YCbCrJPEG_Y(Rd, Gd, Bd);

			CALC_RdBdGd((Yx + 1) + (Yy + 0) * ctx->yw)
			Y[(Yx + 1) + (Yy + 0) * ctx->yw] = RGB_TO_YCbCrJPEG_Y(Rd, Gd, Bd);

			CALC_RdBdGd((Yx + 0) + (Yy + 1) * ctx->yw)
			Y[(Yx + 0) + (Yy + 1) * ctx->yw] = RGB_TO_YCbCrJPEG_Y(Rd, Gd, Bd);

			CALC_RdBdGd((Yx + 1) + (Yy + 1) * ctx->yw)
			Y[(Yx + 1) + (Yy + 1) * ctx->yw] = RGB_TO_YCbCrJPEG_Y(Rd, Gd, Bd);
		}
	}
#undef Rd
#undef Gd
#undef Bd
}

int ycbcr_ctx_msg(struct ycbcr_private_s *ycbcr, glc_ctx_message_t *ctx_msg)
{
	struct ycbcr_ctx_s *ctx;

	ycbcr_get_ctx(ycbcr, ctx_msg->ctx, &ctx);
	pthread_rwlock_wrlock(&ctx->update);

	if (ctx_msg->flags & GLC_CTX_BGRA)
		ctx->bpp = 4;
	else if (ctx_msg->flags & GLC_CTX_BGR)
		ctx->bpp = 3;
	else {
		ctx->convert = NULL;
		pthread_rwlock_unlock(&ctx->update);
		return 0;
	}

	ctx->w = ctx_msg->w;
	ctx->h = ctx_msg->h;

	ctx->row = ctx->w * ctx->bpp;

	if (ctx_msg->flags & GLC_CTX_DWORD_ALIGNED) {
		if (ctx->row % 8 != 0)
			ctx->row += 8 - ctx->row % 8;
	}

	ctx->scale = ycbcr->glc->scale;
	ctx->yw = ctx->w * ctx->scale;
	ctx->yh = ctx->h * ctx->scale;
	ctx->yw -= ctx->yw % 2; /* safer and faster             */
	ctx->yh -= ctx->yh % 2; /* but we might drop a pixel... */

	ctx->cw = ctx->yw / 2;
	ctx->ch = ctx->yh / 2;

	/* nuke old flags */
	ctx_msg->flags &= ~(GLC_CTX_BGR | GLC_CTX_BGRA | GLC_CTX_DWORD_ALIGNED);
	ctx_msg->flags |= GLC_CTX_YCBCR_420JPEG;
	ctx_msg->w = ctx->yw;
	ctx_msg->h = ctx->yh;

	if (ctx->scale == 1.0)
		ctx->convert = &ycbcr_bgr_to_jpeg420;
	else if (ctx->scale == 0.5)
		ctx->convert = &ycbcr_bgr_to_jpeg420_half;
	else {
		ctx->convert = &ycbcr_bgr_to_jpeg420_scale;
		ycbcr_generate_map(ycbcr, ctx);
	}

	ctx->size = ctx->yw * ctx->yh + 2 * (ctx->cw * ctx->ch);

	pthread_rwlock_unlock(&ctx->update);
	return 0;
}

/* TODO smaller map is sometimes possible, should inflict better
        cache utilization => implement */
int ycbcr_generate_map(struct ycbcr_private_s *ycbcr, struct ycbcr_ctx_s *ctx)
{
	size_t scale_maps_size;
	unsigned int tp, x, y, r;
	float d, ofx, ofy, fx0, fx1, fy0, fy1;

	scale_maps_size = ctx->yw * ctx->yh * 4 + ctx->cw * ctx->ch * 4;

	if (ctx->pos)
		ctx->pos = (unsigned int *) realloc(ctx->pos, sizeof(unsigned int) * scale_maps_size);
	else
		ctx->pos = (unsigned int *) malloc(sizeof(unsigned int) * scale_maps_size);

	if (ctx->factor)
		ctx->factor = (float *) realloc(ctx->factor, sizeof(float) * scale_maps_size);
	else
		ctx->factor = (float *) malloc(sizeof(float) * scale_maps_size);

	/* Y' */
	/* NEVER trust CPU with fp mathematics :/ */
	r = 0;
	do {
		d = (float) (ctx->w - r++) / (float) ctx->yw;
	} while ((d * (float) (ctx->yh - 1) + 1.0 > ctx->h) |
		 (d * (float) (ctx->yw - 1) + 1.0 > ctx->w));

	ofx = ofy = 0;

	for (y = 0; y < ctx->yh; y++) {
		for (x = 0; x < ctx->yw; x++) {
			tp = (x + y * ctx->yw) * 4;

			ctx->pos[tp + 0] = ((unsigned int) ofx + 0) * ctx->bpp +
			                   (ctx->h - 1 - (unsigned int) ofy) * ctx->row;
			ctx->pos[tp + 1] = ((unsigned int) ofx + 1) * ctx->bpp +
			                   (ctx->h - 1 - (unsigned int) ofy) * ctx->row;
			ctx->pos[tp + 2] = ((unsigned int) ofx + 0) * ctx->bpp +
			                   (ctx->h - 2 - (unsigned int) ofy) * ctx->row;
			ctx->pos[tp + 3] = ((unsigned int) ofx + 1) * ctx->bpp +
			                   (ctx->h - 2 - (unsigned int) ofy) * ctx->row;

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

	/* CbCr */
	/* try to match Y */
	r = (r < 2) ? (0) : (r - 2);
	do {
		d = (float) (ctx->w - r++) / (float) ctx->cw;
	} while ((d * (float) (ctx->ch - 1) + 1.0 > ctx->h) |
		 (d * (float) (ctx->cw - 1) + 1.0 > ctx->w));

	ofx = ofy = 0;

	for (y = 0; y < ctx->ch; y++) {
		for (x = 0; x < ctx->cw; x++) {
			tp = (ctx->yw * ctx->yh * 4) + (x + y * ctx->cw) * 4;

			ctx->pos[tp + 0] = ((unsigned int) ofx + 0) * ctx->bpp +
			                   (ctx->h - 1 - (unsigned int) ofy) * ctx->row;
			ctx->pos[tp + 1] = ((unsigned int) ofx + 1) * ctx->bpp +
			                   (ctx->h - 1 - (unsigned int) ofy) * ctx->row;
			ctx->pos[tp + 2] = ((unsigned int) ofx + 0) * ctx->bpp +
			                   (ctx->h - 2 - (unsigned int) ofy) * ctx->row;
			ctx->pos[tp + 3] = ((unsigned int) ofx + 1) * ctx->bpp +
			                   (ctx->h - 2 - (unsigned int) ofy) * ctx->row;

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
