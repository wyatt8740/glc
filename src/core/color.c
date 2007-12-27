/**
 * \file src/core/color.c
 * \brief color correction
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup core
 *  \{
 * \defgroup color color correction
 *  \{
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <packetstream.h>
#include <errno.h>
#include <math.h>

#include "../common/glc.h"
#include "../common/thread.h"
#include "../common/util.h"
#include "color.h"

#define LOOKUP_BITS 8
#define YCBCR_LOOKUP_POS(Y, Cb, Cr) \
	(((((Y)  >> (8 - LOOKUP_BITS)) << (LOOKUP_BITS * 2)) + \
	  (((Cb) >> (8 - LOOKUP_BITS)) << LOOKUP_BITS) + \
	  ( (Cr) >> (8 - LOOKUP_BITS))) * 3)

struct color_private_s;
struct color_ctx_s;

typedef void (*color_proc)(struct color_private_s *color,
			   struct color_ctx_s *ctx,
			   unsigned char *from, unsigned char *to);

struct color_ctx_s {
	glc_ctx_i ctx_i;
	glc_flags_t flags;
	unsigned int w, h;

	unsigned int bpp, row;

	float brightness, contrast;
	float red_gamma, green_gamma, blue_gamma;

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

int color_ctx_msg(struct color_private_s *color, glc_ctx_message_t *msg);
int color_color_msg(struct color_private_s *color, glc_color_message_t *msg);

int color_generate_ycbcr_lookup_table(struct color_private_s *color,
				      struct color_ctx_s *ctx);
int color_generate_rgb_lookup_table(struct color_private_s *color,
				    struct color_ctx_s *ctx);

void color_ycbcr(struct color_private_s *color,
		 struct color_ctx_s *ctx,
		 unsigned char *from, unsigned char *to);
void color_bgr(struct color_private_s *color,
	       struct color_ctx_s *ctx,
	       unsigned char *from, unsigned char *to);

/* unfortunately over- and underflows will occur */
__inline__ unsigned char color_clamp(int val)
{
	if (val > 255)
		return 255;
	else if (val < 0)
		return 0;
	return val;
}

void *color_init(glc_t *glc, ps_buffer_t *from, ps_buffer_t *to)
{
	struct color_private_s *color = malloc(sizeof(struct color_private_s));
	memset(color, 0, sizeof(struct color_private_s));

	color->glc = glc;

	color->thread.flags = GLC_THREAD_READ | GLC_THREAD_WRITE;
	color->thread.read_callback = &color_read_callback;
	color->thread.write_callback = &color_write_callback;
	color->thread.finish_callback = &color_finish_callback;
	color->thread.ptr = color;
	color->thread.threads = util_cpus();

	if (glc_thread_create(glc, &color->thread, from, to))
		return NULL;

	return color;
}

int color_wait(void *colorpriv)
{
	struct color_private_s *color = colorpriv;

	glc_thread_wait(&color->thread);
	free(color);

	return 0;
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

	if (state->header.type == GLC_MESSAGE_CTX)
		color_ctx_msg(color, (glc_ctx_message_t *) state->read_data);

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

int color_ctx_msg(struct color_private_s *color, glc_ctx_message_t *msg)
{
	struct color_ctx_s *ctx;
	glc_flags_t old_flags;

	color_get_ctx(color, msg->ctx, &ctx);
	pthread_rwlock_wrlock(&ctx->update);

	old_flags = ctx->flags;
	ctx->flags = msg->flags;
	ctx->w = msg->w;
	ctx->h = msg->h;

	if ((ctx->flags & GLC_CTX_BGR) | (ctx->flags & GLC_CTX_BGRA)) {
		if (ctx->flags & GLC_CTX_BGRA)
			ctx->bpp = 4;
		else
			ctx->bpp = 3;

		ctx->row = ctx->bpp * ctx->w;

		if ((ctx->flags & GLC_CTX_DWORD_ALIGNED) && (ctx->row % 8 != 0))
			ctx->row += 8 - ctx->row % 8;
	}

	if (color->glc->flags & GLC_OVERRIDE_COLOR_CORRECTION) {
		ctx->brightness = color->glc->brightness;
		ctx->contrast = color->glc->contrast;
		ctx->red_gamma = color->glc->red_gamma;
		ctx->green_gamma = color->glc->green_gamma;
		ctx->blue_gamma = color->glc->blue_gamma;

		util_log(color->glc, GLC_INFORMATION, "color",
			 "using global color correction for ctx %d", msg->ctx);
		util_log(color->glc, GLC_INFORMATION, "color",
			 "ctx %d: brightness=%f, contrast=%f, red=%f, green=%f, blue=%f",
			 msg->ctx, ctx->brightness, ctx->contrast,
			 ctx->red_gamma, ctx->green_gamma, ctx->blue_gamma);

		if ((ctx->brightness == 0) &&
		    (ctx->contrast == 0) &&
		    (ctx->red_gamma == 1) &&
		    (ctx->green_gamma == 1) &&
		    (ctx->blue_gamma == 1)) {
			util_log(color->glc, GLC_INFORMATION, "color", "skipping color correction");
			ctx->proc = NULL;
		} else if (ctx->flags & GLC_CTX_YCBCR_420JPEG) {
			color_generate_ycbcr_lookup_table(color, ctx);
			ctx->proc = &color_ycbcr;
		} else if ((ctx->flags & GLC_CTX_BGR) | (ctx->flags & GLC_CTX_BGRA)) {
			color_generate_rgb_lookup_table(color, ctx);
			ctx->proc = &color_bgr;
		} else {
			/* set proc NULL -> no conversion done */
			util_log(color->glc, GLC_WARNING, "color", "unsupported ctx %d", msg->ctx);
			ctx->proc = NULL;
		}
	} else if (((old_flags & GLC_CTX_BGR) | (old_flags & GLC_CTX_BGRA)) &&
		   (msg->flags & GLC_CTX_YCBCR_420JPEG)) {
		util_log(color->glc, GLC_WARNING, "color",
			 "colorspace switched from RGB to Y'CbCr, recalculating lookup table");
		color_generate_ycbcr_lookup_table(color, ctx);
		ctx->proc = &color_ycbcr;
	} else if (((msg->flags & GLC_CTX_BGR) | (msg->flags & GLC_CTX_BGRA)) &&
		 (old_flags & GLC_CTX_YCBCR_420JPEG)) {
		util_log(color->glc, GLC_WARNING, "color",
			 "colorspace switched from Y'CbCr to RGB, recalculating lookup table");
		color_generate_rgb_lookup_table(color, ctx);
		ctx->proc = &color_bgr;
	}

	pthread_rwlock_unlock(&ctx->update);
	return 0;
}

int color_color_msg(struct color_private_s *color, glc_color_message_t *msg)
{
	struct color_ctx_s *ctx;

	if (color->glc->flags & GLC_OVERRIDE_COLOR_CORRECTION)
		return 0; /* ignore */

	color_get_ctx(color, msg->ctx, &ctx);
	pthread_rwlock_wrlock(&ctx->update);

	ctx->brightness = msg->brightness;
	ctx->contrast = msg->contrast;
	ctx->red_gamma = msg->red;
	ctx->green_gamma = msg->green;
	ctx->blue_gamma = msg->blue;

	util_log(color->glc, GLC_INFORMATION, "color",
		 "ctx %d: brightness=%f, contrast=%f, red=%f, green=%f, blue=%f",
		 msg->ctx, ctx->brightness, ctx->contrast,
		 ctx->red_gamma, ctx->green_gamma, ctx->blue_gamma);

	if ((ctx->brightness == 0) &&
	    (ctx->contrast == 0) &&
	    (ctx->red_gamma == 1) &&
	    (ctx->green_gamma == 1) &&
	    (ctx->blue_gamma == 1)) {
		util_log(color->glc, GLC_INFORMATION, "color", "skipping color correction");
		ctx->proc = NULL;
	} else if (ctx->flags & GLC_CTX_YCBCR_420JPEG) {
		color_generate_ycbcr_lookup_table(color, ctx);
		ctx->proc = &color_ycbcr;
	} else if ((ctx->flags & GLC_CTX_BGR) | (ctx->flags & GLC_CTX_BGRA)) {
		color_generate_rgb_lookup_table(color, ctx);
		ctx->proc = &color_bgr;
	} else
		ctx->proc = NULL; /* don't attempt anything... */

	pthread_rwlock_unlock(&ctx->update);
	return 0;
}

void color_ycbcr(struct color_private_s *color,
		 struct color_ctx_s *ctx,
		 unsigned char *from, unsigned char *to)
{
	unsigned int x, y, Cpix, Y;
	unsigned int pos;
	unsigned char *Y_from, *Cb_from, *Cr_from;
	unsigned char *Y_to, *Cb_to, *Cr_to;

	Y_from = from;
	Cb_from = &from[ctx->h * ctx->w];
	Cr_from = &from[ctx->h * ctx->w + (ctx->h / 2) * (ctx->w / 2)];

	Y_to = to;
	Cb_to = &to[ctx->h * ctx->w];
	Cr_to = &to[ctx->h * ctx->w + (ctx->h / 2) * (ctx->w / 2)];

	Cpix = 0;

#define CONVERT_Y(xadd, yadd) 								\
	pos = YCBCR_LOOKUP_POS(Y_from[(x + (xadd)) + (y + (yadd)) * ctx->w],		\
			       Cb_from[Cpix], Cr_from[Cpix]);				\
	Y_to[(x + (xadd)) + (y + (yadd)) * ctx->w] = ctx->lookup_table[pos + 0];	\
	Y += ctx->lookup_table[pos + 0];

	for (y = 0; y < ctx->h; y += 2) {
		for (x = 0; x < ctx->w; x += 2) {
			Y = 0;

			CONVERT_Y(0, 0)
			CONVERT_Y(0, 1)
			CONVERT_Y(1, 0)
			CONVERT_Y(1, 1)

			pos = YCBCR_LOOKUP_POS(Y >> 2, Cb_from[Cpix], Cr_from[Cpix]);
			Cb_to[Cpix] = ctx->lookup_table[pos + 1];
			Cr_to[Cpix] = ctx->lookup_table[pos + 2];

			Cpix++;
		}
	}
}

void color_bgr(struct color_private_s *color,
	       struct color_ctx_s *ctx,
	       unsigned char *from, unsigned char *to)
{
	unsigned int x, y, p;

	for (y = 0; y < ctx->h; y++) {
		for (x = 0; x < ctx->w; x++) {
			p = ctx->row * y + x * ctx->bpp;

			to[p + 0] = ctx->lookup_table[256 + 256 + from[p + 0]];
			to[p + 1] = ctx->lookup_table[256       + from[p + 1]];
			to[p + 2] = ctx->lookup_table[            from[p + 2]];
		}
	}
}

#define CLAMP_256(val) \
	(val) < 0 ? 0 : ((val) > 255 ? 255 : (val))

unsigned char YCbCr_TO_RGB_Rd(unsigned char Y, unsigned char Cb, unsigned char Cr)
{
	int R = Y + 1.402 * (Cr - 128);
	return CLAMP_256(R);
}

unsigned char YCbCr_TO_RGB_Gd(unsigned char Y, unsigned char Cb, unsigned char Cr)
{
	int G = Y - 0.344136 * (Cb - 128) - 0.714136 * (Cr - 128);
	return CLAMP_256(G);
}

unsigned char YCbCr_TO_RGB_Bd(unsigned char Y, unsigned char Cb, unsigned char Cr)
{
	int B = Y + 1.772 * (Cb - 128);
	return CLAMP_256(B);
}

#define RGB_TO_YCbCrJPEG_Y(Rd, Gd, Bd) \
	(    + 0.299    * (Rd) +    0.587 * (Gd) + 0.114 *    (Bd))
#define RGB_TO_YCbCrJPEG_Cb(Rd, Gd, Bd) \
	(128 - 0.168736 * (Rd) - 0.331264 * (Gd) + 0.5      * (Bd))
#define RGB_TO_YCbCrJPEG_Cr(Rd, Gd, Bd) \
	(128 + 0.5      * (Rd) - 0.418688 * (Gd) - 0.081312 * (Bd))

int color_generate_ycbcr_lookup_table(struct color_private_s *color,
				      struct color_ctx_s *ctx)
{
	unsigned int Y, Cb, Cr, pos;
	unsigned char R, G, B;
	size_t lookup_size = (1 << LOOKUP_BITS) * (1 << LOOKUP_BITS) * (1 << LOOKUP_BITS) * 3;

	util_log(color->glc, GLC_INFORMATION, "color",
		 "using %d bit lookup table (%zd bytes)", LOOKUP_BITS, lookup_size);
	ctx->lookup_table = malloc(lookup_size);

#define CALC(value, brightness, contrast, gamma) \
	((pow((double) value / 255.0, 1.0 / gamma) - 0.5) * (1.0 + contrast) \
	 + brightness + 0.5) * 255.0

	pos = 0;
	for (Y = 0; Y < 256; Y += (1 << (8 - LOOKUP_BITS))) {
		for (Cb = 0; Cb < 256; Cb += (1 << (8 - LOOKUP_BITS))) {
			for (Cr = 0; Cr < 256; Cr += (1 << (8 - LOOKUP_BITS))) {
				R = color_clamp(CALC(YCbCr_TO_RGB_Rd(Y, Cb, Cr),
						ctx->brightness, ctx->contrast, ctx->red_gamma));
				G = color_clamp(CALC(YCbCr_TO_RGB_Gd(Y, Cb, Cr),
						ctx->brightness, ctx->contrast, ctx->green_gamma));
				B = color_clamp(CALC(YCbCr_TO_RGB_Bd(Y, Cb, Cr),
						ctx->brightness, ctx->contrast, ctx->blue_gamma));

				ctx->lookup_table[pos + 0] = RGB_TO_YCbCrJPEG_Y(R, G, B);
				ctx->lookup_table[pos + 1] = RGB_TO_YCbCrJPEG_Cb(R, G, B);
				ctx->lookup_table[pos + 2] = RGB_TO_YCbCrJPEG_Cr(R, G, B);
				pos += 3;
			}
		}
	}

#undef CALC

	return 0;
}

int color_generate_rgb_lookup_table(struct color_private_s *color,
				    struct color_ctx_s *ctx)
{
	unsigned int c;

	ctx->lookup_table = malloc(256 + 256 + 256);

#define CALC(value, brightness, contrast, gamma) \
	color_clamp( \
		(((pow((double) value / 255.0, 1.0 / gamma) - 0.5) * (1.0 + contrast) + 0.5) \
		 + brightness) * 255.0 \
		)

	for (c = 0; c < 256; c++)
		ctx->lookup_table[c + 0] = CALC(c, ctx->brightness, ctx->contrast, ctx->red_gamma);

	for (c = 0; c < 256; c++)
		ctx->lookup_table[c + 256] = CALC(c, ctx->brightness, ctx->contrast, ctx->green_gamma);

	for (c = 0; c < 256; c++)
		ctx->lookup_table[c + 256 + 256] = CALC(c, ctx->brightness, ctx->contrast, ctx->blue_gamma);

#undef CALC

	return 0;
}

/**  \} */
/**  \} */
