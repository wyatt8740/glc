/**
 * \file glc/capture/audio_capture.c
 * \brief generic audio capture
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup audio_capture
 *  \{
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <packetstream.h>

#include <glc/common/glc.h>
#include <glc/common/core.h>
#include <glc/common/log.h>
#include <glc/common/state.h>
#include <glc/common/util.h>

#include "audio_capture.h"

#define AUDIO_CAPTURE_CAPTURING     0x1
#define AUDIO_CAPTURE_CFG_CHANGED   0x2

struct audio_capture_s {
	glc_t *glc;
	glc_flags_t flags;
	ps_buffer_t *target;
	ps_packet_t packet;

	glc_flags_t format_flags;
	u_int32_t rate, channels;

	glc_audio_i audio_i;
	glc_state_audio_t state_audio;
};

int audio_capture_write_cfg(audio_capture_t audio_capture);

int audio_capture_init(audio_capture_t *audio_capture, glc_t *glc)
{
	*audio_capture = (audio_capture_t) malloc(sizeof(struct audio_capture_s));
	memset(*audio_capture, 0, sizeof(struct audio_capture_s));

	(*audio_capture)->glc = glc;

	return 0;
}

int audio_capture_destroy(audio_capture_t audio_capture)
{
	if (audio_capture->target)
		ps_packet_destroy(&audio_capture->packet);

	free(audio_capture);
	return 0;
}

int audio_capture_set_buffer(audio_capture_t audio_capture, ps_buffer_t *buffer)
{
	if (audio_capture->target)
		return EAGAIN;

	audio_capture->target = buffer;
	ps_packet_init(&audio_capture->packet, audio_capture->target);

	return 0;
}

int audio_capture_set_format(audio_capture_t audio_capture,
			     glc_flags_t format_flags)
{
	/* check for unsupported flags */
	if (format_flags & (~(GLC_AUDIO_INTERLEAVED |
			      GLC_AUDIO_S16_LE |
			      GLC_AUDIO_S24_LE |
			      GLC_AUDIO_S32_LE)))
		return EINVAL;

	/* FIXME check for invalid combinations */
	if (audio_capture->format_flags != format_flags) {
		audio_capture->format_flags = format_flags;
		audio_capture->flags |= AUDIO_CAPTURE_CFG_CHANGED;
	}
	return 0;
}

int audio_capture_set_rate(audio_capture_t audio_capture,
			   u_int32_t rate)
{
	if (!rate)
		return EINVAL;

	if (audio_capture->rate != rate) {
		audio_capture->rate = rate;
		audio_capture->flags |= AUDIO_CAPTURE_CFG_CHANGED;
	}
	return 0;
}

int audio_capture_set_channels(audio_capture_t audio_capture,
			       u_int32_t channels)
{
	if (!channels)
		return EINVAL;

	if (audio_capture->channels != channels) {
		audio_capture->channels = channels;
		audio_capture->flags |= AUDIO_CAPTURE_CFG_CHANGED;
	}
	return 0;
}

size_t audio_capture_samples_to_bytes(audio_capture_t audio_capture,
				      unsigned int samples)
{
	if (audio_capture->flags & GLC_AUDIO_S16_LE)
		return 2 * samples;
	else if (audio_capture->flags & GLC_AUDIO_S24_LE)
		return 3 * samples;
	else if (audio_capture->flags & GLC_AUDIO_S32_LE)
		return 4 * samples;

	return 0; /* unknown */
}

size_t audio_capture_frames_to_bytes(audio_capture_t audio_capture,
				     unsigned int frames)
{
	return audio_capture_samples_to_bytes(audio_capture,
					      audio_capture->channels * frames);
}

int audio_capture_start(audio_capture_t audio_capture)
{
	if (audio_capture->flags & AUDIO_CAPTURE_CAPTURING)
		return EALREADY;

	/* target buffer must be set */
	if (!audio_capture->target)
		return EINVAL;

	/* and we need valid configuration */
	if ((!audio_capture->format_flags) |
	    (!audio_capture->rate) |
	    (!audio_capture->channels))
		return EINVAL;

	audio_capture->flags |= AUDIO_CAPTURE_CAPTURING;
	return 0;
}

int audio_capture_stop(audio_capture_t audio_capture)
{
	if (!(audio_capture->flags & AUDIO_CAPTURE_CAPTURING))
		return EAGAIN;

	audio_capture->flags &= ~AUDIO_CAPTURE_CAPTURING;
	return 0;
}

int audio_capture_write_cfg(audio_capture_t audio_capture)
{
	glc_message_header_t hdr;
	glc_audio_format_message_t fmt_msg;
	int ret = 0;

	if (!audio_capture->audio_i)
		glc_state_audio_new(audio_capture->glc, &audio_capture->audio_i,
				    &audio_capture->state_audio);

	hdr.type = GLC_MESSAGE_AUDIO_FORMAT;
	fmt_msg.flags = audio_capture->format_flags;
	fmt_msg.audio = audio_capture->audio_i;
	fmt_msg.rate = audio_capture->rate;
	fmt_msg.channels = audio_capture->channels;

	if ((ret = ps_packet_open(&audio_capture->packet, PS_PACKET_WRITE)))
		goto err;
	if ((ret = ps_packet_write(&audio_capture->packet,
				   &hdr, GLC_MESSAGE_HEADER_SIZE)))
		goto err;
	if ((ret = ps_packet_write(&audio_capture->packet,
				   &fmt_msg, GLC_AUDIO_FORMAT_MESSAGE_SIZE)))
		goto err;
	if ((ret = ps_packet_close(&audio_capture->packet)))
		goto err;

	return 0;
err:
	ps_packet_cancel(&audio_capture->packet); /* hopefully we can recover from this */
	glc_log(audio_capture->glc, GLC_ERROR, "audio_capture",
		"can't write audio stream configuration to buffer");
	glc_log(audio_capture->glc, GLC_ERROR, "audio_capture",
		"%s (%d)", strerror(ret), ret);
	return ret;
}

int audio_capture_data(audio_capture_t audio_capture,
		       void *data, size_t size)
{
	glc_message_header_t msg_hdr;
	glc_audio_header_t audio_hdr;

	int ret;
	if (!(audio_capture->flags & AUDIO_CAPTURE_CAPTURING))
		return 0;

	if (audio_capture->flags & AUDIO_CAPTURE_CFG_CHANGED) {
		if ((ret = audio_capture_write_cfg(audio_capture)))
			return ret;
		audio_capture->flags &= ~AUDIO_CAPTURE_CFG_CHANGED;
	}

	msg_hdr.type = GLC_MESSAGE_AUDIO;
	audio_hdr.audio = audio_capture->audio_i; /* should be set to valid one */
	audio_hdr.size = size;
	audio_hdr.timestamp = glc_state_time(audio_capture->glc);

	if ((ret = ps_packet_open(&audio_capture->packet, PS_PACKET_WRITE)))
		goto err;
	if ((ret = ps_packet_write(&audio_capture->packet,
				   &msg_hdr, GLC_MESSAGE_HEADER_SIZE)))
		goto err;
	if ((ret = ps_packet_write(&audio_capture->packet,
				   &audio_hdr, GLC_AUDIO_HEADER_SIZE)))
		goto err;
	if ((ret = ps_packet_write(&audio_capture->packet,
				   data, size)))
		goto err;

	return 0;
err:
	ps_packet_cancel(&audio_capture->packet);
	glc_log(audio_capture->glc, GLC_ERROR, "audio_capture",
		"can't write audio data to buffer");
	glc_log(audio_capture->glc, GLC_ERROR, "audio_capture",
		"%s (%d)", strerror(ret), ret);
	return ret;
}

/**  \} */
