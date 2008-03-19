/**
 * \file glc/export/yuv4mpeg.c
 * \brief yuv4mpeg output
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup yuv4mpeg
 *  \{
 */

#define _FILE_OFFSET_BITS 64

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <packetstream.h>
#include <sys/types.h>
#include <errno.h>

#include <glc/common/glc.h>
#include <glc/common/core.h>
#include <glc/common/log.h>
#include <glc/common/thread.h>
#include <glc/common/util.h>

#include "yuv4mpeg.h"

struct yuv4mpeg_s {
	glc_t *glc;
	glc_thread_t thread;
	int running;

	unsigned int file_count;
	FILE *to;

	glc_utime_t time;
	glc_utime_t fps_usec;
	double fps;

	unsigned int size;
	char *prev_video_data_message;
	int interpolate;

	const char *filename_format;
	glc_stream_id_t id;
};

int yuv4mpeg_read_callback(glc_thread_state_t *state);
void yuv4mpeg_finish_callback(void *priv, int err);

int yuv4mpeg_handle_hdr(yuv4mpeg_t yuv4mpeg, glc_video_format_message_t *video_format);
int yuv4mpeg_handle_video_data_message(yuv4mpeg_t yuv4mpeg, glc_video_data_header_t *pic_header, char *data);
int yuv4mpeg_write_video_data_message(yuv4mpeg_t yuv4mpeg, char *pic);

int yuv4mpeg_init(yuv4mpeg_t *yuv4mpeg, glc_t *glc)
{
	*yuv4mpeg = (yuv4mpeg_t) malloc(sizeof(struct yuv4mpeg_s));
	memset(*yuv4mpeg, 0, sizeof(struct yuv4mpeg_s));

	(*yuv4mpeg)->glc = glc;
	(*yuv4mpeg)->fps = 30;
	(*yuv4mpeg)->fps_usec = 1000000 / (*yuv4mpeg)->fps;
	(*yuv4mpeg)->filename_format = "video%02d.glc";
	(*yuv4mpeg)->id = 1;
	(*yuv4mpeg)->interpolate = 1;

	(*yuv4mpeg)->thread.flags = GLC_THREAD_READ;
	(*yuv4mpeg)->thread.ptr = *yuv4mpeg;
	(*yuv4mpeg)->thread.read_callback = &yuv4mpeg_read_callback;
	(*yuv4mpeg)->thread.finish_callback = &yuv4mpeg_finish_callback;
	(*yuv4mpeg)->thread.threads = 1;

	return 0;
}

int yuv4mpeg_destroy(yuv4mpeg_t yuv4mpeg)
{
	free(yuv4mpeg);
	return 0;
}

int yuv4mpeg_set_filename(yuv4mpeg_t yuv4mpeg, const char *filename)
{
	yuv4mpeg->filename_format = filename;
	return 0;
}

int yuv4mpeg_set_stream_id(yuv4mpeg_t yuv4mpeg, glc_stream_id_t id)
{
	yuv4mpeg->id = id;
	return 0;
}

int yuv4mpeg_set_fps(yuv4mpeg_t yuv4mpeg, double fps)
{
	yuv4mpeg->fps = fps;
	yuv4mpeg->fps_usec = 1000000 / fps;
	return 0;
}

int yuv4mpeg_set_interpolation(yuv4mpeg_t yuv4mpeg, int interpolate)
{
	yuv4mpeg->interpolate = interpolate;
	return 0;
}

int yuv4mpeg_process_start(yuv4mpeg_t yuv4mpeg, ps_buffer_t *from)
{
	int ret;
	if (yuv4mpeg->running)
		return EAGAIN;

	if ((ret = glc_thread_create(yuv4mpeg->glc, &yuv4mpeg->thread, from, NULL)))
		return ret;
	yuv4mpeg->running = 1;

	return 0;
}

int yuv4mpeg_process_wait(yuv4mpeg_t yuv4mpeg)
{
	if (!yuv4mpeg->running)
		return EAGAIN;

	glc_thread_wait(&yuv4mpeg->thread);
	yuv4mpeg->running = 0;

	return 0;
}

void yuv4mpeg_finish_callback(void *priv, int err)
{
	yuv4mpeg_t yuv4mpeg = (yuv4mpeg_t) priv;

	if (err)
		glc_log(yuv4mpeg->glc, GLC_ERROR, "yuv4mpeg", "%s (%d)", strerror(err), err);

	if (yuv4mpeg->to) {
		fclose(yuv4mpeg->to);
		yuv4mpeg->to = NULL;
	}

	if (yuv4mpeg->prev_video_data_message) {
		free(yuv4mpeg->prev_video_data_message);
		yuv4mpeg->prev_video_data_message = NULL;
	}

	yuv4mpeg->file_count = 0;
	yuv4mpeg->time = 0;
}

int yuv4mpeg_read_callback(glc_thread_state_t *state)
{
	yuv4mpeg_t yuv4mpeg = (yuv4mpeg_t) state->ptr;

	if (state->header.type == GLC_MESSAGE_VIDEO_FORMAT)
		return yuv4mpeg_handle_hdr(yuv4mpeg, (glc_video_format_message_t *) state->read_data);
	else if (state->header.type == GLC_MESSAGE_VIDEO_DATA)
		return yuv4mpeg_handle_video_data_message(yuv4mpeg, (glc_video_data_header_t *) state->read_data, &state->read_data[sizeof(glc_video_data_header_t)]);

	return 0;
}

int yuv4mpeg_handle_hdr(yuv4mpeg_t yuv4mpeg, glc_video_format_message_t *video_format)
{
	char *filename;
	unsigned int p, q;

	if (video_format->id != yuv4mpeg->id)
		return 0;

	if (!(video_format->format == GLC_VIDEO_YCBCR_420JPEG))
		return ENOTSUP;

	if (yuv4mpeg->to) {
		fclose(yuv4mpeg->to);
		glc_log(yuv4mpeg->glc, GLC_WARNING, "yuv4mpeg", "video stream configuration changed");
	}

	filename = (char *) malloc(1024);
	snprintf(filename, 1023, yuv4mpeg->filename_format, ++yuv4mpeg->file_count);
	glc_log(yuv4mpeg->glc, GLC_INFORMATION, "yuv4mpeg", "opening %s for writing", filename);

	yuv4mpeg->to = fopen(filename, "w");
	if (!yuv4mpeg->to) {
		glc_log(yuv4mpeg->glc, GLC_ERROR, "yuv4mpeg", "can't open %s", filename);
		free(filename);
		return EINVAL;
	}
	free(filename);

	yuv4mpeg->size = video_format->width * video_format->height +
			 (video_format->width * video_format->height) / 2;

	if (yuv4mpeg->interpolate) {
		if (yuv4mpeg->prev_video_data_message)
			yuv4mpeg->prev_video_data_message = (char *) realloc(yuv4mpeg->prev_video_data_message, yuv4mpeg->size);
		else
			yuv4mpeg->prev_video_data_message = (char *) malloc(yuv4mpeg->size);

		/* Set Y' 0 */
		memset(yuv4mpeg->prev_video_data_message, 0, video_format->width * video_format->height);
		/* Set CbCr 128 */
		memset(&yuv4mpeg->prev_video_data_message[video_format->width * video_format->height],
		       128, (video_format->width * video_format->height) / 2);
	}

	/* calculate fps in p/q */
	/** \todo something more intelligent perhaps... */
	p = yuv4mpeg->fps;
	q = 1;
	while ((p != q * yuv4mpeg->fps) && (q < 1000)) {
		q *= 10;
		p = q * yuv4mpeg->fps;
	}

	fprintf(yuv4mpeg->to, "YUV4MPEG2 W%d H%d F%d:%d Ip\n",
		video_format->width, video_format->height, p, q);
	return 0;
}

int yuv4mpeg_handle_video_data_message(yuv4mpeg_t yuv4mpeg, glc_video_data_header_t *pic_hdr, char *data)
{
	if (pic_hdr->id != yuv4mpeg->id)
		return 0;

	if (yuv4mpeg->time < pic_hdr->time) {
		while (yuv4mpeg->time + yuv4mpeg->fps_usec < pic_hdr->time) {
			if (yuv4mpeg->interpolate)
				yuv4mpeg_write_video_data_message(yuv4mpeg, yuv4mpeg->prev_video_data_message);
			yuv4mpeg->time += yuv4mpeg->fps_usec;
		}
		yuv4mpeg_write_video_data_message(yuv4mpeg, data);
		yuv4mpeg->time += yuv4mpeg->fps_usec;
	}

	if (yuv4mpeg->interpolate)
		memcpy(yuv4mpeg->prev_video_data_message, data, yuv4mpeg->size);

	return 0;
}

int yuv4mpeg_write_video_data_message(yuv4mpeg_t yuv4mpeg, char *pic)
{
	fprintf(yuv4mpeg->to, "FRAME\n");
	fwrite(pic, 1, yuv4mpeg->size, yuv4mpeg->to);
	return 0;
}

/**  \} */
