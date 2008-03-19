/**
 * \file glc/core/ycbcr.c
 * \brief convert BGR to Y'CbCr and scale
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup ycbcr
 *  \{
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <packetstream.h>
#include <pthread.h>
#include <errno.h>

#include <glc/common/glc.h>
#include <glc/common/core.h>
#include <glc/common/log.h>
#include <glc/common/thread.h>
#include <glc/common/util.h>

#include "ycbcr.h"

/*
http://en.wikipedia.org/wiki/YCbCr:
JPEG-Y'CbCr (601)

Y' =       + 0.299    * R'd + 0.587    * G'd + 0.114    * B'd
Cb = 128   - 0.168736 * R'd - 0.331264 * G'd + 0.5      * B'd
Cr = 128   + 0.5      * R'd - 0.418688 * G'd - 0.081312 * B'd
R'd, G'd, B'd   in {0, 1, 2, ..., 255}
Y', Cb, Cr      in {0, 1, 2, ..., 255}
*/

/*
#define RGB_TO_YCbCrJPEG_Y(Rd, Gd, Bd) \
	(    + 0.299    * (Rd) +    0.587 * (Gd) + 0.114 *    (Bd))
#define RGB_TO_YCbCrJPEG_Cb(Rd, Gd, Bd) \
	(128 - 0.168736 * (Rd) - 0.331264 * (Gd) + 0.5      * (Bd))
#define RGB_TO_YCbCrJPEG_Cr(Rd, Gd, Bd) \
	(128 + 0.5      * (Rd) - 0.418688 * (Gd) - 0.081312 * (Bd))
*/

#define RGB_TO_YCbCrJPEG_Y(Rd, Gd, Bd) \
	(    + ((306 * (Rd) + 601 * (Gd) + 117 * (Bd)) >> 10))
#define RGB_TO_YCbCrJPEG_Cb(Rd, Gd, Bd) \
	(128 - ((173 * (Rd) + 339 * (Gd) - 512 * (Bd)) >> 10))
#define RGB_TO_YCbCrJPEG_Cr(Rd, Gd, Bd) \
	(128 + ((512 * (Rd) - 429 * (Gd) -  83 * (Bd)) >> 10))

struct ycbcr_video_stream_s;
struct ycbcr_private_s;

typedef void (*ycbcr_convert_proc)(ycbcr_t ycbcr,
				   struct ycbcr_video_stream_s *video,
				   unsigned char *from,
				   unsigned char *to);

struct ycbcr_video_stream_s {
	glc_stream_id_t id;
	unsigned int w, h, bpp;
	unsigned int yw, yh;
	unsigned int cw, ch;
	unsigned int row;
	double scale;
	size_t size;

	unsigned int *pos;
	float *factor;

	ycbcr_convert_proc convert;

	pthread_rwlock_t update;
	struct ycbcr_video_stream_s *next;
};

struct ycbcr_s {
	glc_t *glc;
	glc_thread_t thread;
	int running;
	double scale;

	struct ycbcr_video_stream_s *video;
};

int ycbcr_read_callback(glc_thread_state_t *state);
int ycbcr_write_callback(glc_thread_state_t *state);
void ycbcr_finish_callback(void *ptr, int err);

int ycbcr_video_format_message(ycbcr_t ycbcr, glc_video_format_message_t *video_format);
void ycbcr_get_video_stream(ycbcr_t ycbcr, glc_stream_id_t id, struct ycbcr_video_stream_s **video);

int ycbcr_generate_map(ycbcr_t ycbcr, struct ycbcr_video_stream_s *video);

void ycbcr_bgr_to_jpeg420(ycbcr_t ycbcr, struct ycbcr_video_stream_s *video,
			  unsigned char *from, unsigned char *to);
void ycbcr_bgr_to_jpeg420_half(ycbcr_t ycbcr, struct ycbcr_video_stream_s *video,
			       unsigned char *from, unsigned char *to);
void ycbcr_bgr_to_jpeg420_scale(ycbcr_t ycbcr, struct ycbcr_video_stream_s *video,
				unsigned char *from, unsigned char *to);

int ycbcr_init(ycbcr_t *ycbcr, glc_t *glc)
{
	*ycbcr = malloc(sizeof(struct ycbcr_s));
	memset(*ycbcr, 0, sizeof(struct ycbcr_s));

	(*ycbcr)->glc = glc;

	(*ycbcr)->thread.flags = GLC_THREAD_READ | GLC_THREAD_WRITE;
	(*ycbcr)->thread.read_callback = &ycbcr_read_callback;
	(*ycbcr)->thread.write_callback = &ycbcr_write_callback;
	(*ycbcr)->thread.finish_callback = &ycbcr_finish_callback;
	(*ycbcr)->thread.ptr = *ycbcr;
	(*ycbcr)->thread.threads = glc_threads_hint(glc);
	(*ycbcr)->scale = 1.0;

	return 0;
}

int ycbcr_destroy(ycbcr_t ycbcr)
{
	free(ycbcr);
	return 0;
}

int ycbcr_set_scale(ycbcr_t ycbcr, double scale)
{
	if (scale <= 0)
		return EINVAL;

	ycbcr->scale = scale;
	return 0;
}

int ycbcr_process_start(ycbcr_t ycbcr, ps_buffer_t *from, ps_buffer_t *to)
{
	int ret;

	if (ycbcr->running)
		return EAGAIN;

	if ((ret = glc_thread_create(ycbcr->glc, &ycbcr->thread, from, to)))
		return ret;
	ycbcr->running = 1;

	return 0;
}

int ycbcr_process_wait(ycbcr_t ycbcr)
{
	/* finish callback takes care of old video data */

	if (!ycbcr->running)
		return EAGAIN;

	glc_thread_wait(&ycbcr->thread);
	ycbcr->running = 0;

	return 0;
}

void ycbcr_finish_callback(void *ptr, int err)
{
	ycbcr_t ycbcr = ptr;
	struct ycbcr_video_stream_s *del;

	if (err)
		glc_log(ycbcr->glc, GLC_ERROR, "ycbcr", "%s (%d)", strerror(err), err);

	while (ycbcr->video != NULL) {
		del = ycbcr->video;
		ycbcr->video = ycbcr->video->next;

		if (del->pos)
			free(del->pos);
		if (del->factor)
			free(del->factor);

		pthread_rwlock_destroy(&del->update);
		free(del);
	}
}

int ycbcr_read_callback(glc_thread_state_t *state)
{
	ycbcr_t ycbcr = state->ptr;
	struct ycbcr_video_stream_s *video;
	glc_video_data_header_t *pic_hdr;

	if (state->header.type == GLC_MESSAGE_VIDEO_FORMAT)
		ycbcr_video_format_message(ycbcr, (glc_video_format_message_t *) state->read_data);

	if (state->header.type == GLC_MESSAGE_VIDEO_DATA) {
		pic_hdr = (glc_video_data_header_t *) state->read_data;
		printf("%lu\n", pic_hdr->time);
		ycbcr_get_video_stream(ycbcr, pic_hdr->id, &video);
		state->threadptr = video;

		pthread_rwlock_rdlock(&video->update);

		if (video->convert != NULL)
			state->write_size = sizeof(glc_video_data_header_t) + video->size;
		else {
			state->flags |= GLC_THREAD_COPY;
			pthread_rwlock_unlock(&video->update);
		}
	} else
		state->flags |= GLC_THREAD_COPY;

	return 0;
}

int ycbcr_write_callback(glc_thread_state_t *state)
{
	ycbcr_t ycbcr = state->ptr;
	struct ycbcr_video_stream_s *video = state->threadptr;

	memcpy(state->write_data, state->read_data, sizeof(glc_video_data_header_t));
	video->convert(ycbcr, video,
		     (unsigned char *) &state->read_data[sizeof(glc_video_data_header_t)],
		     (unsigned char *) &state->write_data[sizeof(glc_video_data_header_t)]);
	pthread_rwlock_unlock(&video->update);

	return 0;
}

void ycbcr_get_video_stream(ycbcr_t ycbcr, glc_stream_id_t id, struct ycbcr_video_stream_s **video)
{
	*video = ycbcr->video;

	while (*video != NULL) {
		if ((*video)->id == id)
			break;
		*video = (*video)->next;
	}

	if (*video == NULL) {
		*video = malloc(sizeof(struct ycbcr_video_stream_s));
		memset(*video, 0, sizeof(struct ycbcr_video_stream_s));

		(*video)->next = ycbcr->video;
		ycbcr->video = *video;
		(*video)->id = id;
		pthread_rwlock_init(&(*video)->update, NULL);
	}
}

void ycbcr_bgr_to_jpeg420(ycbcr_t ycbcr, struct ycbcr_video_stream_s *video,
			  unsigned char *from, unsigned char *to)
{
	unsigned int Ypix;
	unsigned int op1, op2, op3, op4;
	unsigned char Rd, Gd, Bd;
	unsigned int ox, oy, Yy, Yx;
	unsigned char *Y, *Cb, *Cr;

	Y = to;
	Cb = &to[video->yw * video->yh];
	Cr = &to[video->yw * video->yh + video->cw * video->ch];

	oy = (video->h - 2) * video->row;
	ox = 0;

	for (Yy = 0; Yy < video->yh; Yy += 2) {
		for (Yx = 0; Yx < video->yw; Yx += 2) {
			op1 = ox + oy;
			op2 = op1 + video->bpp;
			op3 = op1 + video->row;
			op4 = op2 + video->row;
			Rd = (from[op1 + 2] + from[op2 + 2] + from[op3 + 2] + from[op4 + 2]) >> 2;
			Gd = (from[op1 + 1] + from[op2 + 1] + from[op3 + 1] + from[op4 + 1]) >> 2;
			Bd = (from[op1 + 0] + from[op2 + 0] + from[op3 + 0] + from[op4 + 0]) >> 2;

			/* CbCr */
			*Cb++ = RGB_TO_YCbCrJPEG_Cb(Rd, Gd, Bd);
			*Cr++ = RGB_TO_YCbCrJPEG_Cr(Rd, Gd, Bd);

			/* Y' */
			Ypix = Yx + Yy * video->yw;
			Y[Ypix] = RGB_TO_YCbCrJPEG_Y(from[op3 + 2],
						     from[op3 + 1],
						     from[op3 + 0]);
			Y[Ypix + 1] = RGB_TO_YCbCrJPEG_Y(from[op4 + 2],
							 from[op4 + 1],
							 from[op4 + 0]);
			Y[Ypix + video->yw] = RGB_TO_YCbCrJPEG_Y(from[op1 + 2],
							       from[op1 + 1],
							       from[op1 + 0]);
			Y[Ypix + 1 + video->yw] = RGB_TO_YCbCrJPEG_Y(from[op2 + 2],
								   from[op2 + 1],
								   from[op2 + 0]);
			ox += video->bpp * 2;
		}
		ox = 0;
		oy -= 2 * video->row;
	}
}

#define CALC_BILINEAR_RGB(x0, x1, y0, y1) \
	op1 = (ox + x0) + (oy + y0) * video->row; \
	op2 = op1 + (x1 - x0); \
	op3 = op1 + (y1 - y0) * video->row; \
	op4 = op2 + (y1 - y0) * video->row; \
	Rd = (from[op1 + 2] + from[op2 + 2] + from[op3 + 2] + from[op4 + 2]) >> 2; \
	Gd = (from[op1 + 1] + from[op2 + 1] + from[op3 + 1] + from[op4 + 1]) >> 2; \
	Bd = (from[op1 + 0] + from[op2 + 0] + from[op3 + 0] + from[op4 + 0]) >> 2;

void ycbcr_bgr_to_jpeg420_half(ycbcr_t ycbcr, struct ycbcr_video_stream_s *video,
			       unsigned char *from, unsigned char *to)
{
	unsigned int Ypix;
	unsigned int op1, op2, op3, op4;
	unsigned char Rd, Gd, Bd;
	unsigned int ox, oy, Yy, Yx;
	unsigned char *Cb, *Cr;

	Cb = &to[video->yw * video->yh];
	Cr = &to[video->yw * video->yh + video->cw * video->ch];

	oy = (video->h - 4);
	ox = 0;

	for (Yy = 0; Yy < video->yh; Yy += 2) {
		for (Yx = 0; Yx < video->yw; Yx += 2) {
			/* CbCr */
			CALC_BILINEAR_RGB(video->bpp, video->bpp * 2, 1, 2)
			*Cb++ = RGB_TO_YCbCrJPEG_Cb(Rd, Gd, Bd);
			*Cr++ = RGB_TO_YCbCrJPEG_Cr(Rd, Gd, Bd);

			/* Y' */
			Ypix = Yx + Yy * video->yw;

			CALC_BILINEAR_RGB(0, video->bpp, 2, 3)
			to[Ypix] = RGB_TO_YCbCrJPEG_Y(Rd, Gd, Bd);

			CALC_BILINEAR_RGB(video->bpp * 2, video->bpp * 3, 2, 3)
			to[Ypix + 1] = RGB_TO_YCbCrJPEG_Y(Rd, Gd, Bd);

			CALC_BILINEAR_RGB(0, video->bpp, 0, 1)
			to[Ypix + video->yw] = RGB_TO_YCbCrJPEG_Y(Rd, Gd, Bd);

			CALC_BILINEAR_RGB(video->bpp * 2, video->bpp * 3, 0, 1)
			to[Ypix + 1 + video->yw] = RGB_TO_YCbCrJPEG_Y(Rd, Gd, Bd);

			ox += video->bpp * 4;
		}
		ox = 0;
		oy -= 4;
	}
}

#undef CALC_BILINEAR_RGB

void ycbcr_bgr_to_jpeg420_scale(ycbcr_t ycbcr, struct ycbcr_video_stream_s *video,
				unsigned char *from, unsigned char *to)
{
	unsigned int Cpix;
	unsigned char *Y, *Cb, *Cr;
	unsigned int Cmap, Yy, Yx;
	unsigned char Rd, Gd, Bd;

	Y = to;
	Cb = &to[video->yw * video->yh];
	Cr = &to[video->yw * video->yh + video->cw * video->ch];

	Cpix = 0;
	Cmap = video->yw * video->yh;

#define CALC_Rd(m) (from[video->pos[m + 0] + 2] * video->factor[m + 0] \
		  + from[video->pos[m + 1] + 2] * video->factor[m + 1] \
		  + from[video->pos[m + 2] + 2] * video->factor[m + 2] \
		  + from[video->pos[m + 3] + 2] * video->factor[m + 3])
#define CALC_Bd(m) (from[video->pos[m + 0] + 1] * video->factor[m + 0] \
		  + from[video->pos[m + 1] + 1] * video->factor[m + 1] \
		  + from[video->pos[m + 2] + 1] * video->factor[m + 2] \
		  + from[video->pos[m + 3] + 1] * video->factor[m + 3])
#define CALC_Gd(m) (from[video->pos[m + 0] + 0] * video->factor[m + 0] \
		  + from[video->pos[m + 1] + 0] * video->factor[m + 1] \
		  + from[video->pos[m + 2] + 0] * video->factor[m + 2] \
		  + from[video->pos[m + 3] + 0] * video->factor[m + 3])

#define CALC_RdBdGd(m) \
	Rd = CALC_Rd((m) * 4); \
	Gd = CALC_Bd((m) * 4); \
	Bd = CALC_Gd((m) * 4);

	for (Yy = 0; Yy < video->yh; Yy += 2) {
		for (Yx = 0; Yx < video->yw; Yx += 2) {
			/* CbCr */
			CALC_RdBdGd(Cmap + Cpix)
			Cb[Cpix  ] = RGB_TO_YCbCrJPEG_Cb(Rd, Gd, Bd);
			Cr[Cpix++] = RGB_TO_YCbCrJPEG_Cr(Rd, Gd, Bd);

			/* Y' */
			CALC_RdBdGd((Yx + 0) + (Yy + 0) * video->yw)
			Y[(Yx + 0) + (Yy + 0) * video->yw] = RGB_TO_YCbCrJPEG_Y(Rd, Gd, Bd);

			CALC_RdBdGd((Yx + 1) + (Yy + 0) * video->yw)
			Y[(Yx + 1) + (Yy + 0) * video->yw] = RGB_TO_YCbCrJPEG_Y(Rd, Gd, Bd);

			CALC_RdBdGd((Yx + 0) + (Yy + 1) * video->yw)
			Y[(Yx + 0) + (Yy + 1) * video->yw] = RGB_TO_YCbCrJPEG_Y(Rd, Gd, Bd);

			CALC_RdBdGd((Yx + 1) + (Yy + 1) * video->yw)
			Y[(Yx + 1) + (Yy + 1) * video->yw] = RGB_TO_YCbCrJPEG_Y(Rd, Gd, Bd);
		}
	}
#undef Rd
#undef Gd
#undef Bd
}

int ycbcr_video_format_message(ycbcr_t ycbcr, glc_video_format_message_t *video_format)
{
	struct ycbcr_video_stream_s *video;

	ycbcr_get_video_stream(ycbcr, video_format->id, &video);
	pthread_rwlock_wrlock(&video->update);

	if (video_format->format == GLC_VIDEO_BGRA)
		video->bpp = 4;
	else if (video_format->format == GLC_VIDEO_BGR)
		video->bpp = 3;
	else {
		video->convert = NULL;
		pthread_rwlock_unlock(&video->update);
		return 0;
	}

	video->w = video_format->width;
	video->h = video_format->height;

	video->row = video->w * video->bpp;

	if (video_format->flags & GLC_VIDEO_DWORD_ALIGNED) {
		if (video->row % 8 != 0)
			video->row += 8 - video->row % 8;
	}

	video->scale = ycbcr->scale;
	video->yw = video->w * video->scale;
	video->yh = video->h * video->scale;
	video->yw -= video->yw % 2; /* safer and faster             */
	video->yh -= video->yh % 2; /* but we might drop a pixel... */

	video->cw = video->yw / 2;
	video->ch = video->yh / 2;

	/* nuke old flags */
	video_format->flags &= ~GLC_VIDEO_DWORD_ALIGNED;
	video_format->format = GLC_VIDEO_YCBCR_420JPEG;
	video_format->width = video->yw;
	video_format->height = video->yh;

	if (video->scale == 1.0)
		video->convert = &ycbcr_bgr_to_jpeg420;
	else if (video->scale == 0.5) {
		glc_log(ycbcr->glc, GLC_DEBUG, "ycbcr",
			 "scaling to half-size (from %ux%u to %ux%u)",
			 video->w, video->h, video->yw, video->yh);
		video->convert = &ycbcr_bgr_to_jpeg420_half;
	} else {
		glc_log(ycbcr->glc, GLC_DEBUG, "ycbcr",
			 "scaling with factor %f (from %ux%u to %ux%u)",
			 video->scale, video->w, video->h, video->yw, video->yh);
		video->convert = &ycbcr_bgr_to_jpeg420_scale;
		ycbcr_generate_map(ycbcr, video);
	}

	video->size = video->yw * video->yh + 2 * (video->cw * video->ch);

	pthread_rwlock_unlock(&video->update);
	return 0;
}

/**
 * \todo smaller map is sometimes possible, should inflict better
 *       cache utilization => implement
 */
int ycbcr_generate_map(ycbcr_t ycbcr, struct ycbcr_video_stream_s *video)
{
	size_t scale_maps_size;
	unsigned int tp, x, y, r;
	float d, ofx, ofy, fx0, fx1, fy0, fy1;

	scale_maps_size = video->yw * video->yh * 4 + video->cw * video->ch * 4;
	glc_log(ycbcr->glc, GLC_DEBUG, "ycbcr", "generating %zd + %zd byte scale map for video %d",
		 scale_maps_size * sizeof(unsigned int), scale_maps_size * sizeof(float), video->id);

	if (video->pos)
		video->pos = (unsigned int *) realloc(video->pos, sizeof(unsigned int) * scale_maps_size);
	else
		video->pos = (unsigned int *) malloc(sizeof(unsigned int) * scale_maps_size);

	if (video->factor)
		video->factor = (float *) realloc(video->factor, sizeof(float) * scale_maps_size);
	else
		video->factor = (float *) malloc(sizeof(float) * scale_maps_size);

	/* Y' */
	/* NEVER trust CPU with fp mathematics :/ */
	r = 0;
	do {
		d = (float) (video->w - r++) / (float) video->yw;
		glc_log(ycbcr->glc, GLC_DEBUG, "ycbcr", "Y: d = %f", d);
	} while ((d * (float) (video->yh - 1) + 1.0 > video->h) |
		 (d * (float) (video->yw - 1) + 1.0 > video->w));

	ofx = ofy = 0;

	for (y = 0; y < video->yh; y++) {
		for (x = 0; x < video->yw; x++) {
			tp = (x + y * video->yw) * 4;

			video->pos[tp + 0] = ((unsigned int) ofx + 0) * video->bpp +
			                   (video->h - 1 - (unsigned int) ofy) * video->row;
			video->pos[tp + 1] = ((unsigned int) ofx + 1) * video->bpp +
			                   (video->h - 1 - (unsigned int) ofy) * video->row;
			video->pos[tp + 2] = ((unsigned int) ofx + 0) * video->bpp +
			                   (video->h - 2 - (unsigned int) ofy) * video->row;
			video->pos[tp + 3] = ((unsigned int) ofx + 1) * video->bpp +
			                   (video->h - 2 - (unsigned int) ofy) * video->row;

			fx1 = (float) x * d - (float) ((unsigned int) ofx);
			fx0 = 1.0 - fx1;
			fy1 = (float) y * d - (float) ((unsigned int) ofy);
			fy0 = 1.0 - fy1;

			video->factor[tp + 0] = fx0 * fy0;
			video->factor[tp + 1] = fx1 * fy0;
			video->factor[tp + 2] = fx0 * fy1;
			video->factor[tp + 3] = fx1 * fy1;

			ofx += d;
		}
		ofy += d;
		ofx = 0;
	}

	/* CbCr */
	/* try to match Y */
	r = (r < 2) ? (0) : (r - 2);
	do {
		d = (float) (video->w - r++) / (float) video->cw;
		glc_log(ycbcr->glc, GLC_DEBUG, "ycbcr", "C: d = %f", d);
	} while ((d * (float) (video->ch - 1) + 1.0 > video->h) |
		 (d * (float) (video->cw - 1) + 1.0 > video->w));

	ofx = ofy = 0;

	for (y = 0; y < video->ch; y++) {
		for (x = 0; x < video->cw; x++) {
			tp = (video->yw * video->yh * 4) + (x + y * video->cw) * 4;

			video->pos[tp + 0] = ((unsigned int) ofx + 0) * video->bpp +
			                   (video->h - 1 - (unsigned int) ofy) * video->row;
			video->pos[tp + 1] = ((unsigned int) ofx + 1) * video->bpp +
			                   (video->h - 1 - (unsigned int) ofy) * video->row;
			video->pos[tp + 2] = ((unsigned int) ofx + 0) * video->bpp +
			                   (video->h - 2 - (unsigned int) ofy) * video->row;
			video->pos[tp + 3] = ((unsigned int) ofx + 1) * video->bpp +
			                   (video->h - 2 - (unsigned int) ofy) * video->row;

			fx1 = (float) x * d - (float) ((unsigned int) ofx);
			fx0 = 1.0 - fx1;
			fy1 = (float) y * d - (float) ((unsigned int) ofy);
			fy0 = 1.0 - fy1;

			video->factor[tp + 0] = fx0 * fy0;
			video->factor[tp + 1] = fx1 * fy0;
			video->factor[tp + 2] = fx0 * fy1;
			video->factor[tp + 3] = fx1 * fy1;

			ofx += d;
		}
		ofy += d;
		ofx = 0;
	}

	return 0;
}

/**  \} */
