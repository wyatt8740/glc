/**
 * \file glc/core/rgb.c
 * \brief convert Y'CbCr to BGR
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup rgb
 *  \{
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <packetstream.h>
#include <errno.h>

#include <glc/common/glc.h>
#include <glc/common/core.h>
#include <glc/common/log.h>
#include <glc/common/thread.h>
#include <glc/common/util.h>

#include "rgb.h"

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

/** \note unfortunately overflows will occur */
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

struct rgb_video_stream_s {
	glc_stream_id_t id;
	unsigned int w, h;
	int convert;
	size_t size;
	
	pthread_rwlock_t update;
	struct rgb_video_stream_s *next;
};

struct rgb_s {
	glc_t *glc;
	glc_thread_t thread;
	int running;

	unsigned char *lookup_table;

	struct rgb_video_stream_s *ctx;
};

int rgb_read_callback(glc_thread_state_t *state);
int rgb_write_callback(glc_thread_state_t *state);
void rgb_finish_callback(void *ptr, int err);

void rgbget_video_stream(rgb_t rgb, glc_stream_id_t id,
		struct rgb_video_stream_s **ctx);

int rgb_video_format_message(rgb_t rgb, glc_video_format_message_t *video_format_message);
int rgb_convert(rgb_t rgb, struct rgb_video_stream_s *ctx,
		unsigned char *from, unsigned char *to);

int rgb_init_lookup(rgb_t rgb);
int rgb_convert_lookup(rgb_t rgb, struct rgb_video_stream_s *ctx,
		       unsigned char *from, unsigned char *to);

int rgb_init(rgb_t *rgb, glc_t *glc)
{
	*rgb = (rgb_t) malloc(sizeof(struct rgb_s));
	memset(*rgb, 0, sizeof(struct rgb_s));

	(*rgb)->glc = glc;

	rgb_init_lookup(*rgb);

	(*rgb)->thread.flags = GLC_THREAD_READ | GLC_THREAD_WRITE;
	(*rgb)->thread.read_callback = &rgb_read_callback;
	(*rgb)->thread.write_callback = &rgb_write_callback;
	(*rgb)->thread.finish_callback = &rgb_finish_callback;
	(*rgb)->thread.ptr = *rgb;
	(*rgb)->thread.threads = glc_threads_hint(glc);

	return 0;
}

int rgb_destroy(rgb_t rgb)
{
	if (rgb->lookup_table)
		free(rgb->lookup_table);
	free(rgb);
	return 0;
}

int rgb_process_start(rgb_t rgb, ps_buffer_t *from, ps_buffer_t *to)
{
	int ret;
	if (rgb->running)
		return EAGAIN;

	if ((ret = glc_thread_create(rgb->glc, &rgb->thread, from, to)))
		return ret;
	rgb->running = 1;

	return 0;
}

int rgb_process_wait(rgb_t rgb)
{
	if (!rgb->running)
		return EAGAIN;

	glc_thread_wait(&rgb->thread);
	rgb->running = 0;

	return 0;
}

void rgb_finish_callback(void *ptr, int err)
{
	rgb_t rgb = (rgb_t) ptr;
	struct rgb_video_stream_s *del;

	if (err)
		glc_log(rgb->glc, GLC_ERROR, "rgb", "%s (%d)", strerror(err), err);

	while (rgb->ctx != NULL) {
		del = rgb->ctx;
		rgb->ctx = rgb->ctx->next;

		pthread_rwlock_destroy(&del->update);
		free(del);
	}
}

int rgb_read_callback(glc_thread_state_t *state)
{
	rgb_t rgb = (rgb_t) state->ptr;
	struct rgb_video_stream_s *ctx;
	glc_video_data_header_t *pic_hdr;

	if (state->header.type == GLC_MESSAGE_VIDEO_FORMAT)
		rgb_video_format_message(rgb, (glc_video_format_message_t *) state->read_data);

	if (state->header.type == GLC_MESSAGE_VIDEO_DATA) {
		pic_hdr = (glc_video_data_header_t *) state->read_data;
		rgbget_video_stream(rgb, pic_hdr->id, &ctx);
		state->threadptr = ctx;

		pthread_rwlock_rdlock(&ctx->update);

		if (ctx->convert)
			state->write_size = sizeof(glc_video_data_header_t) + ctx->size;
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
	rgb_t rgb = (rgb_t) state->ptr;
	struct rgb_video_stream_s *ctx = state->threadptr;

	memcpy(state->write_data, state->read_data, sizeof(glc_video_data_header_t));
	rgb_convert_lookup(rgb, ctx,
		    (unsigned char *) &state->read_data[sizeof(glc_video_data_header_t)],
		    (unsigned char *) &state->write_data[sizeof(glc_video_data_header_t)]);
	pthread_rwlock_unlock(&ctx->update);

	return 0;
}

void rgbget_video_stream(rgb_t rgb, glc_stream_id_t id,
		struct rgb_video_stream_s **ctx)
{
	*ctx = rgb->ctx;

	while (*ctx != NULL) {
		if ((*ctx)->id == id)
			break;
		*ctx = (*ctx)->next;
	}

	if (*ctx == NULL) {
		*ctx = malloc(sizeof(struct rgb_video_stream_s));
		memset(*ctx, 0, sizeof(struct rgb_video_stream_s));
		
		(*ctx)->next = rgb->ctx;
		rgb->ctx = *ctx;
		(*ctx)->id = id;
		pthread_rwlock_init(&(*ctx)->update, NULL);
	}
}

int rgb_video_format_message(rgb_t rgb, glc_video_format_message_t *video_format_message)
{
	struct rgb_video_stream_s *video;
	rgbget_video_stream(rgb, video_format_message->id, &video);

	if (video_format_message->format != GLC_VIDEO_YCBCR_420JPEG)
		return 0; /* just don't convert */
	
	pthread_rwlock_wrlock(&video->update);

	video->w = video_format_message->width;
	video->h = video_format_message->height;
	video->size = video->w * video->h * 3; /* convert to BGR */
	video->convert = 1;

	video_format_message->format = GLC_VIDEO_BGR;

	pthread_rwlock_unlock(&video->update);

	return 0;
}

int rgb_convert(rgb_t rgb, struct rgb_video_stream_s *video,
		unsigned char *from, unsigned char *to)
{
	unsigned int x, y, Cpix;
	unsigned char *Y, *Cb, *Cr;

	Y = from;
	Cb = &from[video->h * video->w];
	Cr = &from[video->h * video->w + (video->h / 2) * (video->w / 2)];
	Cpix = 0;

#define CONVERT(xadd, yrgbadd, yadd) 								  \
	to[((x + (xadd)) + ((video->h - y) + (yrgbadd)) * video->w) * 3 + 2] = 			  \
		YCbCrJPEG_TO_RGB_Rd(Y[(x + (xadd)) + (y + (yadd)) * video->w], Cb[Cpix], Cr[Cpix]); \
	to[((x + (xadd)) + ((video->h - y) + (yrgbadd)) * video->w) * 3 + 1] = 			  \
		YCbCrJPEG_TO_RGB_Gd(Y[(x + (xadd)) + (y + (yadd)) * video->w], Cb[Cpix], Cr[Cpix]); \
	to[((x + (xadd)) + ((video->h - y) + (yrgbadd)) * video->w) * 3 + 0] = 			  \
		YCbCrJPEG_TO_RGB_Bd(Y[(x + (xadd)) + (y + (yadd)) * video->w], Cb[Cpix], Cr[Cpix]);

	/* YCBCR_420JPEG frame dimensions are always divisible by two */
	for (y = 0; y < video->h; y += 2) {
		for (x = 0; x < video->w; x += 2) {
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

int rgb_init_lookup(rgb_t rgb)
{
	unsigned int Y, Cb, Cr, color;
	size_t lookup_size = (1 << LOOKUP_BITS) * (1 << LOOKUP_BITS) * (1 << LOOKUP_BITS) * 3;

	glc_log(rgb->glc, GLC_INFORMATION, "rgb",
		 "using %d bit lookup table (%zd bytes)", LOOKUP_BITS, lookup_size);
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

int rgb_convert_lookup(rgb_t rgb, struct rgb_video_stream_s *video,
		       unsigned char *from, unsigned char *to)
{
	unsigned int x, y, Cpix;
	unsigned int color;
	unsigned char *Y, *Cb, *Cr;

	Y = from;
	Cb = &from[video->h * video->w];
	Cr = &from[video->h * video->w + (video->h / 2) * (video->w / 2)];
	Cpix = 0;

#define CONVERT(xadd, yrgbadd, yadd) 						\
	color = LOOKUP_POS(Y[(x + (xadd)) + (y + (yadd)) * video->w],		\
			   Cb[Cpix], Cr[Cpix]);					\
	to[((x + (xadd)) + ((video->h - y) + (yrgbadd)) * video->w) * 3 + 2] =	\
		rgb->lookup_table[color + 0];					\
	to[((x + (xadd)) + ((video->h - y) + (yrgbadd)) * video->w) * 3 + 1] =	\
		rgb->lookup_table[color + 1];					\
	to[((x + (xadd)) + ((video->h - y) + (yrgbadd)) * video->w) * 3 + 0] =	\
		rgb->lookup_table[color + 2];

	/* YCBCR_420JPEG frame dimensions are always divisible by two */
	for (y = 0; y < video->h; y += 2) {
		for (x = 0; x < video->w; x += 2) {
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
