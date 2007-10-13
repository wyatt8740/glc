/**
 * \file src/stream/info.c
 * \brief stream information
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* info.c -- stream information
 * Copyright (C) 2007 Pyry Haulos
 * For conditions of distribution and use, see copyright notice in glc.h
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <packetstream.h>

#include "../common/glc.h"
#include "../common/thread.h"
#include "info.h"

/**
 * \addtogroup stream
 *  \{
 */

/**
 * \defgroup info stream information
 *  \{
 */

#define INFO_DETAILED_CTX           2
#define INFO_DETAILED_AUDIO_FORMAT  2
#define INFO_FPS                    3
#define INFO_AUDIO                  4
#define INFO_AUDIO_DETAILED         5
#define INFO_PICTURE                5
#define INFO_DETAILED_PICTURE       6

struct info_ctx_s {
	int created;
	unsigned int w, h;
	glc_ctx_i ctx_i;
	unsigned long pictures;
	
	unsigned long fps;
	glc_utime_t last_fps_time;
	
	struct info_ctx_s *next;
};

struct info_private_s {
	glc_t *glc;
	glc_thread_t thread;
	glc_utime_t time;
	int level;
	int stream_info;
	glc_ctx_i prev_ctx;
	struct info_ctx_s *ctx_list;
};

void info_finish_callback(void *ptr, int err);
int info_read_callback();

void ctx_info(struct info_private_s *info, glc_ctx_message_t *ctx_message);
void pic_info(struct info_private_s *info, glc_picture_header_t *pic_header);
void audio_fmt_info(struct info_private_s *info, glc_audio_format_message_t *fmt_message);
void audio_info(struct info_private_s *info, glc_audio_header_t *audio_header);
void stream_info(struct info_private_s *info);

void print_time(FILE *stream, glc_utime_t time);

int info_init(glc_t *glc, ps_buffer_t *from)
{
	struct info_private_s *info = malloc(sizeof(struct info_private_s));
	memset(info, 0, sizeof(struct info_private_s));
	
	info->glc = glc;
	info->ctx_list = NULL;
	info->time = 0;
	
	info->thread.flags = GLC_THREAD_READ;
	info->thread.ptr = info;
	info->thread.read_callback = &info_read_callback;
	info->thread.finish_callback = &info_finish_callback;
	info->thread.threads = 1;
	
	return glc_thread_create(glc, &info->thread, from, NULL);
}

void info_finish_callback(void *ptr, int err)
{
	struct info_private_s *info = (struct info_private_s *) ptr;
	
	if (err)
		fprintf(stderr, "info failed: %s (%d)", strerror(err), err);
	
	struct info_ctx_s *del;
	while (info->ctx_list != NULL) {
		del = info->ctx_list;
		info->ctx_list = info->ctx_list->next;
		
		free(del);
	}
	
	sem_post(&info->glc->signal[GLC_SIGNAL_INFO_FINISHED]);
	free(info);
}

int info_read_callback(glc_thread_state_t *state)
{
	struct info_private_s *info = (struct info_private_s *) state->ptr;
	
	if (!info->stream_info)
		stream_info(info);
	
	if (state->header.type == GLC_MESSAGE_CTX)
		ctx_info(info, (glc_ctx_message_t *) state->read_data);
	else if (state->header.type == GLC_MESSAGE_PICTURE)
		pic_info(info, (glc_picture_header_t *) state->read_data);
	else if (state->header.type == GLC_MESSAGE_AUDIO_FORMAT)
		audio_fmt_info(info, (glc_audio_format_message_t *) state->read_data);
	else if (state->header.type == GLC_MESSAGE_AUDIO)
		audio_info(info, (glc_audio_header_t *) state->read_data);
	else if (state->header.type == GLC_MESSAGE_CLOSE) {
		print_time(stdout, info->time);
		printf("end of stream\n");
	} else {
		print_time(stdout, info->time);
		printf("error: unknown %zd B message with type 0x%02x\n", state->read_size, state->header.type);
	}
	
	return 0;
}

int info_get_ctx(struct info_private_s *info, struct info_ctx_s **ctx, glc_ctx_i ctx_i)
{
	struct info_ctx_s *fctx = info->ctx_list;
	
	while (fctx != NULL) {
		if (fctx->ctx_i == ctx_i)
			break;
		fctx = fctx->next;
	}
	
	if (fctx == NULL) {
		fctx = (struct info_ctx_s *) malloc(sizeof(struct info_ctx_s));
		memset(fctx, 0, sizeof(struct info_ctx_s));
		
		fctx->next = info->ctx_list;
		info->ctx_list = fctx;
		fctx->ctx_i = ctx_i;
	}
	
	*ctx = fctx;
	
	return 0;
}

#define INFO_FLAGS \
	const char *__info_flag_op = "";
#define INFO_FLAG(var, flag) \
	if ((var) & flag) { \
		printf("%s", __info_flag_op); \
		printf(#flag); \
		__info_flag_op = " | "; \
	}

void ctx_info(struct info_private_s *info, glc_ctx_message_t *ctx_message)
{
	INFO_FLAGS
	struct info_ctx_s *ctx;
	info_get_ctx(info, &ctx, ctx_message->ctx);
	
	ctx->w = ctx_message->w;
	ctx->h = ctx_message->h;
	
	if ((ctx_message->flags & GLC_CTX_UPDATE) && (!(ctx->created))) {
		print_time(stdout, info->time);
		printf("error: GLC_CTX_UPDATE to uninitialized ctx %d\n", ctx_message->ctx);
	}
	
	if ((ctx_message->flags & GLC_CTX_CREATE) && (ctx->created)) {
		print_time(stdout, info->time);
		printf("error: GLC_CTX_CREATE to initalized ctx %d\n", ctx_message->ctx);
	}
	
	if (ctx_message->flags & GLC_CTX_CREATE)
		ctx->created = 1;
	
	print_time(stdout, info->time);
	if (info->glc->info_level >= INFO_DETAILED_CTX) {
		printf("ctx message\n");
		
		printf("  ctx         = %d\n", ctx_message->ctx);
		printf("  flags       = ");
		INFO_FLAG(ctx_message->flags, GLC_CTX_CREATE)
		INFO_FLAG(ctx_message->flags, GLC_CTX_UPDATE)
		INFO_FLAG(ctx_message->flags, GLC_CTX_BGR)
		INFO_FLAG(ctx_message->flags, GLC_CTX_BGRA)
		INFO_FLAG(ctx_message->flags, GLC_CTX_YCBCR_420JPEG)
		printf("\n");
		printf("  width       = %u\n", ctx_message->w);
		printf("  height      = %u\n", ctx_message->h);
	} else
		printf("ctx %d\n", ctx_message->ctx);
}

void pic_info(struct info_private_s *info, glc_picture_header_t *pic_header)
{
	struct info_ctx_s *ctx;
	info->time = pic_header->timestamp;

	info_get_ctx(info, &ctx, pic_header->ctx);

	if (!(ctx->created)) {
		print_time(stdout, info->time);
		printf("error: picture to uninitialized ctx %d\n", ctx->ctx_i);
	}

	if (info->glc->info_level >= INFO_DETAILED_PICTURE) {
		print_time(stdout, info->time);
		printf("picture\n");
		
		printf("  timestamp   = %lu\n", pic_header->timestamp);
		printf("  ctx         = %d\n", pic_header->ctx);
		printf("  size        = %ux%u\n", ctx->w, ctx->h);
	} else if (info->glc->info_level >= INFO_PICTURE) {
		print_time(stdout, info->time);
		printf("picture\n");
	}

	if ((info->glc->info_level >= INFO_DETAILED_PICTURE) && (info->prev_ctx != pic_header->ctx)) {
		print_time(stdout, info->time);
		printf("ctx switch from %d to %d\n", info->prev_ctx, pic_header->ctx);
	}

	ctx->pictures++;
	ctx->fps++;

	if ((info->glc->info_level >= INFO_FPS) && (pic_header->timestamp - ctx->last_fps_time >= 1000000)) {
		print_time(stdout, info->time);
		printf("ctx %d: %04.2f fps\n", ctx->ctx_i, (double) (ctx->fps * 1000000) / (double) (pic_header->timestamp - ctx->last_fps_time));
		ctx->last_fps_time += 1000000;
		ctx->fps = 0;
	}

	info->prev_ctx = pic_header->ctx;
}

void audio_fmt_info(struct info_private_s *info, glc_audio_format_message_t *fmt_message)
{
	INFO_FLAGS
	print_time(stdout, info->time);
	if (info->glc->info_level >= INFO_DETAILED_AUDIO_FORMAT) {
		printf("audio format message\n");

		printf("  stream      = %d\n", fmt_message->audio);
		printf("  flags       = ");
		INFO_FLAG(fmt_message->flags, GLC_AUDIO_INTERLEAVED)
		INFO_FLAG(fmt_message->flags, GLC_AUDIO_FORMAT_UNKNOWN)
		INFO_FLAG(fmt_message->flags, GLC_AUDIO_S16_LE)
		INFO_FLAG(fmt_message->flags, GLC_AUDIO_S24_LE)
		INFO_FLAG(fmt_message->flags, GLC_AUDIO_S32_LE)
		printf("\n");
		printf("  rate        = %d\n", fmt_message->rate);
		printf("  channels    = %d\n", fmt_message->channels);
	} else
		printf("audio format message\n");
}

void audio_info(struct info_private_s *info, glc_audio_header_t *audio_header)
{
	info->time = audio_header->timestamp;
	
	if (info->glc->info_level >= INFO_AUDIO_DETAILED) {
		print_time(stdout, info->time);
		printf("audio packet\n");
		printf("  stream      = %d\n", audio_header->audio);
		printf("  timestamp   = %lu\n", audio_header->timestamp);
		printf("  size        = %ld\n", audio_header->size);
	} else if (info->glc->info_level >= INFO_AUDIO) {
		print_time(stdout, info->time);
		printf("audio packet\n");
	}
}

void stream_info(struct info_private_s *info)
{
	if (info->stream_info)
		return;
	
	/* show stream info header */
	if (info->glc->info) {
		printf("GLC stream info\n");
		printf("  signature   = 0x%08x\n", info->glc->info->signature);
		printf("  version     = 0x%02x\n", info->glc->info->version);
		printf("  flags       = %d\n", info->glc->info->flags);
		printf("  fps         = %f\n", info->glc->info->fps);
		printf("  pid         = %d\n", info->glc->info->pid);
		printf("  name        = %s\n", info->glc->info_name);
		printf("  date        = %s\n", info->glc->info_date);
	} else
		printf("no GLC stream info available\n");
	
	info->stream_info = 1;
}

void print_time(FILE *stream, glc_utime_t time)
{
	fprintf(stream, "[%7.2fs] ", (double) time / 1000000.0);
}


/**  \} */
/**  \} */
