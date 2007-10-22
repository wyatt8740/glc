/**
 * \file src/stream/rgb.c
 * \brief convert Y'CbCr to BGR
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* rgb.c -- convert Y'CbCr to BGR
 * Copyright (C) 2007 Pyry Haulos
 * For conditions of distribution and use, see copyright notice in glc.h
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <packetstream.h>

#include "../common/glc.h"
#include "../common/thread.h"
#include "../common/util.h"
#include "rgb.h"

/**
 * \addtogroup stream
 *  \{
 */

/**
 * \defgroup rgb convert Y'CbCr to BGR
 *  \{
 */

/*
R'd = Y' + (Cr - 128) * (2 - 2 * Kr)
G'd = Y' - (Cr - 128) * ((2 * Kr - 2 * Kr^2) / (1 - Kr - Kb))
         - (Cb - 128) * ((2 * Kb - 2 * Kb^2) / (1 - Kr - Kb))
B'd = Y' + (Cb - 128) * (2 - 2 * Kb)
*/

/* As accurate as it can be. Unfortunately most players/converters
   get this very wrong. */
/*#define YCbCrJPEG_TO_RGB_Rd(Y, Cb, Cr) \
	((Y) + 1.402 * ((Cr) - 128))
#define YCbCrJPEG_TO_RGB_Gd(Y, Cb, Cr) \
	((Y) - 0.344136 * ((Cb) - 128) - 0.714136 * ((Cr) - 128))
#define YCbCrJPEG_TO_RGB_Bd(Y, Cb, Cr) \
	((Y) + 1.772 * ((Cb) - 128))*/

/* approximative */
/*#define YCbCrJPEG_TO_RGB_Rd(Y, Cb, Cr) \
	((Y) + ((1436 * (Cr)) >> 10) - 179)
#define YCbCrJPEG_TO_RGB_Gd(Y, Cb, Cr) \
	((Y) - ((360853 * (Cb) + 748825 * (Cr)) >> 20) + 135)
#define YCbCrJPEG_TO_RGB_Bd(Y, Cb, Cr) \
	((Y) + ((1814 * (Cb)) >> 10) - 227)*/

#define LOOKUP_BITS 7
#define LOOKUP_POS(Rd, Gd, Bd) \
	(((((Rd) >> (8 - LOOKUP_BITS)) << (LOOKUP_BITS * 2)) + \
	  (((Gd) >> (8 - LOOKUP_BITS)) << LOOKUP_BITS) + \
	  ( (Bd) >> (8 - LOOKUP_BITS))) * 3)

/* FIXME why do overflows occur? */
#define CLAMP_256(val) \
	(val) < 0 ? 0 : ((val) > 255 ? 255 : (val))

unsigned char YCbCrJPEG_TO_RGB_Rd(unsigned char Y, unsigned char Cb, unsigned char Cr)
{
	int R = Y + 1.402 * (Cr - 128);
	return CLAMP_256(R);
}

unsigned char YCbCrJPEG_TO_RGB_Gd(unsigned char Y, unsigned char Cb, unsigned char Cr)
{
	int G = Y - 0.344136 * (Cb - 128) - 0.714136 * (Cr - 128);
	return CLAMP_256(G);
}

unsigned char YCbCrJPEG_TO_RGB_Bd(unsigned char Y, unsigned char Cb, unsigned char Cr)
{
	int B = Y + 1.772 * (Cb - 128);
	return CLAMP_256(B);
}

struct rgb_ctx_s {
	glc_ctx_i ctx_i;
	unsigned int w, h;
	int convert;
	size_t size;
	
	pthread_rwlock_t update;
	struct rgb_ctx_s *next;
};

struct rgb_private_s {
	glc_t *glc;
	glc_thread_t thread;

	unsigned char *lookup_table;

	struct rgb_ctx_s *ctx;
};

int rgb_read_callback(glc_thread_state_t *state);
int rgb_write_callback(glc_thread_state_t *state);
void rgb_finish_callback(void *ptr, int err);

void rgb_get_ctx(struct rgb_private_s *rgb, glc_ctx_i ctx_i,
		struct rgb_ctx_s **ctx);

int rgb_ctx_msg(struct rgb_private_s *rgb, glc_ctx_message_t *ctx_msg);
int rgb_convert(struct rgb_private_s *rgb, struct rgb_ctx_s *ctx,
		unsigned char *from, unsigned char *to);

int rgb_init_lookup(struct rgb_private_s *rgb);
int rgb_convert_lookup(struct rgb_private_s *rgb, struct rgb_ctx_s *ctx,
		       unsigned char *from, unsigned char *to);


int rgb_init(glc_t *glc, ps_buffer_t *from, ps_buffer_t *to)
{
	struct rgb_private_s *rgb = malloc(sizeof(struct rgb_private_s));
	memset(rgb, 0, sizeof(struct rgb_private_s));

	rgb->glc = glc;

	rgb_init_lookup(rgb);

	rgb->thread.flags = GLC_THREAD_READ | GLC_THREAD_WRITE;
	rgb->thread.read_callback = &rgb_read_callback;
	rgb->thread.write_callback = &rgb_write_callback;
	rgb->thread.finish_callback = &rgb_finish_callback;
	rgb->thread.ptr = rgb;
	rgb->thread.threads = util_cpus();

	return glc_thread_create(glc, &rgb->thread, from, to);
}

void rgb_finish_callback(void *ptr, int err)
{
	struct rgb_private_s *rgb = ptr;
	struct rgb_ctx_s *del;

	if (err)
		fprintf(stderr, "rgb failed: %s (%d)\n", strerror(err), err);

	while (rgb->ctx != NULL) {
		del = rgb->ctx;
		rgb->ctx = rgb->ctx->next;

		pthread_rwlock_destroy(&del->update);
		free(del);
	}

	if (rgb->lookup_table)
		free(rgb->lookup_table);


	sem_post(&rgb->glc->signal[GLC_SIGNAL_RGB_FINISHED]);
	free(rgb);
}

int rgb_read_callback(glc_thread_state_t *state)
{
	struct rgb_private_s *rgb = state->ptr;
	struct rgb_ctx_s *ctx;
	glc_picture_header_t *pic_hdr;

	if (state->header.type == GLC_MESSAGE_CTX)
		rgb_ctx_msg(rgb, (glc_ctx_message_t *) state->read_data);

	if (state->header.type == GLC_MESSAGE_PICTURE) {
		pic_hdr = (glc_picture_header_t *) state->read_data;
		rgb_get_ctx(rgb, pic_hdr->ctx, &ctx);
		state->threadptr = ctx;

		pthread_rwlock_rdlock(&ctx->update);

		if (ctx->convert)
			state->write_size = GLC_PICTURE_HEADER_SIZE + ctx->size;
		else {
			state->flags |= GLC_THREAD_COPY;
			pthread_rwlock_unlock(&ctx->update);
		}
	} else
		state->flags |= GLC_THREAD_COPY;

	return 0;
}

int rgb_write_callback(glc_thread_state_t *state)
{
	struct rgb_private_s *rgb = state->ptr;
	struct rgb_ctx_s *ctx = state->threadptr;

	memcpy(state->write_data, state->read_data, GLC_PICTURE_HEADER_SIZE);
	rgb_convert_lookup(rgb, ctx,
		    (unsigned char *) &state->read_data[GLC_PICTURE_HEADER_SIZE],
		    (unsigned char *) &state->write_data[GLC_PICTURE_HEADER_SIZE]);
	pthread_rwlock_unlock(&ctx->update);

	return 0;
}

void rgb_get_ctx(struct rgb_private_s *rgb, glc_ctx_i ctx_i,
		struct rgb_ctx_s **ctx)
{
	*ctx = rgb->ctx;

	while (*ctx != NULL) {
		if ((*ctx)->ctx_i == ctx_i)
			break;
		*ctx = (*ctx)->next;
	}

	if (*ctx == NULL) {
		*ctx = malloc(sizeof(struct rgb_ctx_s));
		memset(*ctx, 0, sizeof(struct rgb_ctx_s));
		
		(*ctx)->next = rgb->ctx;
		rgb->ctx = *ctx;
		(*ctx)->ctx_i = ctx_i;
		pthread_rwlock_init(&(*ctx)->update, NULL);
	}
}

int rgb_ctx_msg(struct rgb_private_s *rgb, glc_ctx_message_t *ctx_msg)
{
	struct rgb_ctx_s *ctx;
	rgb_get_ctx(rgb, ctx_msg->ctx, &ctx);

	if (!(ctx_msg->flags & GLC_CTX_YCBCR_420JPEG))
		return 0; /* just don't convert */
	
	pthread_rwlock_wrlock(&ctx->update);

	ctx->w = ctx_msg->w;
	ctx->h = ctx_msg->h;
	ctx->size = ctx->w * ctx->h * 3; /* convert to BGR */
	ctx->convert = 1;

	ctx_msg->flags &= ~GLC_CTX_YCBCR_420JPEG;
	ctx_msg->flags |= GLC_CTX_BGR;

	pthread_rwlock_unlock(&ctx->update);

	return 0;
}

int rgb_convert(struct rgb_private_s *rgb, struct rgb_ctx_s *ctx,
		unsigned char *from, unsigned char *to)
{
	unsigned int x, y, Cpix;
	unsigned char *Y, *Cb, *Cr;

	Y = from;
	Cb = &from[ctx->h * ctx->w];
	Cr = &from[ctx->h * ctx->w + (ctx->h / 2) * (ctx->w / 2)];
	Cpix = 0;

#define CONVERT(xadd, yrgbadd, yadd) 								  \
	to[((x + (xadd)) + ((ctx->h - y) + (yrgbadd)) * ctx->w) * 3 + 2] = 			  \
		YCbCrJPEG_TO_RGB_Rd(Y[(x + (xadd)) + (y + (yadd)) * ctx->w], Cb[Cpix], Cr[Cpix]); \
	to[((x + (xadd)) + ((ctx->h - y) + (yrgbadd)) * ctx->w) * 3 + 1] = 			  \
		YCbCrJPEG_TO_RGB_Gd(Y[(x + (xadd)) + (y + (yadd)) * ctx->w], Cb[Cpix], Cr[Cpix]); \
	to[((x + (xadd)) + ((ctx->h - y) + (yrgbadd)) * ctx->w) * 3 + 0] = 			  \
		YCbCrJPEG_TO_RGB_Bd(Y[(x + (xadd)) + (y + (yadd)) * ctx->w], Cb[Cpix], Cr[Cpix]);

	/* YCBCR_420JPEG frame dimensions are always divisible by two */
	for (y = 0; y < ctx->h; y += 2) {
		for (x = 0; x < ctx->w; x += 2) {
			CONVERT(0, -1, 0)
			CONVERT(1, -1, 0)
			CONVERT(0, -2, 1)
			CONVERT(1, -2, 1)
			
			Cpix++;
		}
	}
#undef CONVERT
	return 0;
}

int rgb_init_lookup(struct rgb_private_s *rgb)
{
	unsigned int Y, Cb, Cr, color;
	unsigned int lookup_size = (1 << LOOKUP_BITS) * (1 << LOOKUP_BITS) * (1 << LOOKUP_BITS) * 3;

	rgb->lookup_table = malloc(lookup_size);

	color = 0;
	for (Y = 0; Y < 256; Y += (1 << (8 - LOOKUP_BITS))) {
		for (Cb = 0; Cb < 256; Cb += (1 << (8 - LOOKUP_BITS))) {
			for (Cr = 0; Cr < 256; Cr += (1 << (8 - LOOKUP_BITS))) {
				rgb->lookup_table[color + 0] = YCbCrJPEG_TO_RGB_Rd(Y, Cb, Cr);
				rgb->lookup_table[color + 1] = YCbCrJPEG_TO_RGB_Gd(Y, Cb, Cr);
				rgb->lookup_table[color + 2] = YCbCrJPEG_TO_RGB_Bd(Y, Cb, Cr);
				color += 3;
			}
		}
	}
	return 0;
}

int rgb_convert_lookup(struct rgb_private_s *rgb, struct rgb_ctx_s *ctx,
		       unsigned char *from, unsigned char *to)
{
	unsigned int x, y, Cpix;
	unsigned int color;
	unsigned char *Y, *Cb, *Cr;

	Y = from;
	Cb = &from[ctx->h * ctx->w];
	Cr = &from[ctx->h * ctx->w + (ctx->h / 2) * (ctx->w / 2)];
	Cpix = 0;

#define CONVERT(xadd, yrgbadd, yadd) 						\
	color = LOOKUP_POS(Y[(x + (xadd)) + (y + (yadd)) * ctx->w],		\
			   Cb[Cpix], Cr[Cpix]);					\
	to[((x + (xadd)) + ((ctx->h - y) + (yrgbadd)) * ctx->w) * 3 + 2] =	\
		rgb->lookup_table[color + 0];					\
	to[((x + (xadd)) + ((ctx->h - y) + (yrgbadd)) * ctx->w) * 3 + 1] =	\
		rgb->lookup_table[color + 1];					\
	to[((x + (xadd)) + ((ctx->h - y) + (yrgbadd)) * ctx->w) * 3 + 0] =	\
		rgb->lookup_table[color + 2];

	/* YCBCR_420JPEG frame dimensions are always divisible by two */
	for (y = 0; y < ctx->h; y += 2) {
		for (x = 0; x < ctx->w; x += 2) {
			CONVERT(0, -1, 0)
			CONVERT(1, -1, 0)
			CONVERT(0, -2, 1)
			CONVERT(1, -2, 1)
			
			Cpix++;
		}
	}
#undef CONVERT
	return 0;
}

/**  \} */
/**  \} */
