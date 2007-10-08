/**
 * \file src/play.c
 * \brief stream player
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* play.c -- stream player
 * Copyright (C) 2007 Pyry Haulos
 * For conditions of distribution and use, see copyright notice in glc.h
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <semaphore.h>

#include "common/glc.h"
#include "common/util.h"
#include "stream/file.h"
#include "stream/pack.h"
#include "stream/gl.h"
#include "stream/img.h"
#include "stream/info.h"
#include "stream/wav.h"
#include "stream/audio.h"
#include "stream/demux.h"
#include "stream/ycbcr.h"
#include "stream/yuv4mpeg.h"
#include "stream/rgb.h"

/**
 * \defgroup play stream player
 *  \{
 */
 
int show_info_value(glc_t *glc, const char *value);

int main(int argc, char *argv[])
{
	glc_t *glc;
	ps_stats_t stats;
	ps_bufferattr_t attr;
	const char *summary_val = NULL;
	int play, opt, img, info, show_stats, wav, yuv4mpeg;
	ps_buffer_t *uncompressed, *compressed, *picture, *audio, *ycbcr, *rgb;
	size_t uncompressed_size, compressed_size;

	img = info = wav = 0;
	picture = audio = ycbcr = rgb = NULL;
	play = 1;
	
	img = info = show_stats = yuv4mpeg = 0;
	glc_create(&glc);
	glc->scale = 1;
	glc->flags = 0;
	glc->fps = 0;
	glc->filename_format = NULL;
	compressed_size = 10 * 1024 * 1024;
	uncompressed_size = 10 * 1024 * 1024;
	
	while ((opt = getopt(argc, argv, "i:a:p:y:o:f:c:u:s:th")) != -1) {
		switch (opt) {
		case 'i':
			glc->info_level = atoi(optarg);
			if (glc->info_level < 1)
				goto usage;
			info = 1;
			play = 0;
			break;
		case 'a':
			glc->export_audio = atoi(optarg);
			if (glc->export_audio < 1)
				goto usage;
			wav = 1;
			play = 0;
			break;
		case 'p':
			glc->export_ctx = atoi(optarg);
			if (glc->export_ctx < 1)
				goto usage;
			img = 1;
			play = 0;
			break;
		case 'y':
			glc->export_ctx = atoi(optarg);
			if (glc->export_ctx < 1)
				goto usage;
			yuv4mpeg = 1;
			play = 0;
			break;
		case 'f':
			glc->fps = atoi(optarg);
			if (glc->fps <= 0)
				goto usage;
			break;
		case 'o':
			if (!strcmp(optarg, "-"))
				glc->filename_format = "/dev/stdout";
			else
				glc->filename_format = optarg;
			break;
		case 'c':
			compressed_size = atoi(optarg) * 1024 * 1024;
			if (compressed_size <= 0)
				goto usage;
			break;
		case 'u':
			uncompressed_size = atoi(optarg) * 1024 * 1024;
			if (uncompressed_size <= 0)
				goto usage;
			break;
		case 's':
			summary_val = optarg;
			break;
		case 't':
			show_stats = 1;
			break;
		case 'h':
		default:
			goto usage;
		}
	}
	
	if (optind >= argc)
		goto usage;
	glc->stream_file = argv[optind];

	if ((img | wav | yuv4mpeg) && (glc->filename_format == NULL))
		goto usage;

	if (util_load_info(glc, glc->stream_file))
		return EXIT_FAILURE;

	if (summary_val)
		return show_info_value(glc, summary_val);

	if (!glc->fps)
		glc->fps = glc->info->fps;
	
	ps_bufferattr_init(&attr);
	if (show_stats)
		ps_bufferattr_setflags(&attr, PS_BUFFER_STATS);
		
	ps_bufferattr_setsize(&attr, uncompressed_size);
	uncompressed = malloc(sizeof(ps_buffer_t));
	ps_buffer_init(uncompressed, &attr);

	if (play) {
		picture = malloc(sizeof(ps_buffer_t));
		ps_buffer_init(picture, &attr);
		audio = malloc(sizeof(ps_buffer_t));
		ps_buffer_init(audio, &attr);
		rgb = malloc(sizeof(ps_buffer_t));
		ps_buffer_init(rgb, &attr);
	} else if (yuv4mpeg) {
		ycbcr = malloc(sizeof(ps_buffer_t));
		ps_buffer_init(ycbcr, &attr);
	} else if (img) {
		rgb = malloc(sizeof(ps_buffer_t));
		ps_buffer_init(rgb, &attr);
	}
	
	ps_bufferattr_setsize(&attr, compressed_size);
	compressed = malloc(sizeof(ps_buffer_t));
	ps_buffer_init(compressed, &attr);
	
	ps_bufferattr_destroy(&attr);
	
	util_init(glc);
	
	if (img) {
		rgb_init(glc, uncompressed, rgb);
		img_init(glc, rgb);
	} else if (info)
		info_init(glc, uncompressed);
	else if (wav)
		wav_init(glc, uncompressed);
	else if (yuv4mpeg) {
		ycbcr_init(glc, uncompressed, ycbcr);
		yuv4mpeg_init(glc, ycbcr);
	} else { /* play */
		demux_init(glc, uncompressed, audio, picture);
		rgb_init(glc, picture, rgb);
		audio_playback_init(glc, audio);
		gl_show_init(glc, rgb);
	}
	
	unpack_init(glc, compressed, uncompressed);
	if (file_read(glc, compressed)) {
		ps_buffer_cancel(compressed);
		ps_buffer_cancel(uncompressed);
	}
	
	if (img) {
		sem_wait(&glc->signal[GLC_SIGNAL_IMG_FINISHED]);
		sem_wait(&glc->signal[GLC_SIGNAL_RGB_FINISHED]);
	} else if (info)
		sem_wait(&glc->signal[GLC_SIGNAL_INFO_FINISHED]);
	else if (wav)
		sem_wait(&glc->signal[GLC_SIGNAL_WAV_FINISHED]);
	else if (yuv4mpeg) {
		sem_wait(&glc->signal[GLC_SIGNAL_YCBCR_FINISHED]);
		sem_wait(&glc->signal[GLC_SIGNAL_YUV4MPEG_FINISHED]);
	} else {
		sem_wait(&glc->signal[GLC_SIGNAL_DEMUX_FINISHED]);
		sem_wait(&glc->signal[GLC_SIGNAL_RGB_FINISHED]);
		sem_wait(&glc->signal[GLC_SIGNAL_AUDIO_FINISHED]);
		sem_wait(&glc->signal[GLC_SIGNAL_GL_FINISHED]);
	}
	sem_wait(&glc->signal[GLC_SIGNAL_PACK_FINISHED]);
	
	if (show_stats) {
		if (!ps_buffer_stats(uncompressed, &stats)) {
			printf("uncompressed stream:\n");
			ps_stats_text(&stats, stdout);
		}
		
		if (!ps_buffer_stats(compressed, &stats)) {
			printf("compressed stream:\n");
			ps_stats_text(&stats, stdout);
		}
	}
	
	ps_buffer_destroy(compressed);
	ps_buffer_destroy(uncompressed);
	free(compressed);
	free(uncompressed);
	if (play) {
		ps_buffer_destroy(audio);
		ps_buffer_destroy(picture);
		free(audio);
		free(picture);
		ps_buffer_destroy(rgb);
		free(rgb);
	} else if (yuv4mpeg) {
		ps_buffer_destroy(ycbcr);
		free(ycbcr);
	} else if (img) {
		ps_buffer_destroy(rgb);
		free(rgb);
	}
	util_free_info(glc);
	util_free(glc);
	glc_destroy(glc);
	
	return EXIT_SUCCESS;

usage:
	printf("%s [FILE] [OPTION]...\n", argv[0]);
	printf("  -i LEVEL         show stream information, LEVEL must be greater than 0\n");
	printf("  -a NUM           save audio stream NUM in wav format\n");
	printf("  -p NUM           save pictures as bmp files (use -o pic-%%010d.bmp f.ex.)\n");
	printf("  -y NUM           save video stream NUM in yuv4mpeg format\n");
	printf("  -o FILE          write to FILE\n");
	printf("  -f FPS           save images or video at FPS\n");
	printf("  -c SIZE          compressed stream buffer size in MiB, default is 10\n");
	printf("  -u SIZE          uncompressed stream buffer size in MiB, default is 10\n");
	printf("  -s VAL           show stream summary value, possible values are:\n");
	printf("                       signature, version, flags, fps, pid, name, date\n");
	printf("  -t               show stream statistics\n");
	printf("  -h               show help\n");
	
	return EXIT_FAILURE;
}

int show_info_value(glc_t *glc, const char *value)
{
	if (!strcmp("signature", value))
		printf("0x%08x\n", glc->info->signature);
	else if (!strcmp("version", value))
		printf("0x%02x\n", glc->info->version);
	else if (!strcmp("flags", value))
		printf("%d\n", glc->info->flags);
	else if (!strcmp("fps", value))
		printf("%d\n", glc->info->fps);
	else if (!strcmp("pid", value))
		printf("%d\n", glc->info->pid);
	else if (!strcmp("name", value))
		printf("%s\n", glc->info_name);
	else if (!strcmp("date", value))
		printf("%s\n", glc->info_date);
	else
		return EXIT_FAILURE;
	return EXIT_SUCCESS;
}


/**  \} */
