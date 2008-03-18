/**
 * \file glc/core/info.c
 * \brief stream information
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup info
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

#define INFO_DETAILED_VIDEO           2
#define INFO_DETAILED_AUDIO_FORMAT  2
#define INFO_FPS                    3
#define INFO_AUDIO                  4
#define INFO_AUDIO_DETAILED         5
#define INFO_PICTURE                5
#define INFO_DETAILED_PICTURE       6

struct info_video_stream_s {
	glc_stream_id_t id;
	glc_flags_t flags;
	glc_video_format_t format;
	unsigned int w, h;

	unsigned long pictures;
	size_t bytes;

	unsigned long fps;
	glc_utime_t last_fps_time, fps_time;

	struct info_video_stream_s *next;
};

struct info_audio_stream_s {
	glc_stream_id_t id;
	unsigned long packets;
	size_t bytes;

	struct info_audio_stream_s *next;
};

struct info_s {
	glc_t *glc;
	glc_thread_t thread;
	int running;

	glc_utime_t time;
	int level;
	FILE *stream;

	struct info_video_stream_s *video_list;
	struct info_audio_stream_s *audio_list;
};

int info_get_video_stream(info_t info, struct info_video_stream_s **video,
			  glc_stream_id_t id);
int info_get_audio_stream(info_t info, struct info_audio_stream_s **audio,
			  glc_stream_id_t id);

void info_finish_callback(void *ptr, int err);
int info_read_callback();

void video_format_info(info_t info, glc_video_format_message_t *video_message);
void video_data_info(info_t info, glc_video_data_header_t *pic_header);
void audio_format_info(info_t info, glc_audio_format_message_t *fmt_message);
void audio_data_info(info_t info, glc_audio_data_header_t *audio_header);
void color_info(info_t info, glc_color_message_t *color_msg);

void print_time(FILE *stream, glc_utime_t time);
void print_bytes(FILE *stream, size_t bytes);

int info_init(info_t *info, glc_t *glc)
{
	*info = (info_t) malloc(sizeof(struct info_s));
	memset(*info, 0, sizeof(struct info_s));

	(*info)->glc = glc;
	(*info)->video_list = NULL;
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
	struct info_video_stream_s *video;
	struct info_audio_stream_s *audio;

	if (err)
		glc_log(info->glc, GLC_ERROR, "info", "%s (%d)",
			 strerror(err), err);

	while (info->video_list != NULL) {
		video = info->video_list;
		info->video_list = info->video_list->next;

		fprintf(info->stream, "video stream %d\n", video->id);
		fprintf(info->stream, "  frames      = %lu\n", video->pictures);
		fprintf(info->stream, "  fps         = %04.2f\n",
		       (double) (video->pictures * 1000000) / (double) (info->time));
		fprintf(info->stream, "  bytes       = ");
		print_bytes(info->stream, video->bytes);
		fprintf(info->stream, "  bps         = ");
		print_bytes(info->stream, (video->bytes * 1000000) / info->time);

		free(video);
	}

	while (info->audio_list != NULL) {
		audio = info->audio_list;
		info->audio_list = info->audio_list->next;

		fprintf(info->stream, "audio stream %d\n", audio->id);
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

	if (state->header.type == GLC_MESSAGE_VIDEO_FORMAT)
		video_format_info(info, (glc_video_format_message_t *) state->read_data);
	else if (state->header.type == GLC_MESSAGE_VIDEO_DATA)
		video_data_info(info, (glc_video_data_header_t *) state->read_data);
	else if (state->header.type == GLC_MESSAGE_AUDIO_FORMAT)
		audio_format_info(info, (glc_audio_format_message_t *) state->read_data);
	else if (state->header.type == GLC_MESSAGE_AUDIO_DATA)
		audio_data_info(info, (glc_audio_data_header_t *) state->read_data);
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

int info_get_video_stream(info_t info, struct info_video_stream_s **video, glc_stream_id_t id)
{
	struct info_video_stream_s *fvideo = info->video_list;

	while (fvideo != NULL) {
		if (fvideo->id == id)
			break;
		fvideo = fvideo->next;
	}

	if (fvideo == NULL) {
		fvideo = (struct info_video_stream_s *) malloc(sizeof(struct info_video_stream_s));
		memset(fvideo, 0, sizeof(struct info_video_stream_s));

		fvideo->next = info->video_list;
		info->video_list = fvideo;
		fvideo->id = id;
	}

	*video = fvideo;

	return 0;
}

int info_get_audio_stream(info_t info, struct info_audio_stream_s **audio,
		   glc_stream_id_t id)
{
	*audio = info->audio_list;

	while (*audio != NULL) {
		if ((*audio)->id == id)
			break;
		*audio = (*audio)->next;
	}

	if (*audio == NULL) {
		*audio = malloc(sizeof(struct info_audio_stream_s));
		memset(*audio, 0, sizeof(struct info_audio_stream_s));

		(*audio)->next = info->audio_list;
		info->audio_list = *audio;
		(*audio)->id = id;
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

void video_format_info(info_t info, glc_video_format_message_t *format_message)
{
	INFO_FLAGS
	struct info_video_stream_s *video;
	info_get_video_stream(info, &video, format_message->id);

	video->w = format_message->width;
	video->h = format_message->height;
	video->flags = format_message->flags;
	video->format = format_message->format;

	print_time(info->stream, info->time);
	if (info->level >= INFO_DETAILED_VIDEO) {
		fprintf(info->stream, "video stream format message\n");

		fprintf(info->stream, "  stream id   = %d\n", format_message->id);
		fprintf(info->stream, "  format      = ");
		switch (video->format) {
			case GLC_VIDEO_BGR:
				fprintf(info->stream, "GLC_VIDEO_BGR\n");
				break;
			case GLC_VIDEO_BGRA:
				fprintf(info->stream, "GLC_VIDEO_BGRA\n");
				break;
			case GLC_VIDEO_YCBCR_420JPEG:
				fprintf(info->stream, "GLC_VIDEO_YCBCR_420JPEG\n");
				break;
			default:
				fprintf(info->stream, "unknown format 0x%02x\n", video->format);
		}
		fprintf(info->stream, "  flags       = ");
		INFO_FLAG(format_message->flags, GLC_VIDEO_DWORD_ALIGNED)
		fprintf(info->stream, "\n");
		fprintf(info->stream, "  width       = %u\n", format_message->width);
		fprintf(info->stream, "  height      = %u\n", format_message->height);
	} else
		fprintf(info->stream, "video stream %d\n", format_message->id);
}

void video_data_info(info_t info, glc_video_data_header_t *pic_header)
{
	struct info_video_stream_s *video;
	info->time = pic_header->time;

	info_get_video_stream(info, &video, pic_header->id);

	if (info->level >= INFO_DETAILED_PICTURE) {
		print_time(info->stream, info->time);
		fprintf(info->stream, "picture\n");

		fprintf(info->stream, "  stream id   = %d\n", pic_header->id);
		fprintf(info->stream, "  time        = %lu\n", pic_header->time);
		fprintf(info->stream, "  size        = %ux%u\n", video->w, video->h);
	} else if (info->level >= INFO_PICTURE) {
		print_time(info->stream, info->time);
		fprintf(info->stream, "picture (video %d)\n", pic_header->id);
	}

	video->pictures++;
	video->fps++;

	if (video->format == GLC_VIDEO_BGR) {
		video->bytes += video->w * video->h * 3;
		if (video->flags & GLC_VIDEO_DWORD_ALIGNED)
			video->bytes += video->h * (8 - (video->w * 3) % 8);
	} else if (video->format == GLC_VIDEO_BGRA) {
		video->bytes += video->w * video->h * 4;
		if (video->flags & GLC_VIDEO_DWORD_ALIGNED)
			video->bytes += video->h * (8 - (video->w * 4) % 8);
	} else if (video->format == GLC_VIDEO_YCBCR_420JPEG)
		video->bytes += (video->w * video->h * 3) / 2;

	if ((info->level >= INFO_FPS) && (pic_header->time - video->fps_time >= 1000000)) {
		print_time(info->stream, info->time);
		fprintf(info->stream, "video %d: %04.2f fps\n", video->id,
			(double) (video->fps * 1000000) / (double) (pic_header->time - video->last_fps_time));
		video->last_fps_time = pic_header->time;
		video->fps_time += 1000000;
		video->fps = 0;
	}
}

void audio_format_info(info_t info, glc_audio_format_message_t *fmt_message)
{
	INFO_FLAGS
	print_time(info->stream, info->time);
	if (info->level >= INFO_DETAILED_AUDIO_FORMAT) {
		fprintf(info->stream, "audio stream format message\n");

		fprintf(info->stream, "  stream id   = %d\n", fmt_message->id);
		fprintf(info->stream, "  format      = ");
		switch (fmt_message->format) {
			case GLC_AUDIO_S16_LE:
				fprintf(info->stream, "GLC_AUDIO_S16_LE\n");
				break;
			case GLC_AUDIO_S24_LE:
				fprintf(info->stream, "GLC_AUDIO_S24_LE\n");
				break;
			case GLC_AUDIO_S32_LE:
				fprintf(info->stream, "GLC_AUDIO_S32_LE\n");
				break;
			default:
				fprintf(info->stream, "unknown format 0x%02x\n",
					fmt_message->format);
		}
		fprintf(info->stream, "  flags       = ");
		INFO_FLAG(fmt_message->flags, GLC_AUDIO_INTERLEAVED)
		fprintf(info->stream, "\n");
		fprintf(info->stream, "  rate        = %d\n", fmt_message->rate);
		fprintf(info->stream, "  channels    = %d\n", fmt_message->channels);
	} else
		fprintf(info->stream, "audio stream %d\n", fmt_message->id);
}

void audio_data_info(info_t info, glc_audio_data_header_t *audio_header)
{
	info->time = audio_header->time;
	struct info_audio_stream_s *audio;

	info_get_audio_stream(info, &audio, audio_header->id);
	audio->packets++;
	audio->bytes += audio_header->size;

	if (info->level >= INFO_AUDIO_DETAILED) {
		print_time(info->stream, info->time);
		fprintf(info->stream, "audio packet\n");
		fprintf(info->stream, "  stream id   = %d\n", audio_header->id);
		fprintf(info->stream, "  time        = %lu\n", audio_header->time);
		fprintf(info->stream, "  size        = %ld\n", audio_header->size);
	} else if (info->level >= INFO_AUDIO) {
		print_time(info->stream, info->time);
		fprintf(info->stream, "audio packet (stream %d)\n", audio_header->id);
	}
}

void color_info(info_t info, glc_color_message_t *color_msg)
{
	print_time(info->stream, info->time);
	if (info->level >= INFO_DETAILED_VIDEO) {
		fprintf(info->stream, "color correction message\n");
		fprintf(info->stream, "  stream id   = %d\n", color_msg->id);
		fprintf(info->stream, "  brightness  = %f\n", color_msg->brightness);
		fprintf(info->stream, "  contrast    = %f\n", color_msg->contrast);
		fprintf(info->stream, "  red gamma   = %f\n", color_msg->red);
		fprintf(info->stream, "  green gamma = %f\n", color_msg->green);
		fprintf(info->stream, "  blue gamma  = %f\n", color_msg->blue);
	} else
		fprintf(info->stream, "color correction information for video %d\n", color_msg->id);
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
