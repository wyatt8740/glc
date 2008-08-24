/**
 * \file glc/core/tracker.c
 * \brief glc state tracker
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup tracker
 *  \{
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <glc/common/glc.h>
#include <glc/common/log.h>

#include "tracker.h"

#define TRACKER_VIDEO_FORMAT      0x01
#define TRACKER_VIDEO_COLOR       0x02

struct tracker_video_s {
	glc_stream_id_t id;
	glc_flags_t flags;
	glc_video_format_message_t format;
	glc_color_message_t color;
	struct tracker_video_s *next;
};

#define TRACKER_AUDIO_FORMAT      0x01

struct tracker_audio_s {
	glc_stream_id_t id;
	glc_flags_t flags;
	glc_audio_format_message_t format;
	struct tracker_audio_s *next;
};

struct tracker_s {
	glc_t *glc;

	struct tracker_video_s *video_streams;
	struct tracker_audio_s *audio_streams;
};

struct tracker_video_s *tracker_get_video_stream(tracker_t tracker, glc_stream_id_t id);
struct tracker_audio_s *tracker_get_audio_stream(tracker_t tracker, glc_stream_id_t id);

int tracker_init(tracker_t *tracker, glc_t *glc)
{
	*tracker = (tracker_t) malloc(sizeof(struct tracker_s));
	memset(*tracker, 0, sizeof(struct tracker_s));

	return 0;
}

int tracker_destroy(tracker_t tracker)
{
	struct tracker_video_s *video_del;
	struct tracker_audio_s *audio_del;

	while (tracker->video_streams != NULL) {
		video_del = tracker->video_streams;
		tracker->video_streams = tracker->video_streams->next;
		free(video_del);
	}

	while (tracker->audio_streams != NULL) {
		audio_del = tracker->audio_streams;
		tracker->audio_streams = tracker->audio_streams->next;
		free(audio_del);
	}

	free(tracker);
	return 0;
}

struct tracker_video_s *tracker_get_video_stream(tracker_t tracker, glc_stream_id_t id)
{
	struct tracker_video_s *video = tracker->video_streams;

	while (video != NULL) {
		if (video->id == id)
			break;
		video = video->next;
	}

	if (video == NULL) {
		video = malloc(sizeof(struct tracker_video_s));
		memset(video, 0, sizeof(struct tracker_video_s));

		video->next = tracker->video_streams;
		tracker->video_streams = video;
	}

	return video;
}

struct tracker_audio_s *tracker_get_audio_stream(tracker_t tracker, glc_stream_id_t id)
{
	struct tracker_audio_s *audio = tracker->audio_streams;

	while (audio != NULL) {
		if (audio->id == id)
			break;
		audio = audio->next;
	}

	if (audio == NULL) {
		audio = malloc(sizeof(struct tracker_audio_s));
		memset(audio, 0, sizeof(struct tracker_audio_s));

		audio->next = tracker->audio_streams;
		tracker->audio_streams = audio;
	}

	return audio;
}


int tracker_submit(tracker_t tracker, glc_message_header_t *header,
		   void *message, size_t message_size)
{
	struct tracker_video_s *video;
	struct tracker_audio_s *audio;

	if (header->type == GLC_MESSAGE_VIDEO_FORMAT) {
		video = tracker_get_video_stream(tracker, ((glc_video_format_message_t *) message)->id);
		memcpy(&video->format, message, sizeof(glc_video_format_message_t));
		video->flags |= TRACKER_VIDEO_FORMAT;
	} else if (header->type == GLC_MESSAGE_AUDIO_FORMAT) {
		audio = tracker_get_audio_stream(tracker, ((glc_audio_format_message_t *) message)->id);
		memcpy(&audio->format, message, sizeof(glc_audio_format_message_t));
		audio->flags |= TRACKER_AUDIO_FORMAT;
	} else if (header->type == GLC_MESSAGE_COLOR) {
		video = tracker_get_video_stream(tracker, ((glc_color_message_t *) message)->id);
		memcpy(&video->color, message, sizeof(glc_color_message_t));
		video->flags |= TRACKER_VIDEO_COLOR;
	}

	return 0;
}

int tracker_iterate_state(tracker_t tracker, tracker_callback_t callback,
			  void *arg)
{
	int ret = 0;
	struct tracker_video_s *video = tracker->video_streams;
	struct tracker_audio_s *audio = tracker->audio_streams;
	glc_message_header_t header;

	while (video != NULL) {
		if (video->flags & TRACKER_VIDEO_FORMAT) {
			header.type = GLC_MESSAGE_VIDEO_FORMAT;
			if ((ret = callback(&header, &video->format, sizeof(glc_video_format_message_t), arg)))
				goto finish;
		}

		if (video->flags & TRACKER_VIDEO_COLOR) {
			header.type = GLC_MESSAGE_COLOR;
			if ((ret = callback(&header, &video->color, sizeof(glc_color_message_t), arg)))
				goto finish;
		}

		video = video->next;
	}

	while (audio != NULL) {
		if (audio->flags & TRACKER_AUDIO_FORMAT) {
			header.type = GLC_MESSAGE_AUDIO_FORMAT;
			if ((ret = callback(&header, &audio->format, sizeof(glc_audio_format_message_t), arg)))
				goto finish;
		}

		audio = audio->next;
	}

finish:
	return ret;
}
