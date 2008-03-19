/**
 * \file glc/play/demux.c
 * \brief audio/picture stream demuxer
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup demux
 *  \{
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <packetstream.h>
#include <errno.h>

#include <glc/common/glc.h>
#include <glc/common/core.h>
#include <glc/common/log.h>
#include <glc/common/state.h>
#include <glc/common/thread.h>
#include <glc/common/util.h>

#include "demux.h"
#include "gl_play.h"
#include "alsa_play.h"

struct demux_video_stream_s {
	glc_stream_id_t id;
	ps_buffer_t buffer;
	ps_packet_t packet;

	int running;
	gl_play_t gl_play;

	struct demux_video_stream_s *next;
};

struct demux_audio_stream_s {
	glc_stream_id_t id;
	ps_buffer_t buffer;
	ps_packet_t packet;

	int running;
	alsa_play_t alsa_play;

	struct demux_audio_stream_s *next;
};

struct demux_s {
	glc_t *glc;
	ps_buffer_t *from;

	pthread_t thread;
	int running;

	const char *alsa_playback_device;

	ps_bufferattr_t video_bufferattr;
	ps_bufferattr_t audio_bufferattr;

	struct demux_video_stream_s *video;
	struct demux_audio_stream_s *audio;
};

int demux_close(demux_t demux);
void *demux_thread(void *argptr);

int demux_video_stream_message(demux_t demux, glc_message_header_t *header,
			       char *data, size_t size);
int demux_video_stream_get(demux_t demux, glc_stream_id_t id,
			   struct demux_video_stream_s **video);
int demux_video_stream_send(demux_t demux, struct demux_video_stream_s *video,
			    glc_message_header_t *header, char *data, size_t size);
int demux_video_stream_close(demux_t demux);
int demux_video_stream_clean(demux_t demux, struct demux_video_stream_s *video);

int demux_audio_stream_message(demux_t demux, glc_message_header_t *header,
			       char *data, size_t size);
int demux_audio_stream_get(demux_t demux, glc_stream_id_t id,
			   struct demux_audio_stream_s **audio);
int demux_audio_stream_send(demux_t demux, struct demux_audio_stream_s *audio,
			 glc_message_header_t *header, char *data, size_t size);
int demux_audio_stream_close(demux_t demux);
int demux_audio_stream_clean(demux_t demux, struct demux_audio_stream_s *audio);

int demux_init(demux_t *demux, glc_t *glc)
{
	*demux = malloc(sizeof(struct demux_s));
	memset(*demux, 0, sizeof(struct demux_s));

	(*demux)->glc = glc;
	(*demux)->alsa_playback_device = "default";

	ps_bufferattr_init(&(*demux)->video_bufferattr);
	ps_bufferattr_init(&(*demux)->audio_bufferattr);

	ps_bufferattr_setsize(&(*demux)->video_bufferattr, 1024 * 1024 * 10);
	ps_bufferattr_setsize(&(*demux)->audio_bufferattr, 1024 * 1024 * 1);

	return 0;
}

int demux_destroy(demux_t demux)
{
	ps_bufferattr_destroy(&demux->video_bufferattr);
	ps_bufferattr_destroy(&demux->audio_bufferattr);
	free(demux);

	return 0;
}

int demux_set_video_buffer_size(demux_t demux, size_t size)
{
	return ps_bufferattr_setsize(&demux->video_bufferattr, size);
}

int demux_set_audio_buffer_size(demux_t demux, size_t size)
{
	return ps_bufferattr_setsize(&demux->audio_bufferattr, size);
}

int demux_set_alsa_playback_device(demux_t demux, const char *device)
{
	demux->alsa_playback_device = device;
	return 0;
}

int demux_process_start(demux_t demux, ps_buffer_t *from)
{
	int ret;
	pthread_attr_t attr;
	if (demux->running)
		return EAGAIN;

	demux->from = from;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	if ((ret = pthread_create(&demux->thread, &attr, demux_thread, demux)))
		return ret;
	demux->running = 1;

	pthread_attr_destroy(&attr);
	return 0;
}

int demux_process_wait(demux_t demux)
{
	if (!demux->running)
		return EAGAIN;

	pthread_join(demux->thread, NULL);
	demux->running = 0;

	return 0;
}

int demux_close(demux_t demux)
{
	int ret = 0;

	if (demux->video != NULL) {
		if ((ret = demux_video_stream_close(demux)))
			glc_log(demux->glc, GLC_ERROR, "demux", "can't close video streams: %s (%d)",
				 strerror(ret), ret);
	}

	if (demux->audio != NULL) {
		if ((ret = demux_audio_stream_close(demux)))
			glc_log(demux->glc, GLC_ERROR, "demux", "can't close audio streams: %s (%d)",
				 strerror(ret), ret);
	}

	return 0;
}

void *demux_thread(void *argptr)
{
	demux_t demux = (demux_t ) argptr;
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

		if ((ret = ps_packet_read(&read, &msg_hdr, sizeof(glc_message_header_t))))
			goto err;
		if ((ret = ps_packet_getsize(&read, &data_size)))
			goto err;
		data_size -= sizeof(glc_message_header_t);
		if ((ret = ps_packet_dma(&read, (void *) &data, data_size, PS_ACCEPT_FAKE_DMA)))
			goto err;

		if ((msg_hdr.type == GLC_MESSAGE_CLOSE) |
		    (msg_hdr.type == GLC_MESSAGE_VIDEO_DATA) |
		    (msg_hdr.type == GLC_MESSAGE_VIDEO_FORMAT)) {
			/* handle msg to gl_play */
			demux_video_stream_message(demux, &msg_hdr, data, data_size);
		}

		if ((msg_hdr.type == GLC_MESSAGE_CLOSE) |
		    (msg_hdr.type == GLC_MESSAGE_AUDIO_FORMAT) |
		    (msg_hdr.type == GLC_MESSAGE_AUDIO_DATA)) {
			/* handle msg to alsa_play */
			demux_audio_stream_message(demux, &msg_hdr, data, data_size);
		}

		ps_packet_close(&read);
	} while ((!glc_state_test(demux->glc, GLC_STATE_CANCEL)) &&
		 (msg_hdr.type != GLC_MESSAGE_CLOSE));

finish:
	ps_packet_destroy(&read);

	if (glc_state_test(demux->glc, GLC_STATE_CANCEL))
		ps_buffer_cancel(demux->from);

	demux_video_stream_close(demux);
	demux_audio_stream_close(demux);

	demux_close(demux);
	return NULL;
err:
	if (ret == EINTR) /* just _cancel() */
		ret = 0;
	else
		glc_log(demux->glc, GLC_ERROR, "demux", "%s (%d)", strerror(ret), ret);
	goto finish;
}

int demux_video_stream_message(demux_t demux, glc_message_header_t *header,
			char *data, size_t size)
{
	struct demux_video_stream_s *video;
	glc_stream_id_t id;
	int ret;

	if (header->type == GLC_MESSAGE_CLOSE) {
		/* broadcast to all */
		video = demux->video;
		while (video != NULL) {
			if (video->running) {
				if ((ret = demux_video_stream_send(demux, video, header, data, size)))
					return ret;
			}
			video = video->next;
		}
		return 0;
	} else if (header->type == GLC_MESSAGE_VIDEO_FORMAT)
		id = ((glc_video_format_message_t *) data)->id;
	else if (header->type == GLC_MESSAGE_VIDEO_DATA)
		id = ((glc_video_data_header_t *) data)->id;
	else
		return EINVAL;

	/* pass to single client */
	if ((ret = demux_video_stream_get(demux, id, &video)))
		return ret;

	if ((ret = demux_video_stream_send(demux, video, header, data, size)))
		return ret;

	return 0;
}

int demux_video_stream_send(demux_t demux, struct demux_video_stream_s *video,
			 glc_message_header_t *header, char *data, size_t size)
{
	int ret;
	if ((ret = ps_packet_open(&video->packet, PS_PACKET_WRITE)))
		goto err;
	if ((ret = ps_packet_write(&video->packet, header, sizeof(glc_message_header_t))))
		goto err;
	if ((ret = ps_packet_write(&video->packet, data, size)))
		goto err;
	if ((ret = ps_packet_close(&video->packet)))
		goto err;
err:
	if (ret != EINTR)
		return ret;

	/* since it is EINTR, _cancel() is already done */
	glc_log(demux->glc, GLC_DEBUG, "demux", "video stream %d has quit", video->id);
	demux_video_stream_clean(demux, video);
	return 0;
}

int demux_video_stream_close(demux_t demux)
{
	struct demux_video_stream_s *del;

	while (demux->video != NULL) {
		del = demux->video;
		demux->video = demux->video->next;

		if (del->running) {
			ps_buffer_cancel(&del->buffer);
			demux_video_stream_clean(demux, del);
		}

		free(del);
	}
	return 0;
}

int demux_video_stream_get(demux_t demux, glc_stream_id_t id, struct demux_video_stream_s **video)
{
	int ret;
	*video = demux->video;

	while (*video != NULL) {
		if ((*video)->id == id)
			break;
		*video = (*video)->next;
	}

	if (*video == NULL) {
		*video = malloc(sizeof(struct demux_video_stream_s));
		memset(*video, 0, sizeof(struct demux_video_stream_s));
		(*video)->id = id;

		if ((ret = ps_buffer_init(&(*video)->buffer, &demux->video_bufferattr)))
			return ret;
		if ((ret = ps_packet_init(&(*video)->packet, &(*video)->buffer)))
			return ret;

		if ((ret = gl_play_init(&(*video)->gl_play, demux->glc)))
			return ret;
		if ((ret = gl_play_set_stream_id((*video)->gl_play, (*video)->id)))
			return ret;
		if ((ret = gl_play_process_start((*video)->gl_play, &(*video)->buffer)))
			return ret;
		(*video)->running = 1;

		(*video)->next = demux->video;
		demux->video = (*video);
	}
	return 0;
}

int demux_video_stream_clean(demux_t demux, struct demux_video_stream_s *video)
{
	int ret;
	video->running = 0;

	if ((ret = gl_play_process_wait(video->gl_play)))
		return ret;
	gl_play_destroy(video->gl_play);

	ps_packet_destroy(&video->packet);
	ps_buffer_destroy(&video->buffer);

	return 0;
}

int demux_audio_stream_message(demux_t demux, glc_message_header_t *header,
			char *data, size_t size)
{
	struct demux_audio_stream_s *audio;
	glc_stream_id_t id;
	int ret;

	if (header->type == GLC_MESSAGE_CLOSE) {
		/* broadcast to all */
		audio = demux->audio;
		while (audio != NULL) {
			if (audio->running) {
				if ((ret = demux_audio_stream_send(demux, audio, header, data, size)))
					return ret;
			}
			audio = audio->next;
		}
		return 0;
	} else if (header->type == GLC_MESSAGE_AUDIO_FORMAT)
		id = ((glc_audio_format_message_t *) data)->id;
	else if (header->type == GLC_MESSAGE_AUDIO_DATA)
		id = ((glc_audio_data_header_t *) data)->id;
	else
		return EINVAL;

	/* pass to single client */
	if ((ret = demux_audio_stream_get(demux, id, &audio)))
		return ret;

	if ((ret = demux_audio_stream_send(demux, audio, header, data, size)))
		return ret;

	return 0;
}

int demux_audio_stream_close(demux_t demux)
{
	struct demux_audio_stream_s *del;

	while (demux->audio != NULL) {
		del = demux->audio;
		demux->audio = demux->audio->next;

		if (del->running) {
			ps_buffer_cancel(&del->buffer);
			demux_audio_stream_clean(demux, del);
		}

		free(del);
	}
	return 0;
}

int demux_audio_stream_get(demux_t demux, glc_stream_id_t id,
			   struct demux_audio_stream_s **audio)
{
	int ret;
	*audio = demux->audio;

	while (*audio != NULL) {
		if ((*audio)->id == id)
			break;
		*audio = (*audio)->next;
	}

	if (*audio == NULL) {
		*audio = malloc(sizeof(struct demux_audio_stream_s));
		memset(*audio, 0, sizeof(struct demux_audio_stream_s));
		(*audio)->id = id;

		if ((ret = ps_buffer_init(&(*audio)->buffer, &demux->audio_bufferattr)))
			return ret;
		if ((ret = ps_packet_init(&(*audio)->packet, &(*audio)->buffer)))
			return ret;

		if ((ret = alsa_play_init(&(*audio)->alsa_play, demux->glc)))
			return ret;
		if ((ret = alsa_play_set_stream_id((*audio)->alsa_play,
							(*audio)->id)))
			return ret;
		if ((ret = alsa_play_set_alsa_playback_device((*audio)->alsa_play,
							       demux->alsa_playback_device)))
			return ret;
		if ((ret = alsa_play_process_start((*audio)->alsa_play,
						    &(*audio)->buffer)))
			return ret;
		(*audio)->running = 1;

		(*audio)->next = demux->audio;
		demux->audio = (*audio);
	}
	return 0;
}

int demux_audio_stream_send(demux_t demux, struct demux_audio_stream_s *audio,
			 glc_message_header_t *header, char *data, size_t size)
{
	int ret;
	if ((ret = ps_packet_open(&audio->packet, PS_PACKET_WRITE)))
		goto err;
	if ((ret = ps_packet_write(&audio->packet, header, sizeof(glc_message_header_t))))
		goto err;
	if ((ret = ps_packet_write(&audio->packet, data, size)))
		goto err;
	if ((ret = ps_packet_close(&audio->packet)))
		goto err;
err:
	if (ret != EINTR)
		return ret;

	glc_log(demux->glc, GLC_DEBUG, "demux", "audio stream %d has quit", audio->id);
	demux_audio_stream_clean(demux, audio);
	return 0;
}

int demux_audio_stream_clean(demux_t demux, struct demux_audio_stream_s *audio)
{
	int ret;
	audio->running = 0;

	if ((ret = alsa_play_process_wait(audio->alsa_play)))
		return ret;
	alsa_play_destroy(audio->alsa_play);

	ps_packet_destroy(&audio->packet);
	ps_buffer_destroy(&audio->buffer);

	return 0;
}

/**  \} */
