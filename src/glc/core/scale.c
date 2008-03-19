/**
 * \file glc/core/scale.c
 * \brief software scaler
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup scale
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

#include "scale.h"

#define SCALE_RUNNING      0x1
#define SCALE_SIZE         0x2

struct scale_video_stream_s;

typedef void (*scale_proc)(scale_t scale,
			   struct scale_video_stream_s *video,
			   unsigned char *from,
			   unsigned char *to);

struct scale_video_stream_s {
	glc_stream_id_t id;
	glc_flags_t flags;
	glc_video_format_t format;
	size_t size;
	unsigned int w, h, sw, sh, bpp;
	unsigned int row;
	double scale;
	int created;

	unsigned int rw, rh, rx, ry;

	unsigned int *pos;
	float *factor;

	scale_proc proc;

	pthread_rwlock_t update;
	struct scale_video_stream_s *next;
};

struct scale_s {
	glc_t *glc;
	glc_flags_t flags;
	struct scale_video_stream_s *video;
	glc_thread_t thread;

	double scale;
	unsigned int width, height;
};

int scale_read_callback(glc_thread_state_t *state);
int scale_write_callback(glc_thread_state_t *state);
void scale_finish_callback(void *ptr, int err);

int scale_video_format_message(scale_t scale, glc_video_format_message_t *format_message, glc_thread_state_t *state);
int scale_get_video_stream(scale_t scale, glc_stream_id_t id, struct scale_video_stream_s **video);

int scale_generate_rgb_map(scale_t scale, struct scale_video_stream_s *video);
int scale_generate_ycbcr_map(scale_t scale, struct scale_video_stream_s *video);

void scale_rgb_convert(scale_t scale, struct scale_video_stream_s *video,
		       unsigned char *from, unsigned char *to);
void scale_rgb_half(scale_t scale, struct scale_video_stream_s *video,
		    unsigned char *from, unsigned char *to);
void scale_rgb_scale(scale_t scale, struct scale_video_stream_s *video,
		     unsigned char *from, unsigned char *to);

void scale_ycbcr_half(scale_t scale, struct scale_video_stream_s *video,
		      unsigned char *from, unsigned char *to);
void scale_ycbcr_scale(scale_t scale, struct scale_video_stream_s *video,
		       unsigned char *from, unsigned char *to);

int scale_init(scale_t *scale, glc_t *glc)
{
	*scale = malloc(sizeof(struct scale_s));
	memset(*scale, 0, sizeof(struct scale_s));

	(*scale)->glc = glc;

	(*scale)->thread.flags = GLC_THREAD_READ | GLC_THREAD_WRITE;
	(*scale)->thread.read_callback = &scale_read_callback;
	(*scale)->thread.write_callback = &scale_write_callback;
	(*scale)->thread.finish_callback = &scale_finish_callback;
	(*scale)->thread.ptr = *scale;
	(*scale)->thread.threads = glc_threads_hint(glc);
	(*scale)->scale = 1.0;

	return 0;
}

int scale_destroy(scale_t scale)
{
	free(scale);
	return 0;
}

int scale_set_scale(scale_t scale, double factor)
{
	if (factor <= 0)
		return EINVAL;

	scale->scale = factor;
	scale->flags &= ~SCALE_SIZE;
	return 0;
}

int scale_set_size(scale_t scale, unsigned int width, unsigned int height)
{
	if ((!width) | (!height))
		return EINVAL;

	scale->width = width;
	scale->height = height;
	scale->flags |= SCALE_SIZE;
	return 0;
}

int scale_process_start(scale_t scale, ps_buffer_t *from, ps_buffer_t *to)
{
	int ret;
	if (scale->flags & SCALE_RUNNING)
		return EAGAIN;

	if ((ret = glc_thread_create(scale->glc, &scale->thread, from, to)))
		return ret;
	scale->flags |= SCALE_RUNNING;

	return 0;
}

int scale_process_wait(scale_t scale)
{
	if (!(scale->flags & SCALE_RUNNING))
		return EAGAIN;

	/* finish callback frees video stuff */
	glc_thread_wait(&scale->thread);
	scale->flags &= ~SCALE_RUNNING;

	return 0;
}

void scale_finish_callback(void *ptr, int err)
{
	scale_t scale = ptr;
	struct scale_video_stream_s *del;

	if (err)
		glc_log(scale->glc, GLC_ERROR, "scale", "%s (%d)", strerror(err), err);

	while (scale->video != NULL) {
		del = scale->video;
		scale->video = scale->video->next;

		if (del->pos)
			free(del->pos);
		if (del->factor)
			free(del->factor);

		pthread_rwlock_destroy(&del->update);
		free(del);
	}
}

int scale_read_callback(glc_thread_state_t *state) {
	scale_t scale = (scale_t) state->ptr;
	struct scale_video_stream_s *video;
	glc_video_frame_header_t *video_frame_header;

	if (state->header.type == GLC_MESSAGE_VIDEO_FORMAT)
		return scale_video_format_message(scale, (glc_video_format_message_t *) state->read_data, state);

	if (state->header.type == GLC_MESSAGE_VIDEO_FRAME) {
		video_frame_header = (glc_video_frame_header_t *) state->read_data;
		scale_get_video_stream(scale, video_frame_header->id, &video);
		state->threadptr = video;

		pthread_rwlock_rdlock(&video->update);

		if (video->proc)
			state->write_size = video->size + sizeof(glc_video_frame_header_t);
		else {
			state->flags |= GLC_THREAD_COPY;
			pthread_rwlock_unlock(&video->update);
		}
	} else
		state->flags |= GLC_THREAD_COPY;

	return 0;
}

int scale_write_callback(glc_thread_state_t *state) {
	scale_t scale = (scale_t) state->ptr;
	struct scale_video_stream_s *video = state->threadptr;

	memcpy(state->write_data, state->read_data, sizeof(glc_video_frame_header_t));
	video->proc(scale, video,
		  (unsigned char *) &state->read_data[sizeof(glc_video_frame_header_t)],
		  (unsigned char *) &state->write_data[sizeof(glc_video_frame_header_t)]);
	pthread_rwlock_unlock(&video->update);

	return 0;
}

int scale_get_video_stream(scale_t scale, glc_stream_id_t id, struct scale_video_stream_s **video)
{
	struct scale_video_stream_s *list = scale->video;

	while (list != NULL) {
		if (list->id == id)
			break;
		list = list->next;
	}

	if (list == NULL) {
		list = (struct scale_video_stream_s *) malloc(sizeof(struct scale_video_stream_s));
		memset(list, 0, sizeof(struct scale_video_stream_s));

		list->next = scale->video;
		scale->video = list;
		list->id = id;
		pthread_rwlock_init(&list->update, NULL);
	}

	*video = list;
	return 0;
}

void scale_rgb_convert(scale_t scale, struct scale_video_stream_s *video,
		       unsigned char *from, unsigned char *to)
{
	unsigned int x, y, ox, oy, op, tp;
	unsigned int swi = video->sw * 3;
	unsigned int shi = video->sh * 3;
	ox = oy = 0;

	/* just convert from different bpp to 3 */
	for (y = 0; y < shi; y += 3) {
		for (x = 0; x < swi; x += 3) {
			tp = x + y * video->sw;
			op = ox + oy * video->row;

			to[tp + 0] = from[op + 0];
			to[tp + 1] = from[op + 1];
			to[tp + 2] = from[op + 2];

			ox += video->bpp;
		}
		oy++;
		ox = 0;
	}
}

void scale_rgb_half(scale_t scale, struct scale_video_stream_s *video,
		    unsigned char *from, unsigned char *to)
{
	unsigned int ox, oy, op1, op2, op3, op4;

	for (oy = 0; oy < video->h; oy += 2) {
		for (ox = 0; ox < video->w; ox += 2) {
			op1 = ox * video->bpp + oy * video->row;
			op2 = op1 + video->bpp;
			op3 = op1 + video->row;
			op4 = op2 + video->row;

			*to++ = (from[op1 + 0] +
				 from[op2 + 0] +
				 from[op3 + 0] +
				 from[op4 + 0]) >> 2;
			*to++ = (from[op1 + 1] +
				 from[op2 + 1] +
				 from[op3 + 1] +
				 from[op4 + 1]) >> 2;
			*to++ = (from[op1 + 2] +
				 from[op2 + 2] +
				 from[op3 + 2] +
				 from[op4 + 2]) >> 2;

		}
	}
}

void scale_rgb_scale(scale_t scale, struct scale_video_stream_s *video,
		     unsigned char *from, unsigned char *to)
{
	unsigned int x, y, tp, sp;

	if (scale->flags & SCALE_SIZE)
		memset(to, 0, video->size);

	for (y = 0; y < video->sh; y++) {
		for (x = 0; x < video->sw; x++) {
			sp = (x + y * video->sw) * 4;
			tp = ((x + video->rx) + (y + video->ry) * video->rw) * 3;

			to[tp + 0] = from[video->pos[sp + 0] + 0] * video->factor[sp + 0] +
				     from[video->pos[sp + 1] + 0] * video->factor[sp + 1] +
				     from[video->pos[sp + 2] + 0] * video->factor[sp + 2] +
				     from[video->pos[sp + 3] + 0] * video->factor[sp + 3];
			to[tp + 1] = from[video->pos[sp + 0] + 1] * video->factor[sp + 0] +
				     from[video->pos[sp + 1] + 1] * video->factor[sp + 1] +
				     from[video->pos[sp + 2] + 1] * video->factor[sp + 2] +
				     from[video->pos[sp + 3] + 1] * video->factor[sp + 3];
			to[tp + 2] = from[video->pos[sp + 0] + 2] * video->factor[sp + 0] +
				     from[video->pos[sp + 1] + 2] * video->factor[sp + 1] +
				     from[video->pos[sp + 2] + 2] * video->factor[sp + 2] +
				     from[video->pos[sp + 3] + 2] * video->factor[sp + 3];
		}
	}
}

void scale_ycbcr_half(scale_t scale, struct scale_video_stream_s *video,
		      unsigned char *from, unsigned char *to)
{
	unsigned int x, y, ox, oy, cw_from, ch_from, cw_to, ch_to, op1, op2, op3, op4;
	unsigned char *Cb_to, *Cr_to;
	unsigned char *Cb_from, *Cr_from;

	cw_from = video->w / 2;
	ch_from = video->h / 2;
	Cb_from = &from[video->w * video->h];
	Cr_from = &Cb_from[cw_from * ch_from];

	cw_to = video->sw / 2;
	ch_to = video->sh / 2;
	Cb_to = &to[video->sw * video->sh];
	Cr_to = &Cb_to[cw_to * ch_to];

	ox = oy = 0;
	for (y = 0; y < ch_to; y++) {
		for (x = 0; x < cw_to; x++) {
			op1 = oy * cw_from + ox;
			op2 = op1 + 1;
			op3 = op2 + cw_from;
			op4 = op2 + cw_from;

			*Cb_to++ = (Cb_from[op1] +
				    Cb_from[op2] +
				    Cb_from[op3] +
				    Cb_from[op4]) >> 2;
			*Cr_to++ = (Cr_from[op1] +
				    Cr_from[op2] +
				    Cr_from[op3] +
				    Cr_from[op4]) >> 2;

			ox += 2;
		}
		ox = 0;
		oy += 2;
	}

	ox = oy = 0;
	for (y = 0; y < video->sh; y++) {
		for (x = 0; x < video->sw; x++) {
			op1 = oy * video->w + ox;
			op2 = op1 + 1;
			op3 = op1 + video->w;
			op4 = op2 + video->w;

			*to++ = (from[op1] +
				 from[op2] +
				 from[op3] +
				 from[op4]) >> 2;

			ox += 2;
		}
		ox = 0;
		oy += 2;
	}
}

void scale_ycbcr_scale(scale_t scale, struct scale_video_stream_s *video,
		       unsigned char *from, unsigned char *to)
{
	unsigned int x, y, sp, cw, ch;
	unsigned char *Y_to, *Cb_to, *Cr_to;
	unsigned char *Y_from, *Cb_from, *Cr_from;

	Y_from = from;
	Cb_from = &from[video->w * video->h];
	Cr_from = &Cb_from[(video->w / 2) * (video->h / 2)];

	cw = video->sw / 2;
	ch = video->sh / 2;
	Y_to = to;
	Cb_to = &to[video->rw * video->rh];
	Cr_to = &Cb_to[(video->rw / 2) * (video->rh / 2)];

	if (scale->flags & SCALE_SIZE) {
		memset(Y_to, 0, video->rw * video->rh);
		memset(Cb_to, 128, (video->rw / 2) * (video->rh / 2));
		memset(Cr_to, 128, (video->rw / 2) * (video->rh / 2));
	}

	for (y = 0; y < video->sh; y++) {
		for (x = 0; x < video->sw; x++) {
			sp = (x + y * video->sw) * 4;

			Y_to[(x + video->rx) + (y + video->ry) * video->rw] =
				Y_from[video->pos[sp + 0]] * video->factor[sp + 0] +
				Y_from[video->pos[sp + 1]] * video->factor[sp + 1] +
				Y_from[video->pos[sp + 2]] * video->factor[sp + 2] +
				Y_from[video->pos[sp + 3]] * video->factor[sp + 3];
		}
	}

	for (y = 0; y < ch; y++) {
		for (x = 0; x < cw; x++) {
			sp = video->sw * video->sh * 4 + (x + y * cw) * 4;

			Cb_to[(x + video->rx / 2) + (y + video->ry / 2) * (video->rw / 2)] =
				Cb_from[video->pos[sp + 0]] * video->factor[sp + 0] +
				Cb_from[video->pos[sp + 1]] * video->factor[sp + 1] +
				Cb_from[video->pos[sp + 2]] * video->factor[sp + 2] +
				Cb_from[video->pos[sp + 3]] * video->factor[sp + 3];

			Cr_to[(x + video->rx / 2) + (y + video->ry / 2) * (video->rw / 2)] =
				Cr_from[video->pos[sp + 0]] * video->factor[sp + 0] +
				Cr_from[video->pos[sp + 1]] * video->factor[sp + 1] +
				Cr_from[video->pos[sp + 2]] * video->factor[sp + 2] +
				Cr_from[video->pos[sp + 3]] * video->factor[sp + 3];
		}
	}
}

int scale_video_format_message(scale_t scale,
			       glc_video_format_message_t *format_message,
			       glc_thread_state_t *state)
{
	struct scale_video_stream_s *video;
	glc_flags_t old_flags;

	scale_get_video_stream(scale, format_message->id, &video);
	pthread_rwlock_wrlock(&video->update);

	old_flags = video->flags;
	video->flags = format_message->flags;
	video->format = format_message->format;
	video->w = format_message->width;
	video->h = format_message->height;

	if (scale->flags & SCALE_SIZE) {
		video->rw = scale->width;
		video->rh = scale->height;

		if ((float) video->rw / (float) video->w < (float) video->rh / (float) video->h)
			video->scale = (float) video->rw / (float) video->w;
		else
			video->scale = (float) video->rh / (float) video->h;

		video->sw = video->scale * video->w;
		video->sh = video->scale * video->h;
		video->rx = (video->rw - video->sw) / 2;
		video->ry = (video->rh - video->sh) / 2;
		glc_log(scale->glc, GLC_DEBUG, "scale",
			 "real size is %ux%u, scaled picture starts at %ux%u",
			 video->rw, video->rh, video->rx, video->ry);
	} else {
		video->scale = scale->scale;
		video->sw = video->scale * video->w;
		video->sh = video->scale * video->h;

		video->rx = video->ry = 0;
		video->rw = video->sw;
		video->rh = video->sh;
	}

	if ((video->format == GLC_VIDEO_BGRA) |
	    (video->format == GLC_VIDEO_BGR)) {
		if (video->format == GLC_VIDEO_BGRA)
			video->bpp = 4;
		else
			video->bpp = 3;

		video->row = video->w * video->bpp;

		if (format_message->flags & GLC_VIDEO_DWORD_ALIGNED) {
			if (video->row % 8 != 0)
				video->row += 8 - video->row % 8;
		}
	}

	video->proc = NULL; /* do not try anything stupid... */

	if ((video->format == GLC_VIDEO_BGR) |
	    (video->format == GLC_VIDEO_BGRA)) {
		if ((video->scale == 0.5) && !(scale->flags & SCALE_SIZE)) {
			glc_log(scale->glc, GLC_DEBUG, "scale",
				 "scaling RGB data to half-size (from %ux%u to %ux%u)",
				 video->w, video->h, video->sw, video->sh);
			video->proc = scale_rgb_half;
		} else if ((video->scale == 1.0) &&
			   (video->format == GLC_VIDEO_BGRA)) {
			glc_log(scale->glc, GLC_DEBUG, "scale", "converting BGRA to BGR");
			video->proc = scale_rgb_convert;
		} else if (video->scale != 1.0) {
			glc_log(scale->glc, GLC_DEBUG, "scale",
				 "scaling RGB data with factor %f (from %ux%u to %ux%u)",
				 video->scale, video->w, video->h, video->sw, video->sh);
			video->proc = scale_rgb_scale;
			scale_generate_rgb_map(scale, video);
		}

		format_message->format = GLC_VIDEO_BGR; /* after scaling data is in BGR */

		if (video->proc) /* dword alignment is lost if something is done to the data */
			format_message->flags &= ~GLC_VIDEO_DWORD_ALIGNED;

		format_message->width = video->rw;
		format_message->height = video->rh;
		video->size = video->rw * video->rh * 3;

		if ((scale->flags & SCALE_SIZE) && (video->created) &&
		    (format_message->flags == old_flags))
			state->flags |= GLC_THREAD_STATE_SKIP_WRITE;
		video->created = 1;
	} else if (video->format == GLC_VIDEO_YCBCR_420JPEG) {
		video->sw -= video->sw % 2;
		video->sh -= video->sh % 2;
		video->rw -= video->rw % 2;
		video->rh -= video->rh % 2;
		format_message->width = video->rw;
		format_message->height = video->rh;
		video->size = video->rw * video->rh + 2 * ((video->rw / 2) * (video->rh / 2));

		if ((video->scale == 0.5) && !(scale->flags & SCALE_SIZE)) {
			glc_log(scale->glc, GLC_DEBUG, "scale",
				 "scaling Y'CbCr data to half-size (from %ux%u to %ux%u)",
				 video->w, video->h, video->sw, video->sh);
			video->proc = scale_ycbcr_half;
		} else if (video->scale != 1.0) {
			glc_log(scale->glc, GLC_DEBUG, "scale",
				 "scaling Y'CbCr data with factor %f (from %ux%u to %ux%u)",
				 video->scale, video->w, video->h, video->sw, video->sh);
			video->proc = scale_ycbcr_scale;
			scale_generate_ycbcr_map(scale, video);
		}

		if ((scale->flags & SCALE_SIZE) && (video->created) &&
		    (format_message->flags == old_flags))
			state->flags |= GLC_THREAD_STATE_SKIP_WRITE;
		video->created = 1;
	}

	state->flags |= GLC_THREAD_COPY;

	pthread_rwlock_unlock(&video->update);
	return 0;
}

int scale_generate_rgb_map(scale_t scale, struct scale_video_stream_s *video)
{
	float ofx, ofy, fx0, fx1, fy0, fy1;
	unsigned int tp, x, y, r;
	float d;
	size_t smap_size = video->sw * video->sh * 3 * 4;

	glc_log(scale->glc, GLC_DEBUG, "scale", "generating %zd + %zd byte scale map for video stream %d",
		 smap_size * sizeof(unsigned int), smap_size * sizeof(float), video->id);

	if (video->pos)
		video->pos = (unsigned int *) realloc(video->pos, sizeof(unsigned int) * smap_size);
	else
		video->pos = (unsigned int *) malloc(sizeof(unsigned int) * smap_size);
	if (video->factor)
		video->factor = (float *) realloc(video->factor, sizeof(float) * smap_size);
	else
		video->factor = (float *) malloc(sizeof(float) * smap_size);

	r = 0;
	do {
		d = (float) (video->w - r++) / (float) video->sw;
		glc_log(scale->glc, GLC_DEBUG, "scale", "d = %f", d);
	} while ((d * (float) (video->sh - 1) + 1.0 > video->h) |
		 (d * (float) (video->sw - 1) + 1.0 > video->w));

	ofx = ofy = 0;
	for (y = 0; y < video->sh; y++) {
		for (x = 0; x < video->sw; x++) {
			tp = (x + y * video->sw) * 4;

			video->pos[tp + 0] = ((unsigned int) ofx + 0) * video->bpp +
					   ((unsigned int) ofy + 0) * video->row;
			video->pos[tp + 1] = ((unsigned int) ofx + 1) * video->bpp +
					   ((unsigned int) ofy + 0) * video->row;
			video->pos[tp + 2] = ((unsigned int) ofx + 0) * video->bpp +
					   ((unsigned int) ofy + 1) * video->row;
			video->pos[tp + 3] = ((unsigned int) ofx + 1) * video->bpp +
					   ((unsigned int) ofy + 1) * video->row;

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

int scale_generate_ycbcr_map(scale_t scale, struct scale_video_stream_s *video)
{
	float ofx, ofy, fx0, fx1, fy0, fy1;
	unsigned int tp, x, y, r, cw, ch;
	float d;
	size_t smap_size = video->sw * video->sh * 5; /* yw*yh*4 + (ch*cw)*4, ch = yh/2, cw = yw/2 */

	glc_log(scale->glc, GLC_DEBUG, "scale", "generating %zd B + %zd B scale map for video stream %d",
		 smap_size * sizeof(unsigned int), smap_size * sizeof(float), video->id);

	if (video->pos)
		video->pos = (unsigned int *) realloc(video->pos, sizeof(unsigned int) * smap_size);
	else
		video->pos = (unsigned int *) malloc(sizeof(unsigned int) * smap_size);
	if (video->factor)
		video->factor = (float *) realloc(video->factor, sizeof(float) * smap_size);
	else
		video->factor = (float *) malloc(sizeof(float) * smap_size);

	r = 0;
	do {
		d = (float) (video->w - r++) / (float) video->sw;
		glc_log(scale->glc, GLC_DEBUG, "scale", "Y: d = %f", d);
	} while ((d * (float) (video->sh - 1) + 1.0 > video->h) |
		 (d * (float) (video->sw - 1) + 1.0 > video->w));

	ofx = ofy = 0;
	for (y = 0; y < video->sh; y++) {
		for (x = 0; x < video->sw; x++) {
			tp = (x + y * video->sw) * 4;

			video->pos[tp + 0] = ((unsigned int) ofx + 0) +
					   ((unsigned int) ofy + 0) * video->w;
			video->pos[tp + 1] = ((unsigned int) ofx + 1) +
					   ((unsigned int) ofy + 0) * video->w;
			video->pos[tp + 2] = ((unsigned int) ofx + 0) +
					   ((unsigned int) ofy + 1) * video->w;
			video->pos[tp + 3] = ((unsigned int) ofx + 1) +
					   ((unsigned int) ofy + 1) * video->w;

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

	cw = video->sw / 2;
	ch = video->sh / 2;
	r = (r < 2) ? (0) : (r - 2);
	do {
		d = (float) ((video->w / 2) - r++) / (float) cw;
		glc_log(scale->glc, GLC_DEBUG, "scale", "C: d = %f", d);
	} while ((d * (float) (ch - 1) + 1.0 > (video->h / 2)) |
		 (d * (float) (cw - 1) + 1.0 > (video->w / 2)));

	ofx = ofy = 0;
	for (y = 0; y < ch; y++) {
		for (x = 0; x < cw; x++) {
			tp = video->sw * video->sh * 4 + (x + y * cw) * 4;

			video->pos[tp + 0] = ((unsigned int) ofx + 0) +
					   ((unsigned int) ofy + 0) * (video->w / 2);
			video->pos[tp + 1] = ((unsigned int) ofx + 1) +
					   ((unsigned int) ofy + 0) * (video->w / 2);
			video->pos[tp + 2] = ((unsigned int) ofx + 0) +
					   ((unsigned int) ofy + 1) * (video->w / 2);
			video->pos[tp + 3] = ((unsigned int) ofx + 1) +
					   ((unsigned int) ofy + 1) * (video->w / 2);

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
