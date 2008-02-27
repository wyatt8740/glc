/**
 * \file src/core/info.c
 * \brief stream information
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup core
 *  \{
 * \defgroup info stream information
 *  \{
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <packetstream.h>
#include <errno.h>

#include <glc/common/glc.h>
#include <glc/common/core.h>
#include <glc/common/log.h>
#include <glc/common/thread.h>
#include <glc/common/util.h>

#include "info.h"

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
	glc_flags_t flags;
	unsigned long pictures;
	size_t bytes;

	unsigned long fps;
	glc_utime_t last_fps_time, fps_time;

	struct info_ctx_s *next;
};

struct info_audio_s {
	glc_audio_i audio_i;
	unsigned long packets;
	size_t bytes;

	struct info_audio_s *next;
};

struct info_s {
	glc_t *glc;
	glc_thread_t thread;
	int running;

	glc_utime_t time;
	int level;
	FILE *stream;
	glc_ctx_i prev_ctx;

	struct info_ctx_s *ctx_list;
	struct info_audio_s *audio_list;
};

int info_get_ctx(info_t info, struct info_ctx_s **ctx, glc_ctx_i ctx_i);
int info_get_audio(info_t info, struct info_audio_s **audio,
		   glc_audio_i audio_i);

void info_finish_callback(void *ptr, int err);
int info_read_callback();

void ctx_info(info_t info, glc_ctx_message_t *ctx_message);
void pic_info(info_t info, glc_picture_header_t *pic_header);
void audio_fmt_info(info_t info, glc_audio_format_message_t *fmt_message);
void audio_info(info_t info, glc_audio_header_t *audio_header);
void color_info(info_t info, glc_color_message_t *color_msg);

void print_time(FILE *stream, glc_utime_t time);
void print_bytes(FILE *stream, size_t bytes);

int info_init(info_t *info, glc_t *glc)
{
	*info = (info_t) malloc(sizeof(struct info_s));
	memset(*info, 0, sizeof(struct info_s));

	(*info)->glc = glc;
	(*info)->ctx_list = NULL;
	(*info)->time = 0;

	(*info)->stream = stdout;
	(*info)->level = 1;

	(*info)->thread.flags = GLC_THREAD_READ;
	(*info)->thread.ptr = *info;
	(*info)->thread.read_callback = &info_read_callback;
	(*info)->thread.finish_callback = &info_finish_callback;
	(*info)->thread.threads = 1;

	return 0;
}

int info_destroy(info_t info)
{
	free(info);
	return 0;
}

int info_set_level(info_t info, int level)
{
	if (level < 1)
		return EINVAL;

	info->level = level;
	return 0;
}

int info_set_stream(info_t info, FILE *stream)
{
	info->stream = stream;
	return 0;
}

int info_process_start(info_t info, ps_buffer_t *from)
{
	int ret;
	if (info->running)
		return EAGAIN;

	if ((ret = glc_thread_create(info->glc, &info->thread, from, NULL)))
		return ret;
	info->running = 1;

	return 0;
}

int info_process_wait(info_t info)
{
	if (!info->running)
		return EAGAIN;

	glc_thread_wait(&info->thread);
	info->running = 0;

	return 0;
}

void info_finish_callback(void *ptr, int err)
{
	info_t info = (info_t) ptr;
	struct info_ctx_s *ctx;
	struct info_audio_s *audio;

	if (err)
		glc_log(info->glc, GLC_ERROR, "info", "%s (%d)",
			 strerror(err), err);

	while (info->ctx_list != NULL) {
		ctx = info->ctx_list;
		info->ctx_list = info->ctx_list->next;

		fprintf(info->stream, "video stream %d\n", ctx->ctx_i);
		fprintf(info->stream, "  frames      = %lu\n", ctx->pictures);
		fprintf(info->stream, "  fps         = %04.2f\n",
		       (double) (ctx->pictures * 1000000) / (double) (info->time));
		fprintf(info->stream, "  bytes       = ");
		print_bytes(info->stream, ctx->bytes);
		fprintf(info->stream, "  bps         = ");
		print_bytes(info->stream, (ctx->bytes * 1000000) / info->time);

		free(ctx);
	}

	while (info->audio_list != NULL) {
		audio = info->audio_list;
		info->audio_list = info->audio_list->next;

		fprintf(info->stream, "audio stream %d\n", audio->audio_i);
		fprintf(info->stream, "  packets     = %lu\n", audio->packets);
		fprintf(info->stream, "  pps         = %04.2f\n",
		       (double) (audio->packets * 1000000) / (double) (info->time));
		fprintf(info->stream, "  bytes       = ");
		print_bytes(info->stream, audio->bytes);
		fprintf(info->stream, "  bps         = ");
		print_bytes(info->stream, (audio->bytes * 1000000) / info->time);

		free(audio);
	}
}

int info_read_callback(glc_thread_state_t *state)
{
	info_t info = (info_t) state->ptr;

	if (state->header.type == GLC_MESSAGE_CTX)
		ctx_info(info, (glc_ctx_message_t *) state->read_data);
	else if (state->header.type == GLC_MESSAGE_PICTURE)
		pic_info(info, (glc_picture_header_t *) state->read_data);
	else if (state->header.type == GLC_MESSAGE_AUDIO_FORMAT)
		audio_fmt_info(info, (glc_audio_format_message_t *) state->read_data);
	else if (state->header.type == GLC_MESSAGE_AUDIO)
		audio_info(info, (glc_audio_header_t *) state->read_data);
	else if (state->header.type == GLC_MESSAGE_COLOR)
		color_info(info, (glc_color_message_t *) state->read_data);
	else if (state->header.type == GLC_MESSAGE_CLOSE) {
		print_time(info->stream, info->time);
		fprintf(info->stream, "end of stream\n");
	} else {
		print_time(info->stream, info->time);
		fprintf(info->stream, "error: unknown %zd B message with type 0x%02x\n", state->read_size, state->header.type);
	}

	return 0;
}

int info_get_ctx(info_t info, struct info_ctx_s **ctx, glc_ctx_i ctx_i)
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

int info_get_audio(info_t info, struct info_audio_s **audio,
		   glc_audio_i audio_i)
{
	*audio = info->audio_list;

	while (*audio != NULL) {
		if ((*audio)->audio_i == audio_i)
			break;
		*audio = (*audio)->next;
	}

	if (*audio == NULL) {
		*audio = malloc(sizeof(struct info_audio_s));
		memset(*audio, 0, sizeof(struct info_audio_s));

		(*audio)->next = info->audio_list;
		info->audio_list = *audio;
		(*audio)->audio_i = audio_i;
	}

	return 0;
}

#define INFO_FLAGS \
	const char *__info_flag_op = "";
#define INFO_FLAG(var, flag) \
	if ((var) & flag) { \
		fprintf(info->stream, "%s", __info_flag_op); \
		fprintf(info->stream, #flag); \
		__info_flag_op = " | "; \
	}

void ctx_info(info_t info, glc_ctx_message_t *ctx_message)
{
	INFO_FLAGS
	struct info_ctx_s *ctx;
	info_get_ctx(info, &ctx, ctx_message->ctx);

	ctx->w = ctx_message->w;
	ctx->h = ctx_message->h;
	ctx->flags = ctx_message->flags;

	if ((ctx_message->flags & GLC_CTX_UPDATE) && (!(ctx->created))) {
		print_time(info->stream, info->time);
		fprintf(info->stream, "error: GLC_CTX_UPDATE to uninitialized ctx %d\n",
			ctx_message->ctx);
	}

	if ((ctx_message->flags & GLC_CTX_CREATE) && (ctx->created)) {
		print_time(info->stream, info->time);
		fprintf(info->stream, "error: GLC_CTX_CREATE to initalized ctx %d\n",
			ctx_message->ctx);
	}

	if (ctx_message->flags & GLC_CTX_CREATE)
		ctx->created = 1;

	print_time(info->stream, info->time);
	if (info->level >= INFO_DETAILED_CTX) {
		fprintf(info->stream, "video stream format message\n");

		fprintf(info->stream, "  ctx         = %d\n", ctx_message->ctx);
		fprintf(info->stream, "  flags       = ");
		INFO_FLAG(ctx_message->flags, GLC_CTX_CREATE)
		INFO_FLAG(ctx_message->flags, GLC_CTX_UPDATE)
		INFO_FLAG(ctx_message->flags, GLC_CTX_BGR)
		INFO_FLAG(ctx_message->flags, GLC_CTX_BGRA)
		INFO_FLAG(ctx_message->flags, GLC_CTX_YCBCR_420JPEG)
		INFO_FLAG(ctx_message->flags, GLC_CTX_DWORD_ALIGNED)
		fprintf(info->stream, "\n");
		fprintf(info->stream, "  width       = %u\n", ctx_message->w);
		fprintf(info->stream, "  height      = %u\n", ctx_message->h);
	} else
		fprintf(info->stream, "video stream %d\n", ctx_message->ctx);
}

void pic_info(info_t info, glc_picture_header_t *pic_header)
{
	struct info_ctx_s *ctx;
	info->time = pic_header->timestamp;

	info_get_ctx(info, &ctx, pic_header->ctx);

	if (!(ctx->created)) {
		print_time(info->stream, info->time);
		fprintf(info->stream, "error: picture to uninitialized ctx %d\n", ctx->ctx_i);
	}

	if (info->level >= INFO_DETAILED_PICTURE) {
		print_time(info->stream, info->time);
		fprintf(info->stream, "picture\n");

		fprintf(info->stream, "  timestamp   = %lu\n", pic_header->timestamp);
		fprintf(info->stream, "  ctx         = %d\n", pic_header->ctx);
		fprintf(info->stream, "  size        = %ux%u\n", ctx->w, ctx->h);
	} else if (info->level >= INFO_PICTURE) {
		print_time(info->stream, info->time);
		fprintf(info->stream, "picture (ctx %d)\n", pic_header->ctx);
	}

	if ((info->level >= INFO_DETAILED_PICTURE) && (info->prev_ctx != pic_header->ctx)) {
		print_time(info->stream, info->time);
		fprintf(info->stream, "ctx switch from %d to %d\n", info->prev_ctx, pic_header->ctx);
	}

	ctx->pictures++;
	ctx->fps++;

	if (ctx->flags & GLC_CTX_BGR) {
		ctx->bytes += ctx->w * ctx->h * 3;
		if (ctx->flags & GLC_CTX_DWORD_ALIGNED)
			ctx->bytes += ctx->h * (8 - (ctx->w * 3) % 8);
	} else if (ctx->flags & GLC_CTX_BGRA) {
		ctx->bytes += ctx->w * ctx->h * 4;
		if (ctx->flags & GLC_CTX_DWORD_ALIGNED)
			ctx->bytes += ctx->h * (8 - (ctx->w * 4) % 8);
	} else if (ctx->flags & GLC_CTX_YCBCR_420JPEG)
		ctx->bytes += (ctx->w * ctx->h * 3) / 2;

	if ((info->level >= INFO_FPS) && (pic_header->timestamp - ctx->fps_time >= 1000000)) {
		print_time(info->stream, info->time);
		fprintf(info->stream, "ctx %d: %04.2f fps\n", ctx->ctx_i,
			(double) (ctx->fps * 1000000) / (double) (pic_header->timestamp - ctx->last_fps_time));
		ctx->last_fps_time = pic_header->timestamp;
		ctx->fps_time += 1000000;
		ctx->fps = 0;
	}

	info->prev_ctx = pic_header->ctx;
}

void audio_fmt_info(info_t info, glc_audio_format_message_t *fmt_message)
{
	INFO_FLAGS
	print_time(info->stream, info->time);
	if (info->level >= INFO_DETAILED_AUDIO_FORMAT) {
		fprintf(info->stream, "audio stream format message\n");

		fprintf(info->stream, "  stream      = %d\n", fmt_message->audio);
		fprintf(info->stream, "  flags       = ");
		INFO_FLAG(fmt_message->flags, GLC_AUDIO_INTERLEAVED)
		INFO_FLAG(fmt_message->flags, GLC_AUDIO_FORMAT_UNKNOWN)
		INFO_FLAG(fmt_message->flags, GLC_AUDIO_S16_LE)
		INFO_FLAG(fmt_message->flags, GLC_AUDIO_S24_LE)
		INFO_FLAG(fmt_message->flags, GLC_AUDIO_S32_LE)
		fprintf(info->stream, "\n");
		fprintf(info->stream, "  rate        = %d\n", fmt_message->rate);
		fprintf(info->stream, "  channels    = %d\n", fmt_message->channels);
	} else
		fprintf(info->stream, "audio stream %d\n", fmt_message->audio);
}

void audio_info(info_t info, glc_audio_header_t *audio_header)
{
	info->time = audio_header->timestamp;
	struct info_audio_s *audio;

	info_get_audio(info, &audio, audio_header->audio);
	audio->packets++;
	audio->bytes += audio_header->size;

	if (info->level >= INFO_AUDIO_DETAILED) {
		print_time(info->stream, info->time);
		fprintf(info->stream, "audio packet\n");
		fprintf(info->stream, "  stream      = %d\n", audio_header->audio);
		fprintf(info->stream, "  timestamp   = %lu\n", audio_header->timestamp);
		fprintf(info->stream, "  size        = %ld\n", audio_header->size);
	} else if (info->level >= INFO_AUDIO) {
		print_time(info->stream, info->time);
		fprintf(info->stream, "audio packet (stream %d)\n", audio_header->audio);
	}
}

void color_info(info_t info, glc_color_message_t *color_msg)
{
	print_time(info->stream, info->time);
	if (info->level >= INFO_DETAILED_CTX) {
		fprintf(info->stream, "color correction message\n");
		fprintf(info->stream, "  ctx         = %d\n", color_msg->ctx);
		fprintf(info->stream, "  brightness  = %f\n", color_msg->brightness);
		fprintf(info->stream, "  contrast    = %f\n", color_msg->contrast);
		fprintf(info->stream, "  red gamma   = %f\n", color_msg->red);
		fprintf(info->stream, "  green gamma = %f\n", color_msg->green);
		fprintf(info->stream, "  blue gamma  = %f\n", color_msg->blue);
	} else
		fprintf(info->stream, "color correction information for ctx %d\n", color_msg->ctx);
}

/*
void stream_info(info_t info)
{
	if (info->stream_info)
		return;

	if (info->glc->info) {
		fprintf(info->stream, "glc stream info\n");
		fprintf(info->stream, "  signature   = 0x%08x\n", info->glc->info->signature);
		fprintf(info->stream, "  version     = 0x%02x\n", info->glc->info->version);
		fprintf(info->stream, "  flags       = %d\n", info->glc->info->flags);
		fprintf(info->stream, "  fps         = %f\n", info->glc->info->fps);
		fprintf(info->stream, "  pid         = %d\n", info->glc->info->pid);
		fprintf(info->stream, "  name        = %s\n", info->glc->info_name);
		fprintf(info->stream, "  date        = %s\n", info->glc->info_date);
	} else
		fprintf(info->stream, "no glc stream info available\n");

	info->stream_info = 1;
}
*/

void print_time(FILE *stream, glc_utime_t time)
{
	fprintf(stream, "[%7.2fs] ", (double) time / 1000000.0);
}

void print_bytes(FILE *stream, size_t bytes)
{
	if (bytes >= 1024 * 1024 * 1024)
		fprintf(stream, "%.2f GiB\n", (float) bytes / (float) (1024 * 1024 * 1024));
	else if (bytes >= 1024 * 1024)
		fprintf(stream, "%.2f MiB\n", (float) bytes / (float) (1024 * 1024));
	else if (bytes >= 1024)
		fprintf(stream, "%.2f KiB\n", (float) bytes / 1024.0f);
	else
		fprintf(stream, "%d B\n", (int) bytes);
}

/**  \} */
/**  \} */
