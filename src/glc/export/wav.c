/**
 * \file glc/export/wav.c
 * \brief export audio to wav
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup wav
 *  \{
 */

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

#include "wav.h"

struct wav_hdr {
	u_int32_t id;
	u_int32_t size;
	u_int32_t riff;
} __attribute__((packed));

struct wav_fmt {
	u_int32_t id;
	u_int32_t size;
	u_int16_t compression;
	u_int16_t channels;
	u_int32_t rate;
	u_int32_t bps;
	u_int16_t align;
	u_int16_t bits_per_sample;
} __attribute__((packed));

struct wav_data {
	u_int32_t id;
	u_int32_t size;
} __attribute__((packed));

struct wav_s {
	glc_t *glc;
	glc_thread_t thread;
	int running;

	glc_audio_i audio_i;
	int interpolate;

	unsigned int file_count;
	const char *filename_format;

	char *silence;
	size_t silence_size;
	glc_utime_t silence_threshold;

	FILE *to;
	
	glc_utime_t time;
	unsigned int rate, channels, interleaved;
	size_t bps;
	size_t sample_size;

	struct audio_stream_s *stream;
};

int wav_read_callback(glc_thread_state_t *state);
void wav_finish_callback(void *priv, int err);

int wav_write_hdr(wav_t wav, glc_audio_format_message_t *fmt_msg);
int wav_write_audio(wav_t wav, glc_audio_header_t *audio_msg, char *data);

int wav_init(wav_t *wav, glc_t *glc)
{
	*wav = (wav_t) malloc(sizeof(struct wav_s));
	memset(*wav, 0, sizeof(struct wav_s));

	(*wav)->glc = glc;

	(*wav)->filename_format = "audio%02d.wav";
	(*wav)->silence_threshold = 200000; /* 0.2s */
	(*wav)->audio_i = 1;
	(*wav)->interpolate = 1;

	(*wav)->silence_size = 1024;
	(*wav)->silence = (char *) malloc((*wav)->silence_size);
	memset((*wav)->silence, 0, (*wav)->silence_size);

	(*wav)->thread.flags = GLC_THREAD_READ;
	(*wav)->thread.ptr = *wav;
	(*wav)->thread.read_callback = &wav_read_callback;
	(*wav)->thread.finish_callback = &wav_finish_callback;
	(*wav)->thread.threads = 1;

	return 0;
}

int wav_destroy(wav_t wav)
{
	free(wav->silence);
	free(wav);
	return 0;
}

int wav_process_start(wav_t wav, ps_buffer_t *from)
{
	int ret;
	if (wav->running)
		return EAGAIN;

	if ((ret = glc_thread_create(wav->glc, &wav->thread, from, NULL)))
		return ret;
	wav->running = 1;

	return 0;
}

int wav_process_wait(wav_t wav)
{
	if (!wav->running)
		return EAGAIN;

	glc_thread_wait(&wav->thread);
	wav->running = 0;

	return 0;
}

int wav_set_interpolation(wav_t wav, int interpolate)
{
	wav->interpolate = interpolate;
	return 0;
}

int wav_set_filename(wav_t wav, const char *filename)
{
	wav->filename_format = filename;
	return 0;
}

int wav_set_stream_number(wav_t wav, glc_audio_i audio)
{
	wav->audio_i = audio;
	return 0;
}

int wav_set_silence_threshold(wav_t wav, glc_utime_t silence_threshold)
{
	wav->silence_threshold = silence_threshold;
	return 0;
}

void wav_finish_callback(void *priv, int err)
{
	wav_t wav = (wav_t ) priv;

	if (err)
		glc_log(wav->glc, GLC_ERROR, "wav", "%s (%d)", strerror(err), err);

	if (wav->to) {
		fclose(wav->to);
		wav->to = NULL;
	}

	wav->file_count = 0;
}

int wav_read_callback(glc_thread_state_t *state)
{
	wav_t wav = (wav_t ) state->ptr;

	if (state->header.type == GLC_MESSAGE_AUDIO_FORMAT)
		return wav_write_hdr(wav, (glc_audio_format_message_t *) state->read_data);
	else if (state->header.type == GLC_MESSAGE_AUDIO)
		return wav_write_audio(wav, (glc_audio_header_t *) state->read_data, &state->read_data[GLC_AUDIO_HEADER_SIZE]);
	
	return 0;
}

int wav_write_hdr(wav_t wav, glc_audio_format_message_t *fmt_msg)
{
	int sample_size;
	char *filename;

	if (fmt_msg->audio != wav->audio_i)
		return 0;
	
	if (fmt_msg->flags & GLC_AUDIO_S16_LE)
		sample_size = 2;
	else if (fmt_msg->flags & GLC_AUDIO_S24_LE)
		sample_size = 3;
	else if (fmt_msg->flags & GLC_AUDIO_S32_LE)
		sample_size = 4;
	else {
		glc_log(wav->glc, GLC_ERROR, "wav",
			 "unsupported format 0x%02x (stream %d)", fmt_msg->flags, fmt_msg->audio);
		return ENOTSUP;
	}

	if (wav->to) {
		glc_log(wav->glc, GLC_ERROR, "wav",
			 "configuration update msg to stream %d", fmt_msg->audio);
		fclose(wav->to);
	}
	
	filename = (char *) malloc(1024);
	snprintf(filename, 1023, wav->filename_format, ++wav->file_count);
	glc_log(wav->glc, GLC_INFORMATION, "wav", "opening %s for writing", filename);
	wav->to = fopen(filename, "w");
	if (!wav->to) {
		glc_log(wav->glc, GLC_ERROR, "wav", "can't open %s", filename);
		free(filename);
		return EINVAL;
	}
	free(filename);
	
	struct wav_hdr hdr = {0x46464952, 0xffffffff, 0x45564157};
	struct wav_fmt fmt = {0x20746D66, /* id */
			      16, /* chunk size */
			      1, /* compression */
			      fmt_msg->channels, /* channels */
			      fmt_msg->rate, /* rate */
			      fmt_msg->rate * sample_size * fmt_msg->channels, /* bps */
			      sample_size * 2, /* block align */
			      sample_size * 8 /* bits per sample */ };
	struct wav_data data = {0x61746164, 0xffffffff};

	fwrite(&hdr, 1, sizeof(struct wav_hdr), wav->to);
	fwrite(&fmt, 1, sizeof(struct wav_fmt), wav->to);
	fwrite(&data, 1, sizeof(struct wav_data), wav->to);

	wav->sample_size = sample_size;
	wav->bps = fmt.bps;
	wav->rate = fmt_msg->rate;
	wav->channels = fmt_msg->channels;
	if (fmt_msg->flags & GLC_AUDIO_INTERLEAVED)
		wav->interleaved = 1;
	else
		wav->interleaved = 0;
	return 0;
}

int wav_write_audio(wav_t wav, glc_audio_header_t *audio_hdr, char *data)
{
	size_t need_silence, write_silence;
	unsigned int c;
	size_t samples, s;

	if (audio_hdr->audio != wav->audio_i)
		return 0;
	
	glc_utime_t duration = (audio_hdr->size * 1000000) / wav->bps;

	if (!wav->to) {
		glc_log(wav->glc, GLC_ERROR, "wav", "broken stream %d", audio_hdr->audio);
		return EINVAL;
	}

	wav->time += duration;

	if (wav->time + wav->silence_threshold < audio_hdr->timestamp) {
		need_silence = ((audio_hdr->timestamp - wav->time) * wav->bps) / 1000000;
		need_silence -= need_silence % (wav->sample_size * wav->channels);

		wav->time += (need_silence * 1000000) / wav->bps;
		if (wav->interpolate) {
			glc_log(wav->glc, GLC_WARNING, "wav", "writing %zd bytes of silence", need_silence);
			while (need_silence > 0) {
				write_silence = need_silence > wav->silence_size ? wav->silence_size : need_silence;
				fwrite(wav->silence, 1, write_silence, wav->to);
				need_silence -= write_silence;
			}
		}
	}

	if (wav->interleaved)
		fwrite(data, 1, audio_hdr->size, wav->to);
	else {
		samples = audio_hdr->size / (wav->sample_size * wav->channels);
		for (s = 0; s < samples; s++) {
			for (c = 0; c < wav->channels; c++)
				fwrite(&data[samples * wav->sample_size * c + wav->sample_size * s], 1, wav->sample_size, wav->to);
		}
	}

	return 0;
}


/**  \} */
