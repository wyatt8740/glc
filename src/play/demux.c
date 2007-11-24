/**
 * \file src/play/demux.c
 * \brief audio/picture stream demuxer
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* demux.c -- audio/picture stream demuxer
 * Copyright (C) 2007 Pyry Haulos
 * For conditions of distribution and use, see copyright notice in glc.h
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <packetstream.h>
#include <errno.h>

#include "../common/glc.h"
#include "../common/thread.h"
#include "../common/util.h"
#include "demux.h"
#include "gl_play.h"
#include "audio_play.h"

/**
 * \addtogroup play
 *  \{
 */

/**
 * \defgroup demux audio/picture stream demuxer
 *  \{
 */

struct demux_ctx_s {
	glc_ctx_i ctx_i;
	ps_buffer_t buffer;
	ps_packet_t packet;

	int running;
	sem_t finished;

	struct demux_ctx_s *next;
};

struct demux_stream_s {
	glc_audio_i audio_i;
	ps_buffer_t buffer;
	ps_packet_t packet;

	int running;
	sem_t finished;

	struct demux_stream_s *next;
};

struct demux_private_s {
	glc_t *glc;
	ps_buffer_t *from;

	pthread_t thread;

	ps_bufferattr_t bufferattr;

	struct demux_ctx_s *ctx;
	struct demux_stream_s *stream;
};

int demux_close(struct demux_private_s *demux);
void *demux_thread(void *argptr);

int demux_video_message(struct demux_private_s *demux, glc_message_header_t *header,
			char *data, size_t size);
int demux_video_close(struct demux_private_s *demux);
int demux_video_get_ctx(struct demux_private_s *demux, glc_ctx_i ctx_i, struct demux_ctx_s **ctx);
int demux_video_ctx_send(struct demux_private_s *demux, struct demux_ctx_s *ctx,
			 glc_message_header_t *header, char *data, size_t size);
int demux_video_ctx_clean(struct demux_private_s *demux, struct demux_ctx_s *ctx);

int demux_audio_message(struct demux_private_s *demux, glc_message_header_t *header,
			char *data, size_t size);
int demux_audio_close(struct demux_private_s *demux);
int demux_audio_get_stream(struct demux_private_s *demux, glc_audio_i audio_i,
			   struct demux_stream_s **stream);
int demux_audio_stream_send(struct demux_private_s *demux, struct demux_stream_s *stream,
			 glc_message_header_t *header, char *data, size_t size);
int demux_audio_stream_clean(struct demux_private_s *demux, struct demux_stream_s *stream);

int demux_init(glc_t *glc, ps_buffer_t *from)
{
	struct demux_private_s *demux = (struct demux_private_s *) malloc(sizeof(struct demux_private_s));
	memset(demux, 0, sizeof(struct demux_private_s));

	demux->glc = glc;
	demux->from = from;

	ps_bufferattr_init(&demux->bufferattr);
	ps_bufferattr_setsize(&demux->bufferattr, glc->uncompressed_size);

	return pthread_create(&demux->thread, NULL, demux_thread, demux);
}

int demux_close(struct demux_private_s *demux)
{
	ps_bufferattr_destroy(&demux->bufferattr);

	if (demux->ctx != NULL)
		demux_video_close(demux);
	if (demux->stream != NULL)
		demux_audio_close(demux);

	sem_post(&demux->glc->signal[GLC_SIGNAL_DEMUX_FINISHED]);
	free(demux);
	return 0;
}

void *demux_thread(void *argptr)
{
	struct demux_private_s *demux = (struct demux_private_s *) argptr;
	glc_message_header_t msg_hdr;
	size_t data_size;
	char *data;
	int ret = 0;

	ps_packet_t read;

	if ((ret = ps_packet_init(&read, demux->from)))
		goto err;

	do {
		if ((ret = ps_packet_open(&read, PS_PACKET_READ)))
			goto err;

		if ((ret = ps_packet_read(&read, &msg_hdr, GLC_MESSAGE_HEADER_SIZE)))
			goto err;
		if ((ret = ps_packet_getsize(&read, &data_size)))
			goto err;
		data_size -= GLC_MESSAGE_HEADER_SIZE;
		if ((ret = ps_packet_dma(&read, (void *) &data, data_size, PS_ACCEPT_FAKE_DMA)))
			goto err;

		if ((msg_hdr.type == GLC_MESSAGE_CLOSE) |
		    (msg_hdr.type == GLC_MESSAGE_PICTURE) |
		    (msg_hdr.type == GLC_MESSAGE_CTX)) {
			/* handle msg to gl_play */
			demux_video_message(demux, &msg_hdr, data, data_size);
		}

		if ((msg_hdr.type == GLC_MESSAGE_CLOSE) |
		    (msg_hdr.type == GLC_MESSAGE_AUDIO_FORMAT) |
		    (msg_hdr.type == GLC_MESSAGE_AUDIO)) {
			/* handle msg to audio_play */
			demux_audio_message(demux, &msg_hdr, data, data_size);
		}

		ps_packet_close(&read);
	} while ((!(demux->glc->flags & GLC_CANCEL)) && (msg_hdr.type != GLC_MESSAGE_CLOSE));

finish:
	ps_packet_destroy(&read);

	if (demux->glc->flags & GLC_CANCEL)
		ps_buffer_cancel(demux->from);

	demux_video_close(demux);
	demux_audio_close(demux);

	demux_close(demux);
	return NULL;
err:
	if (ret == EINTR) /* just _cancel() */
		ret = 0;
	else
		util_log(demux->glc, GLC_ERROR, "demux", "%s (%d)", strerror(ret), ret);
	goto finish;
}

int demux_video_message(struct demux_private_s *demux, glc_message_header_t *header,
			char *data, size_t size)
{
	struct demux_ctx_s *ctx;
	glc_ctx_i ctx_i;
	int ret;

	if (header->type == GLC_MESSAGE_CLOSE) {
		/* broadcast to all */
		ctx = demux->ctx;
		while (ctx != NULL) {
			if (ctx->running) {
				if ((ret = demux_video_ctx_send(demux, ctx, header, data, size)))
					return ret;
			}
			ctx = ctx->next;
		}
		return 0;
	} else if (header->type == GLC_MESSAGE_CTX)
		ctx_i = ((glc_ctx_message_t *) data)->ctx;
	else if (header->type == GLC_MESSAGE_PICTURE)
		ctx_i = ((glc_picture_header_t *) data)->ctx;
	else
		return EINVAL;

	/* pass to single client */
	if ((ret = demux_video_get_ctx(demux, ctx_i, &ctx)))
		return ret;

	if ((ret = demux_video_ctx_send(demux, ctx, header, data, size)))
		return ret;

	return 0;
}

int demux_video_ctx_send(struct demux_private_s *demux, struct demux_ctx_s *ctx,
			 glc_message_header_t *header, char *data, size_t size)
{
	int ret;
	if ((ret = ps_packet_open(&ctx->packet, PS_PACKET_WRITE)))
		goto err;
	if ((ret = ps_packet_write(&ctx->packet, header, GLC_MESSAGE_HEADER_SIZE)))
		goto err;
	if ((ret = ps_packet_write(&ctx->packet, data, size)))
		goto err;
	if ((ret = ps_packet_close(&ctx->packet)))
		goto err;
err:
	if (ret != EINTR)
		return ret;

	/* since it is EINTR, _cancel() is already done */
	demux_video_ctx_clean(demux, ctx);
	return 0;
}

int demux_video_close(struct demux_private_s *demux)
{
	struct demux_ctx_s *del;

	while (demux->ctx != NULL) {
		del = demux->ctx;
		demux->ctx = demux->ctx->next;

		if (del->running) {
			if (demux->glc->flags & GLC_CANCEL)
				ps_buffer_cancel(&del->buffer);
			demux_video_ctx_clean(demux, del);
		}

		free(del);
	}
	return 0;
}

int demux_video_get_ctx(struct demux_private_s *demux, glc_ctx_i ctx_i, struct demux_ctx_s **ctx)
{
	int ret;
	*ctx = demux->ctx;

	while (*ctx != NULL) {
		if ((*ctx)->ctx_i == ctx_i)
			break;
		*ctx = (*ctx)->next;
	}

	if (*ctx == NULL) {
		*ctx = malloc(sizeof(struct demux_ctx_s));
		memset(*ctx, 0, sizeof(struct demux_ctx_s));
		(*ctx)->ctx_i = ctx_i;

		if ((ret = ps_buffer_init(&(*ctx)->buffer, &demux->bufferattr)))
			return ret;
		if ((ret = ps_packet_init(&(*ctx)->packet, &(*ctx)->buffer)))
			return ret;

		sem_init(&(*ctx)->finished, 0, 0);
		if ((ret = gl_play_init(demux->glc, &(*ctx)->buffer, (*ctx)->ctx_i, &(*ctx)->finished)))
			return ret;
		(*ctx)->running = 1;

		(*ctx)->next = demux->ctx;
		demux->ctx = (*ctx);
	}
	return 0;
}

int demux_video_ctx_clean(struct demux_private_s *demux, struct demux_ctx_s *ctx)
{
	int ret;
	ctx->running = 0;

	if ((ret = sem_wait(&ctx->finished)))
		return ret;

	ps_packet_destroy(&ctx->packet);
	ps_buffer_destroy(&ctx->buffer);
	sem_destroy(&ctx->finished);

	return 0;
}

int demux_audio_message(struct demux_private_s *demux, glc_message_header_t *header,
			char *data, size_t size)
{
	struct demux_stream_s *stream;
	glc_audio_i audio_i;
	int ret;

	if (header->type == GLC_MESSAGE_CLOSE) {
		/* broadcast to all */
		stream = demux->stream;
		while (stream != NULL) {
			if (stream->running) {
				if ((ret = demux_audio_stream_send(demux, stream, header, data, size)))
					return ret;
			}
			stream = stream->next;
		}
		return 0;
	} else if (header->type == GLC_MESSAGE_AUDIO_FORMAT)
		audio_i = ((glc_audio_format_message_t *) data)->audio;
	else if (header->type == GLC_MESSAGE_AUDIO)
		audio_i = ((glc_audio_header_t *) data)->audio;
	else
		return EINVAL;

	/* pass to single client */
	if ((ret = demux_audio_get_stream(demux, audio_i, &stream)))
		return ret;

	if ((ret = demux_audio_stream_send(demux, stream, header, data, size)))
		return ret;

	return 0;
}

int demux_audio_close(struct demux_private_s *demux)
{
	struct demux_stream_s *del;

	while (demux->stream != NULL) {
		del = demux->stream;
		demux->stream = demux->stream->next;

		if (del->running) {
			if (demux->glc->flags & GLC_CANCEL)
				ps_buffer_cancel(&del->buffer);
			demux_audio_stream_clean(demux, del);
		}

		free(del);
	}
	return 0;
}

int demux_audio_get_stream(struct demux_private_s *demux, glc_audio_i audio_i,
			   struct demux_stream_s **stream)
{
	int ret;
	*stream = demux->stream;

	while (*stream != NULL) {
		if ((*stream)->audio_i == audio_i)
			break;
		*stream = (*stream)->next;
	}

	if (*stream == NULL) {
		*stream = malloc(sizeof(struct demux_stream_s));
		memset(*stream, 0, sizeof(struct demux_stream_s));
		(*stream)->audio_i = audio_i;

		if ((ret = ps_buffer_init(&(*stream)->buffer, &demux->bufferattr)))
			return ret;
		if ((ret = ps_packet_init(&(*stream)->packet, &(*stream)->buffer)))
			return ret;

		sem_init(&(*stream)->finished, 0, 0);
		if ((ret = audio_play_init(demux->glc, &(*stream)->buffer,
					   (*stream)->audio_i, &(*stream)->finished)))
			return ret;
		(*stream)->running = 1;

		(*stream)->next = demux->stream;
		demux->stream = (*stream);
	}
	return 0;
}

int demux_audio_stream_send(struct demux_private_s *demux, struct demux_stream_s *stream,
			 glc_message_header_t *header, char *data, size_t size)
{
	int ret;
	if ((ret = ps_packet_open(&stream->packet, PS_PACKET_WRITE)))
		goto err;
	if ((ret = ps_packet_write(&stream->packet, header, GLC_MESSAGE_HEADER_SIZE)))
		goto err;
	if ((ret = ps_packet_write(&stream->packet, data, size)))
		goto err;
	if ((ret = ps_packet_close(&stream->packet)))
		goto err;
err:
	if (ret != EINTR)
		return ret;

	demux_audio_stream_clean(demux, stream);
	return 0;
}

int demux_audio_stream_clean(struct demux_private_s *demux, struct demux_stream_s *stream)
{
	int ret;
	stream->running = 0;

	if ((ret = sem_wait(&stream->finished)))
		return ret;

	ps_packet_destroy(&stream->packet);
	ps_buffer_destroy(&stream->buffer);
	sem_destroy(&stream->finished);

	return 0;
}

/**  \} */
/**  \} */
