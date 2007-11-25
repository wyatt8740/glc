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

#include <stdlib.h>
#include <unistd.h>
#include <semaphore.h>
#include <getopt.h>
#include <string.h>

#include "common/glc.h"
#include "common/util.h"

#include "core/file.h"
#include "core/pack.h"
#include "core/rgb.h"
#include "core/color.h"
#include "core/info.h"
#include "core/ycbcr.h"
#include "core/scale.h"

#include "export/img.h"
#include "export/wav.h"
#include "export/yuv4mpeg.h"

#include "play/demux.h"

int show_info_value(glc_t *glc, const char *value);

int main(int argc, char *argv[])
{
	glc_t *glc;
	ps_stats_t stats;
	ps_bufferattr_t attr;
	const char *summary_val = NULL;
	int play, opt, option_index, img, info, show_stats, wav, yuv4mpeg;
	ps_buffer_t *uncompressed, *compressed, *ycbcr, *rgb, *color, *scale;

	struct option long_options[] = {
		{"info",		1, NULL, 'i'},
		{"wav",			1, NULL, 'a'},
		{"bmp",			1, NULL, 'p'},
		{"yuv4mpeg",		1, NULL, 'y'},
		{"out",			1, NULL, 'o'},
		{"fps",			1, NULL, 'f'},
		{"resize",		1, NULL, 'r'},
		{"adjust",		1, NULL, 'g'},
		{"silence",		1, NULL, 'l'},
		{"compressed",		1, NULL, 'c'},
		{"uncompressed",	1, NULL, 'u'},
		{"show",		1, NULL, 's'},
		{"verbosity",		1, NULL, 'v'},
		{"statistics",		0, NULL, 't'},
		{"help",		0, NULL, 'h'},
		{0, 0, 0, 0}
	};
	option_index = 0;

	img = info = wav = 0;
	ycbcr = rgb = color = scale = NULL;
	play = 1;

	img = info = show_stats = yuv4mpeg = 0;
	glc_create(&glc);
	glc->scale = 1;
	glc->scale_width = glc->scale_height = 0;
	glc->flags = 0;
	glc->fps = 0;
	glc->filename_format = NULL;
	glc->silence_threshold = 200000;
	glc->compressed_size = 10 * 1024 * 1024;
	glc->uncompressed_size = 10 * 1024 * 1024;
	glc->log_file = "/dev/stderr";
	glc->log_level = 0;

	glc->brightness = glc->contrast = 0;
	glc->red_gamma = 1.0;
	glc->green_gamma = 1.0;
	glc->blue_gamma = 1.0;

	while ((opt = getopt_long(argc, argv, "i:a:p:y:o:f:r:g:l:c:u:s:v:th",
				  long_options, &optind)) != -1) {
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
			glc->fps = atof(optarg);
			if (glc->fps <= 0)
				goto usage;
			break;
		case 'r':
			if (strstr(optarg, "x")) {
				sscanf(optarg, "%ux%u", &glc->scale_width, &glc->scale_height);
				if ((!glc->scale_width) | (!glc->scale_height))
					goto usage;
				glc->flags |= GLC_SCALE_SIZE;
			} else {
				glc->scale = atof(optarg);
				if (glc->scale <= 0)
					goto usage;
			}
			break;
		case 'g':
			glc->flags |= GLC_OVERRIDE_COLOR_CORRECTION;
			sscanf(optarg, "%f;%f;%f;%f;%f", &glc->brightness, &glc->contrast,
			       &glc->red_gamma, &glc->green_gamma, &glc->blue_gamma);
			break;
		case 'l':
			/* glc_utime_t so always positive */
			glc->silence_threshold = atof(optarg) * 1000000;
			break;
		case 'o':
			if (!strcmp(optarg, "-")) /* TODO fopen(1) */
				glc->filename_format = "/dev/stdout";
			else
				glc->filename_format = optarg;
			break;
		case 'c':
			glc->compressed_size = atoi(optarg) * 1024 * 1024;
			if (glc->compressed_size <= 0)
				goto usage;
			break;
		case 'u':
			glc->uncompressed_size = atoi(optarg) * 1024 * 1024;
			if (glc->uncompressed_size <= 0)
				goto usage;
			break;
		case 's':
			summary_val = optarg;
			break;
		case 'v':
			glc->log_level = atoi(optarg);
			if (glc->log_level < 0)
				goto usage;
			glc->flags |= GLC_LOG | GLC_NOERR;
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

	util_init(glc);
	if (glc->flags & GLC_LOG)
		util_log_init(glc);

	if ((img | wav | yuv4mpeg) && (glc->filename_format == NULL))
		goto usage;

	if (util_load_info(glc, glc->stream_file))
		return EXIT_FAILURE;

	if (summary_val)
		return show_info_value(glc, summary_val);

	if (glc->fps == 0)
		glc->fps = glc->info->fps;

	ps_bufferattr_init(&attr);
	if (show_stats)
		ps_bufferattr_setflags(&attr, PS_BUFFER_STATS);

	ps_bufferattr_setsize(&attr, glc->uncompressed_size);

	uncompressed = malloc(sizeof(ps_buffer_t));
	ps_buffer_init(uncompressed, &attr);

	if ((yuv4mpeg) | (img) | (play)) {
		color = malloc(sizeof(ps_buffer_t));
		ps_buffer_init(color, &attr);

		scale = malloc(sizeof(ps_buffer_t));
		ps_buffer_init(scale, &attr);
	}

	if (yuv4mpeg) {
		ycbcr = malloc(sizeof(ps_buffer_t));
		ps_buffer_init(ycbcr, &attr);
	} else if ((img) | (play)) {
		rgb = malloc(sizeof(ps_buffer_t));
		ps_buffer_init(rgb, &attr);
	}

	ps_bufferattr_setsize(&attr, glc->compressed_size);
	compressed = malloc(sizeof(ps_buffer_t));
	ps_buffer_init(compressed, &attr);

	ps_bufferattr_destroy(&attr);

	if (img) {
		rgb_init(glc, uncompressed, rgb);
		color_init(glc, rgb, color);
		scale_init(glc, color, scale);
		img_init(glc, color);
	} else if (info)
		info_init(glc, uncompressed);
	else if (wav)
		wav_init(glc, uncompressed);
	else if (yuv4mpeg) {
		color_init(glc, uncompressed, color);
		scale_init(glc, color, scale);
		ycbcr_init(glc, scale, ycbcr);
		yuv4mpeg_init(glc, ycbcr);
	} else { /* play */
		rgb_init(glc, uncompressed, rgb);
		color_init(glc, rgb, color);
		scale_init(glc, color, scale);
		demux_init(glc, scale);
	}

	unpack_init(glc, compressed, uncompressed);
	if (file_read(glc, compressed)) {
		ps_buffer_cancel(compressed);
		ps_buffer_cancel(uncompressed);
	}

	if ((yuv4mpeg) | (img) | (play)) {
		sem_wait(&glc->signal[GLC_SIGNAL_COLOR_FINISHED]);
		sem_wait(&glc->signal[GLC_SIGNAL_SCALE_FINISHED]);
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
		sem_wait(&glc->signal[GLC_SIGNAL_RGB_FINISHED]);
		sem_wait(&glc->signal[GLC_SIGNAL_DEMUX_FINISHED]);
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
	if ((play) | (img) | (yuv4mpeg)) {
		ps_buffer_destroy(color);
		free(color);

		ps_buffer_destroy(scale);
		free(scale);
	}
	if (play) {
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
	printf("%s [file] [option]...\n", argv[0]);
	printf("  -i, --info=LEVEL         show stream information, LEVEL must be\n"
	       "                             greater than 0\n"
	       "  -a, --wav=NUM            save audio stream NUM in wav format\n"
	       "  -p, --bmp=NUM            save pictures as bmp files\n"
	       "                             (use -o pic-%%010d.bmp f.ex.)\n"
	       "  -y, --yuv4mpeg=NUM       save video stream NUM in yuv4mpeg format\n"
	       "  -o, --out=FILE           write to FILE\n"
	       "  -f, --fps=FPS            save images or video at FPS\n"
	       "  -r, --resize=VAL         resize pictures with scale factor VAL or WxH\n"
	       "  -g, --color=ADJUST       adjust colors\n"
	       "                             format is brightness;contrast;red;green;blue\n"
	       "  -l, --silence=SECONDS    audio silence threshold in seconds\n"
	       "                             default threshold is 0.2\n"
	       "  -c, --compressed=SIZE    compressed stream buffer size in MiB, default is 10\n"
	       "  -u, --uncompressed=SIZE  uncompressed stream buffer size in MiB, default is 10\n"
	       "  -s, --show=VAL           show stream summary value, possible values are:\n"
	       "                             signature, version, flags, fps, pid, name, date\n"
	       "  -v, --verbosity=LEVEL    verbosity level\n"
	       "  -t, --statistics         show stream statistics\n"
	       "  -h, --help               show help\n");

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
		printf("%f\n", glc->info->fps);
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
