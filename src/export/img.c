/**
 * \file src/export/img.c
 * \brief export to images
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup export
 *  \{
 * \defgroup img export to images
 *  \{
 */

/** \todo employ threads in image compression */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <png.h>
#include <packetstream.h>

#include "../common/glc.h"
#include "../common/thread.h"
#include "../common/util.h"
#include "img.h"

struct img_private_s;
typedef int (*img_write_proc)(struct img_private_s *img,
			      const unsigned char *pic,
			      unsigned int w,
			      unsigned int h,
			      const char *filename);

struct img_private_s {
	glc_t *glc;
	glc_utime_t fps;
	glc_thread_t thread;

	unsigned int w, h;
	unsigned int row;
	unsigned char *prev_pic;
	glc_utime_t time;
	int i;

	img_write_proc write_proc;
};

void img_finish_callback(void *ptr, int err);
int img_read_callback(glc_thread_state_t *state);

int img_ctx_msg(struct img_private_s *img, glc_ctx_message_t *ctx_msg);
int img_pic(struct img_private_s *img, glc_picture_header_t *pic_hdr,
	    const unsigned char *pic, size_t pic_size);

int img_write_bmp(struct img_private_s *img, const unsigned char *pic,
		  unsigned int w, unsigned int h,
		  const char *filename);
int img_write_png(struct img_private_s *img, const unsigned char *pic,
		  unsigned int w, unsigned int h,
		  const char *filename);

void *img_init(glc_t *glc, ps_buffer_t *from)
{
	struct img_private_s *img = malloc(sizeof(struct img_private_s));
	memset(img, 0, sizeof(struct img_private_s));

	img->glc = glc;
	img->fps = 1000000 / img->glc->fps;

	if (img->glc->flags & GLC_EXPORT_PNG)
		img->write_proc = &img_write_png;
	else
		img->write_proc = &img_write_bmp;

	img->thread.flags = GLC_THREAD_READ;
	img->thread.ptr = img;
	img->thread.read_callback = &img_read_callback;
	img->thread.finish_callback = &img_finish_callback;
	img->thread.threads = 1;

	if (glc_thread_create(glc, &img->thread, from, NULL))
		return NULL;

	return img;
}

int img_wait(void *imgpriv)
{
	struct img_private_s *img = imgpriv;

	glc_thread_wait(&img->thread);
	free(img);

	return 0;
}

void img_finish_callback(void *ptr, int err)
{
	struct img_private_s *img = (struct img_private_s *) ptr;

	util_log(img->glc, GLC_INFORMATION, "img", "%d images written", img->i);

	if (err)
		util_log(img->glc, GLC_ERROR, "img", "%s (%d)", strerror(err), err);

	if (img->prev_pic)
		free(img->prev_pic);
}

int img_read_callback(glc_thread_state_t *state)
{
	struct img_private_s *img = state->ptr;
	int ret = 0;

	if (state->header.type == GLC_MESSAGE_CTX) {
		ret = img_ctx_msg(img, (glc_ctx_message_t *) state->read_data);
	} else if (state->header.type == GLC_MESSAGE_PICTURE) {
		ret = img_pic(img, (glc_picture_header_t *) state->read_data,
			      (const unsigned char *) &state->read_data[GLC_PICTURE_HEADER_SIZE],
			      state->read_size);
	}

	return ret;
}

int img_ctx_msg(struct img_private_s *img, glc_ctx_message_t *ctx_msg)
{
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
		img->prev_pic = (unsigned char *) realloc(img->prev_pic, img->row * img->h);
	else
		img->prev_pic = (unsigned char *) malloc(img->row * img->h);
	memset(img->prev_pic, 0, img->row * img->h);

	return 0;
}

int img_pic(struct img_private_s *img, glc_picture_header_t *pic_hdr,
	    const unsigned char *pic, size_t pic_size)
{
	int ret = 0;
	char filename[1024];

	if (pic_hdr->ctx != img->glc->export_ctx)
		return 0;

	if (img->time < pic_hdr->timestamp) {
		/* write previous pic until we are 'fps' away from current time */
		while (img->time + img->fps < pic_hdr->timestamp) {
			img->time += img->fps;

			snprintf(filename, sizeof(filename) - 1, img->glc->filename_format, img->i++);
			img->write_proc(img, img->prev_pic, img->w, img->h, filename);
		}

		img->time += img->fps;

		snprintf(filename, sizeof(filename) - 1, img->glc->filename_format, img->i++);
		ret = img->write_proc(img, pic, img->w, img->h, filename);
	}

	memcpy(img->prev_pic, pic, pic_size);

	return ret;
}

int img_write_bmp(struct img_private_s *img, const unsigned char *pic,
		  unsigned int w, unsigned int h, const char *filename)
{
	FILE *fd;
	unsigned int val;
	unsigned int i;

	util_log(img->glc, GLC_INFORMATION, "img",
		 "opening %s for writing (BMP)", filename);
	if (!(fd = fopen(filename, "w")))
		return errno;

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

	return 0;
}

int img_write_png(struct img_private_s *img, const unsigned char *pic,
		  unsigned int w, unsigned int h,
		  const char *filename)
{
	png_structp png_ptr;
	png_infop info_ptr;
	png_bytep *row_pointers;
	unsigned int i;
	FILE *fd;

	util_log(img->glc, GLC_INFORMATION, "img",
		 "opening %s for writing (PNG)", filename);
	if (!(fd = fopen(filename, "w")))
		return errno;

	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING,
					  (png_voidp) NULL, NULL, NULL);
	info_ptr = png_create_info_struct(png_ptr);
	setjmp(png_jmpbuf(png_ptr));
	png_init_io(png_ptr, fd);
	png_set_IHDR(png_ptr, info_ptr, w, h, 8, PNG_COLOR_TYPE_RGB,
		     PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
		     PNG_FILTER_TYPE_DEFAULT);
	png_set_bgr(png_ptr);
	row_pointers = (png_bytep *) png_malloc(png_ptr, h * sizeof(png_bytep));

	for (i = 0; i < h; i++)
		row_pointers[i] = (png_bytep) &pic[(h - i - 1) * img->row];

	png_set_rows(png_ptr, info_ptr, row_pointers);
	png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);
	png_free(png_ptr, row_pointers);
	png_destroy_write_struct(&png_ptr, &info_ptr);

	fclose(fd);

	return 0;
}

/**  \} */
/**  \} */
