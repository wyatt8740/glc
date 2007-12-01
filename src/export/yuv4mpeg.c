/**
 * \file src/export/yuv4mpeg.c
 * \brief yuv4mpeg output
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup export
 *  \{
 * \defgroup yuv4mpeg yuv4mpeg output
 *  \{
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <packetstream.h>
#include <sys/types.h>
#include <errno.h>

#include "../common/glc.h"
#include "../common/thread.h"
#include "../common/util.h"
#include "yuv4mpeg.h"

struct yuv4mpeg_private_s {
	glc_t *glc;
	glc_thread_t thread;
	unsigned int file_count;
	FILE *to;

	glc_utime_t time;
	glc_utime_t fps;

	unsigned int size;
	char *prev_pic;
};

int yuv4mpeg_read_callback(glc_thread_state_t *state);
void yuv4mpeg_finish_callback(void *priv, int err);

int yuv4mpeg_handle_hdr(struct yuv4mpeg_private_s *yuv4mpeg, glc_ctx_message_t *ctx_msg);
int yuv4mpeg_handle_pic(struct yuv4mpeg_private_s *yuv4mpeg, glc_picture_header_t *pic_header, char *data);
int yuv4mpeg_write_pic(struct yuv4mpeg_private_s *yuv4mpeg, char *pic);

int yuv4mpeg_init(glc_t *glc, ps_buffer_t *from)
{
	struct yuv4mpeg_private_s *yuv4mpeg = malloc(sizeof(struct yuv4mpeg_private_s));
	memset(yuv4mpeg, 0, sizeof(struct yuv4mpeg_private_s));

	yuv4mpeg->glc = glc;
	yuv4mpeg->fps = 1000000 / yuv4mpeg->glc->fps;

	yuv4mpeg->thread.flags = GLC_THREAD_READ;
	yuv4mpeg->thread.ptr = yuv4mpeg;
	yuv4mpeg->thread.read_callback = &yuv4mpeg_read_callback;
	yuv4mpeg->thread.finish_callback = &yuv4mpeg_finish_callback;
	yuv4mpeg->thread.threads = 1;

	return glc_thread_create(glc, &yuv4mpeg->thread, from, NULL);
}

void yuv4mpeg_finish_callback(void *priv, int err)
{
	struct yuv4mpeg_private_s *yuv4mpeg = (struct yuv4mpeg_private_s *) priv;

	if (err)
		util_log(yuv4mpeg->glc, GLC_ERROR, "yuv4mpeg", "%s (%d)", strerror(err), err);

	sem_post(&yuv4mpeg->glc->signal[GLC_SIGNAL_YUV4MPEG_FINISHED]);
	free(yuv4mpeg);
}

int yuv4mpeg_read_callback(glc_thread_state_t *state)
{
	struct yuv4mpeg_private_s *yuv4mpeg = (struct yuv4mpeg_private_s *) state->ptr;

	if (state->header.type == GLC_MESSAGE_CTX)
		return yuv4mpeg_handle_hdr(yuv4mpeg, (glc_ctx_message_t *) state->read_data);
	else if (state->header.type == GLC_MESSAGE_PICTURE)
		return yuv4mpeg_handle_pic(yuv4mpeg, (glc_picture_header_t *) state->read_data, &state->read_data[GLC_PICTURE_HEADER_SIZE]);

	return 0;
}

int yuv4mpeg_handle_hdr(struct yuv4mpeg_private_s *yuv4mpeg, glc_ctx_message_t *ctx_msg)
{
	char *filename;
	unsigned int p, q;

	if (ctx_msg->ctx != yuv4mpeg->glc->export_ctx)
		return 0;

	if (!(ctx_msg->flags & GLC_CTX_YCBCR_420JPEG))
		return ENOTSUP;

	if (yuv4mpeg->to) {
		util_log(yuv4mpeg->glc, GLC_WARNING, "yuv4mpeg", "ctx update msg");
		yuv4mpeg->time = 0; /* reset time */
	}

	filename = (char *) malloc(1024);
	snprintf(filename, 1023, yuv4mpeg->glc->filename_format, ++yuv4mpeg->file_count);
	util_log(yuv4mpeg->glc, GLC_INFORMATION, "yuv4mpeg", "opening %s for writing", filename);
	yuv4mpeg->to = fopen(filename, "w");
	if (!yuv4mpeg->to) {
		util_log(yuv4mpeg->glc, GLC_ERROR, "yuv4mpeg", "can't open %s", filename);
		free(filename);
		return EINVAL;
	}
	free(filename);

	yuv4mpeg->size = ctx_msg->w * ctx_msg->h + (ctx_msg->w * ctx_msg->h) / 2;

	if (yuv4mpeg->prev_pic)
		yuv4mpeg->prev_pic = (char *) realloc(yuv4mpeg->prev_pic, yuv4mpeg->size);
	else
		yuv4mpeg->prev_pic = (char *) malloc(yuv4mpeg->size);

	/* Set Y' 0 */
	memset(yuv4mpeg->prev_pic, 0, ctx_msg->w * ctx_msg->h);
	/* Set CbCr 128 */
	memset(&yuv4mpeg->prev_pic[ctx_msg->w * ctx_msg->h], 128, (ctx_msg->w * ctx_msg->h) / 2);

	/* calculate fps in p/q */
	/* TODO something more intelligent perhaps... */
	p = yuv4mpeg->glc->fps;
	q = 1;
	while ((p != q * yuv4mpeg->glc->fps) && (q < 1000)) {
		q *= 10;
		p = q * yuv4mpeg->glc->fps;
	}

	fprintf(yuv4mpeg->to, "YUV4MPEG2 W%d H%d F%d:%d Ip\n", ctx_msg->w, ctx_msg->h, p, q);
	return 0;
}

int yuv4mpeg_handle_pic(struct yuv4mpeg_private_s *yuv4mpeg, glc_picture_header_t *pic_hdr, char *data)
{
	if (pic_hdr->ctx != yuv4mpeg->glc->export_ctx)
		return 0;

	if (yuv4mpeg->time < pic_hdr->timestamp) {
		while (yuv4mpeg->time + yuv4mpeg->fps < pic_hdr->timestamp) {
			yuv4mpeg_write_pic(yuv4mpeg, yuv4mpeg->prev_pic);
			yuv4mpeg->time += yuv4mpeg->fps;
		}
		yuv4mpeg_write_pic(yuv4mpeg, data);
		yuv4mpeg->time += yuv4mpeg->fps;
	}

	memcpy(yuv4mpeg->prev_pic, data, yuv4mpeg->size);

	return 0;
}

int yuv4mpeg_write_pic(struct yuv4mpeg_private_s *yuv4mpeg, char *pic)
{
	fprintf(yuv4mpeg->to, "FRAME\n");
	fwrite(pic, 1, yuv4mpeg->size, yuv4mpeg->to);
	return 0;
}

/**  \} */
/**  \} */
