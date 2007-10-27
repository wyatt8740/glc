/**
 * \file src/stream/img.c
 * \brief export to images
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* img.c -- export to images
 * Copyright (C) 2007 Pyry Haulos
 * For conditions of distribution and use, see copyright notice in glc.h
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <packetstream.h>

#include "../common/glc.h"
#include "../common/thread.h"
#include "img.h"

/**
 * \addtogroup stream
 *  \{
 */

/**
 * \defgroup img export to images
 *  \{
 */

struct img_private_s {
	glc_t *glc;
	glc_utime_t fps;
	glc_thread_t thread;

	unsigned int w, h;
	unsigned int row;
	char *prev_pic;
	glc_utime_t time;
	int i, total;
};

void img_finish_callback(void *ptr, int err);
int img_read_callback(glc_thread_state_t *state);

int write_pic(struct img_private_s *img, char *pic, unsigned int w, unsigned int h, int num);

int img_init(glc_t *glc, ps_buffer_t *from)
{
	struct img_private_s *img = malloc(sizeof(struct img_private_s));
	memset(img, 0, sizeof(struct img_private_s));

	img->glc = glc;
	img->fps = 1000000 / img->glc->fps;
	img->total = 0;

	img->thread.flags = GLC_THREAD_READ;
	img->thread.ptr = img;
	img->thread.read_callback = &img_read_callback;
	img->thread.finish_callback = &img_finish_callback;
	img->thread.threads = 1;

	return glc_thread_create(glc, &img->thread, from, NULL);
}

void img_finish_callback(void *ptr, int err)
{
	struct img_private_s *img = (struct img_private_s *) ptr;

	util_log(img->glc, GLC_INFORMATION, "img", "%d images written", img->total);

	if (err)
		util_log(img->glc, GLC_ERROR, "img", "%s (%d)", strerror(err), err);

	if (img->prev_pic)
		free(img->prev_pic);

	sem_post(&img->glc->signal[GLC_SIGNAL_IMG_FINISHED]);
	free(img);
}

int img_read_callback(glc_thread_state_t *state)
{
	struct img_private_s *img = (struct img_private_s *) state->ptr;

	glc_picture_header_t *pic_hdr;
	glc_ctx_message_t *ctx_msg;

	if (state->header.type == GLC_MESSAGE_CTX) {
		ctx_msg = (glc_ctx_message_t *) state->read_data;

		if (ctx_msg->ctx != img->glc->export_ctx)
			return 0;

		if (!(ctx_msg->flags & GLC_CTX_BGR)) {
			util_log(img->glc, GLC_ERROR, "img",
				 "ctx %d is in unsupported format", ctx_msg->ctx);
			return ENOTSUP;
		}

		img->w = ctx_msg->w;
		img->h = ctx_msg->h;
		img->row = img->w * 3;

		if (ctx_msg->flags & GLC_CTX_DWORD_ALIGNED) {
			if (img->row % 8 != 0)
				img->row += 8 - img->row % 8;
		}

		if (img->prev_pic)
			img->prev_pic = (char *) realloc(img->prev_pic, img->row * img->h);
		else
			img->prev_pic = (char *) malloc(img->row * img->h);
		memset(img->prev_pic, 0, img->row * img->h);
	} else if (state->header.type == GLC_MESSAGE_PICTURE) {
		pic_hdr = (glc_picture_header_t *) state->read_data;

		if (pic_hdr->ctx != img->glc->export_ctx)
			return 0;

		if (img->time < pic_hdr->timestamp) {
			while (img->time + img->fps < pic_hdr->timestamp) {
				write_pic(img, img->prev_pic, img->w, img->h, img->i++);
				img->time += img->fps;
			}
			write_pic(img, &state->read_data[GLC_PICTURE_HEADER_SIZE],
			          img->w, img->h, img->i++);
			img->time += img->fps;
		}

		memcpy(img->prev_pic, &state->read_data[GLC_PICTURE_HEADER_SIZE],
		       state->read_size - GLC_PICTURE_HEADER_SIZE);
	}

	return 0;
}

int write_pic(struct img_private_s *img, char *pic, unsigned int w, unsigned int h, int num)
{
	char fname[1024];
	FILE *fd;
	unsigned int val;
	unsigned int i;
	snprintf(fname, sizeof(fname) - 1, img->glc->filename_format, num);

	if (!(fd = fopen(fname, "w")))
		return 1;

	fwrite("BM", 1, 2, fd);
	val = w * h * 3 + 54;
	fwrite(&val, 1, 4, fd);
	fwrite("\x00\x00\x00\x00\x36\x00\x00\x00\x28\x00\x00\x00", 1, 12, fd);
	fwrite(&w, 1, 4, fd);
	fwrite(&h, 1, 4, fd);
	fwrite("\x01\x00\x18\x00\x00\x00\x00\x00", 1, 8, fd);
	val -= 54;
	fwrite(&val, 1, 4, fd);
	fwrite("\x00\x00\x00\x00\x00\x00\x00\x00\x03\x00\x00\x00\x03\x00\x00\x00", 1, 16, fd);

	for (i = 0; i < h; i++) {
		fwrite(&pic[i * img->row], 1, w * 3, fd);
		if ((w * 3) % 4 != 0)
			fwrite("\x00\x00\x00\x00", 1, 4 - ((w * 3) % 4), fd);
	}

	fclose(fd);

	img->total++;
	return 0;
}


/**  \} */
/**  \} */
