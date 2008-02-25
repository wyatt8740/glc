/**
 * \file src/play.c
 * \brief stream player
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

#include <stdlib.h>
#include <unistd.h>
#include <semaphore.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>

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

enum play_action {action_play, action_info, action_img, action_yuv4mpeg, action_wav, action_val};

struct play_s {
	glc_t glc;
	enum play_action action;

	file_t file;
	const char *stream_file;

	double scale_factor;
	unsigned int scale_width, scale_height;

	size_t compressed_size, uncompressed_size;
};

int show_info_value(struct play_s *play, const char *value);

int play_stream(struct play_s *play);
int stream_info(struct play_s *play);
int export_img(struct play_s *play);
int export_yuv4mpeg(struct play_s *play);
int export_wav(struct play_s *play);

int main(int argc, char *argv[])
{
	struct play_s play;
	play.action = action_play;
	const char *val_str = NULL;
	int opt, option_index;

	struct option long_options[] = {
		{"info",		1, NULL, 'i'},
		{"wav",			1, NULL, 'a'},
		{"bmp",			1, NULL, 'b'},
		{"png",			1, NULL, 'p'},
		{"yuv4mpeg",		1, NULL, 'y'},
		{"out",			1, NULL, 'o'},
		{"fps",			1, NULL, 'f'},
		{"resize",		1, NULL, 'r'},
		{"adjust",		1, NULL, 'g'},
		{"silence",		1, NULL, 'l'},
		{"alsa-device",		1, NULL, 'd'},
		{"streaming",		0, NULL, 't'},
		{"compressed",		1, NULL, 'c'},
		{"uncompressed",	1, NULL, 'u'},
		{"show",		1, NULL, 's'},
		{"verbosity",		1, NULL, 'v'},
		{"help",		0, NULL, 'h'},
		{0, 0, 0, 0}
	};
	option_index = 0;

	/* initialize glc with some sane values */
	play.glc.flags = 0;
	play.glc.fps = 0;
	play.glc.filename_format = NULL; /* user has to specify */

	play.glc.silence_threshold = 200000; /* 0.2 sec accuracy */
	play.glc.alsa_playback_device = "default";

	/* don't scale by default */
	play.scale_factor = 1;
	play.scale_width = play.scale_height = 0;

	/* default buffer size is 10MiB */
	play.compressed_size = 10 * 1024 * 1024;
	play.uncompressed_size = 10 * 1024 * 1024;

	/* log to stderr */
	play.glc.log_file = "/dev/stderr";
	play.glc.log_level = 0;

	/* global color correction */
	play.glc.brightness = play.glc.contrast = 0;
	play.glc.red_gamma = 1.0;
	play.glc.green_gamma = 1.0;
	play.glc.blue_gamma = 1.0;

	while ((opt = getopt_long(argc, argv, "i:a:b:p:y:o:f:r:g:l:td:c:u:s:v:h",
				  long_options, &optind)) != -1) {
		switch (opt) {
		case 'i':
			play.glc.info_level = atoi(optarg);
			if (play.glc.info_level < 1)
				goto usage;
			play.action = action_info;
			break;
		case 'a':
			play.glc.export_audio = atoi(optarg);
			if (play.glc.export_audio < 1)
				goto usage;
			play.action = action_wav;
			break;
		case 'p':
			play.glc.flags |= GLC_EXPORT_PNG;
		case 'b':
			play.glc.export_ctx = atoi(optarg);
			if (play.glc.export_ctx < 1)
				goto usage;
			play.action = action_img;
			break;
		case 'y':
			play.glc.export_ctx = atoi(optarg);
			if (play.glc.export_ctx < 1)
				goto usage;
			play.action = action_yuv4mpeg;
			break;
		case 'f':
			play.glc.fps = atof(optarg);
			if (play.glc.fps <= 0)
				goto usage;
			break;
		case 'r':
			if (strstr(optarg, "x")) {
				sscanf(optarg, "%ux%u", &play.scale_width, &play.scale_height);
				if ((!play.scale_width) | (!play.scale_height))
					goto usage;
			} else {
				play.scale_factor = atof(optarg);
				if (play.scale_factor <= 0)
					goto usage;
			}
			break;
		case 'g':
			play.glc.flags |= GLC_OVERRIDE_COLOR_CORRECTION;
			sscanf(optarg, "%f;%f;%f;%f;%f", &play.glc.brightness, &play.glc.contrast,
			       &play.glc.red_gamma, &play.glc.green_gamma, &play.glc.blue_gamma);
			break;
		case 'l':
			/* glc_utime_t so always positive */
			play.glc.silence_threshold = atof(optarg) * 1000000;
			break;
		case 'd':
			play.glc.alsa_playback_device = optarg;
			break;
		case 'o':
			if (!strcmp(optarg, "-")) /** \todo fopen(1) ? */
				play.glc.filename_format = "/dev/stdout";
			else
				play.glc.filename_format = optarg;
			break;
		case 't':
			play.glc.flags |= GLC_EXPORT_STREAMING;
			break;
		case 'c':
			play.compressed_size = atoi(optarg) * 1024 * 1024;
			if (play.compressed_size <= 0)
				goto usage;
			break;
		case 'u':
			play.uncompressed_size = atoi(optarg) * 1024 * 1024;
			if (play.uncompressed_size <= 0)
				goto usage;
			break;
		case 's':
			val_str = optarg;
			play.action = action_val;
			break;
		case 'v':
			play.glc.log_level = atoi(optarg);
			if (play.glc.log_level < 0)
				goto usage;
			play.glc.flags |= GLC_LOG | GLC_NOERR;
			break;
		case 'h':
		default:
			goto usage;
		}
	}

	/* stream file is mandatory */
	if (optind >= argc)
		goto usage;
	play.stream_file = argv[optind];

	/* same goes to output file */
	if (((play.action == action_img) |
	     (play.action == action_wav) |
	     (play.action == action_yuv4mpeg)) &&
	    (play.glc.filename_format == NULL))
		goto usage;

	/* we do global initialization */
	util_init(&play.glc);
	if (play.glc.flags & GLC_LOG) {
		util_log_init(&play.glc);
		util_log_version(&play.glc);
	}

	/* open stream file */
	if (file_init(&play.file, &play.glc))
		return EXIT_FAILURE;
	if (file_open_source(play.file, play.stream_file))
		return EXIT_FAILURE;

	/* load information and check that the file is valid */
	util_create_info(&play.glc);
	if (file_read_info(play.file, play.glc.info, &play.glc.info_name, &play.glc.info_date))
		return EXIT_FAILURE;

	/*
	 If the fps hasn't been specified read it from the
	 stream information.
	*/
	if (play.glc.fps == 0)
		play.glc.fps = play.glc.info->fps;

	switch (play.action) {
	case action_play:
		if (play_stream(&play))
			return EXIT_FAILURE;
		break;
	case action_wav:
		if (export_wav(&play))
			return EXIT_FAILURE;
		break;
	case action_yuv4mpeg:
		if (export_yuv4mpeg(&play))
			return EXIT_FAILURE;
		break;
	case action_img:
		if (export_img(&play))
			return EXIT_FAILURE;
		break;
	case action_info:
		if (stream_info(&play))
			return EXIT_FAILURE;
		break;
	case action_val:
		if (show_info_value(&play, val_str))
			return EXIT_FAILURE;
		break;
	}

	/* our cleanup */
	file_close_source(play.file);
	file_destroy(play.file);
	util_log_close(&play.glc);
	util_free_info(&play.glc);
	util_free(&play.glc);

	return EXIT_SUCCESS;

usage:
	printf("%s [file] [option]...\n", argv[0]);
	printf("  -i, --info=LEVEL         show stream information, LEVEL must be\n"
	       "                             greater than 0\n"
	       "  -a, --wav=NUM            save audio stream NUM in wav format\n"
	       "  -b, --bmp=NUM            save frames from stream NUM as bmp files\n"
	       "                             (use -o pic-%%010d.bmp f.ex.)\n"
	       "  -p, --png=NUM            save frames from stream NUM as png files\n"
	       "  -y, --yuv4mpeg=NUM       save video stream NUM in yuv4mpeg format\n"
	       "  -o, --out=FILE           write to FILE\n"
	       "  -f, --fps=FPS            save images or video at FPS\n"
	       "  -r, --resize=VAL         resize pictures with scale factor VAL or WxH\n"
	       "  -g, --color=ADJUST       adjust colors\n"
	       "                             format is brightness;contrast;red;green;blue\n"
	       "  -l, --silence=SECONDS    audio silence threshold in seconds\n"
	       "                             default threshold is 0.2\n"
	       "  -d, --alsa-device=DEV    alsa playback device name\n"
	       "                             default is 'default'\n"
	       "  -t, --streaming          streaming mode (eg. don't interpolate data)\n"
	       "  -c, --compressed=SIZE    compressed stream buffer size in MiB\n"
	       "                             default is 10 MiB\n"
	       "  -u, --uncompressed=SIZE  uncompressed stream buffer size in MiB\n"
	       "                             default is 10 MiB\n"
	       "  -s, --show=VAL           show stream summary value, possible values are:\n"
	       "                             all, signature, version, flags, fps,\n"
	       "                             pid, name, date\n"
	       "  -v, --verbosity=LEVEL    verbosity level\n"
	       "  -h, --help               show help\n");

	return EXIT_FAILURE;
}

int show_info_value(struct play_s *play, const char *value)
{
	if (!strcmp("all", value)) {
		printf("  signature   = 0x%08x\n", play->glc.info->signature);
		printf("  version     = 0x%02x\n", play->glc.info->version);
		printf("  flags       = %d\n", play->glc.info->flags);
		printf("  fps         = %f\n", play->glc.info->fps);
		printf("  pid         = %d\n", play->glc.info->pid);
		printf("  name        = %s\n", play->glc.info_name);
		printf("  date        = %s\n", play->glc.info_date);
	} else if (!strcmp("signature", value))
		printf("0x%08x\n", play->glc.info->signature);
	else if (!strcmp("version", value))
		printf("0x%02x\n", play->glc.info->version);
	else if (!strcmp("flags", value))
		printf("%d\n", play->glc.info->flags);
	else if (!strcmp("fps", value))
		printf("%f\n", play->glc.info->fps);
	else if (!strcmp("pid", value))
		printf("%d\n", play->glc.info->pid);
	else if (!strcmp("name", value))
		printf("%s\n", play->glc.info_name);
	else if (!strcmp("date", value))
		printf("%s\n", play->glc.info_date);
	else
		return ENOTSUP;
	return 0;
}

int play_stream(struct play_s *play)
{
	/*
	 Playback uses following pipeline:

	 file -(uncompressed)->     reads data from stream file
	 unpack -(uncompressed)->   decompresses lzo/quicklz packets
	 rgb -(rgb)->               does conversion to BGR
	 scale -(scale)->           does rescaling
	 color -(color)->           applies color correction
	 demux -(...)-> gl_play, audio_play

	 Each filter, except demux and file, has util_cpus() worker
	 threads. Packet order in stream is preserved. Demux creates
	 separate buffer and _play handler for each video/audio stream.
	*/

	ps_bufferattr_t attr;
	ps_buffer_t uncompressed_buffer, compressed_buffer,
		    rgb_buffer, color_buffer, scale_buffer;
	void *color, *rgb, *demux;
	scale_t scale;
	unpack_t unpack;
	int ret = 0;

	if ((ret = ps_bufferattr_init(&attr)))
		goto err;

	/*
	 'compressed_buffer' buffer holds raw data from file and
	 has its own size.
	*/
	if ((ret = ps_bufferattr_setsize(&attr, play->compressed_size)))
		goto err;
	if ((ret = ps_buffer_init(&compressed_buffer, &attr)))
		goto err;

	/* rest use 'uncompressed_buffer' size */
	if ((ret = ps_bufferattr_setsize(&attr, play->uncompressed_size)))
		goto err;
	if ((ret = ps_buffer_init(&uncompressed_buffer, &attr)))
		goto err;
	if ((ret = ps_buffer_init(&color_buffer, &attr)))
		goto err;
	if ((ret = ps_buffer_init(&rgb_buffer, &attr)))
		goto err;
	if ((ret = ps_buffer_init(&scale_buffer, &attr)))
		goto err;

	/* no longer necessary */
	if ((ret = ps_bufferattr_destroy(&attr)))
		goto err;

	/* init filters */
	if ((ret = unpack_init(&unpack, &play->glc)))
		goto err;
	if ((ret = scale_init(&scale, &play->glc)))
		goto err;
	if (play->scale_width && play->scale_height)
		scale_set_size(scale, play->scale_width, play->scale_height);
	else
		scale_set_scale(scale, play->scale_factor);

	/* construct a pipeline for playback */
	if ((ret = unpack_process_start(unpack, &compressed_buffer, &uncompressed_buffer)))
		goto err;
	if ((rgb = rgb_init(&play->glc, &uncompressed_buffer, &rgb_buffer)) == NULL)
		goto err;
	if ((ret = scale_process_start(scale, &rgb_buffer, &scale_buffer)))
		goto err;
	if ((color = color_init(&play->glc, &scale_buffer, &color_buffer)) == NULL)
		goto err;
	if ((demux = demux_init(&play->glc, &color_buffer)) == NULL)
		goto err;

	/* the pipeline is ready - lets give it some data */
	if ((ret = file_read(play->file, &compressed_buffer)))
		goto err;

	/* we've done our part - just wait for the threads */
	if ((ret = demux_wait(demux)))
		goto err; /* wait for demux, since when it quits, others should also */
	if ((ret = color_wait(color)))
		goto err;
	if ((ret = scale_process_wait(scale)))
		goto err;
	if ((ret = rgb_wait(rgb)))
		goto err;
	if ((ret = unpack_process_wait(unpack)))
		goto err;

	/* stream processed - clean up time */
	unpack_destroy(unpack);

	ps_buffer_destroy(&compressed_buffer);
	ps_buffer_destroy(&uncompressed_buffer);
	ps_buffer_destroy(&color_buffer);
	ps_buffer_destroy(&scale_buffer);
	ps_buffer_destroy(&rgb_buffer);

	scale_destroy(scale);

	return 0;
err:
	if (!ret) {
		fprintf(stderr, "playing stream failed: initializing filters failed\n");
		return EAGAIN;
	} else {
		fprintf(stderr, "playing stream failed: %s (%d)\n", strerror(ret), ret);
		return ret;
	}
}

int stream_info(struct play_s *play)
{
	/*
	 Info uses following pipeline:

	 file -(uncompressed_buffer)->     reads data from stream file
	 unpack -(uncompressed_buffer)->   decompresses lzo/quicklz packets
	 info -(rgb)->              shows stream information
	*/

	ps_bufferattr_t attr;
	ps_buffer_t uncompressed_buffer, compressed_buffer;
	void *info;
	unpack_t unpack;
	int ret = 0;

	if ((ret = ps_bufferattr_init(&attr)))
		goto err;

	/* initialize buffers */
	if ((ret = ps_bufferattr_setsize(&attr, play->compressed_size)))
		goto err;
	if ((ret = ps_buffer_init(&compressed_buffer, &attr)))
		goto err;

	if ((ret = ps_bufferattr_setsize(&attr, play->uncompressed_size)))
		goto err;
	if ((ret = ps_buffer_init(&uncompressed_buffer, &attr)))
		goto err;

	if ((ret = ps_bufferattr_destroy(&attr)))
		goto err;

	/* and filters */
	if ((ret = unpack_init(&unpack, &play->glc)))
		goto err;

	/* run it */
	if ((ret = unpack_process_start(unpack, &compressed_buffer, &uncompressed_buffer)))
		goto err;
	if ((info = info_init(&play->glc, &uncompressed_buffer)) == NULL)
		goto err;
	if ((ret = file_read(play->file, &compressed_buffer)))
		goto err;

	/* wait for threads and do cleanup */
	if ((ret = info_wait(info)))
		goto err;
	if ((ret = unpack_process_wait(unpack)))
		goto err;

	unpack_destroy(unpack);

	ps_buffer_destroy(&compressed_buffer);
	ps_buffer_destroy(&uncompressed_buffer);

	return 0;
err:
	if (!ret) {
		fprintf(stderr, "extracting stream information failed: initializing filters failed\n");
		return EAGAIN;
	} else {
		fprintf(stderr, "extracting stream information failed: %s (%d)\n", strerror(ret), ret);
		return ret;
	}
}

int export_img(struct play_s *play)
{
	/*
	 Export img uses following pipeline:

	 file -(uncompressed_buffer)->     reads data from stream file
	 unpack -(uncompressed_buffer)->   decompresses lzo/quicklz packets
	 rgb -(rgb)->               does conversion to BGR
	 scale -(scale)->           does rescaling
	 color -(color)->           applies color correction
	 img                        writes separate image files for each frame
	*/

	ps_bufferattr_t attr;
	ps_buffer_t uncompressed_buffer, compressed_buffer,
		    rgb_buffer, color_buffer, scale_buffer;
	void *rgb, *color, *img;
	scale_t scale;
	unpack_t unpack;
	int ret = 0;

	if ((ret = ps_bufferattr_init(&attr)))
		goto err;

	/* buffers */
	if ((ret = ps_bufferattr_setsize(&attr, play->compressed_size)))
		goto err;
	if ((ret = ps_buffer_init(&compressed_buffer, &attr)))
		goto err;

	if ((ret = ps_bufferattr_setsize(&attr, play->uncompressed_size)))
		goto err;
	if ((ret = ps_buffer_init(&uncompressed_buffer, &attr)))
		goto err;
	if ((ret = ps_buffer_init(&color_buffer, &attr)))
		goto err;
	if ((ret = ps_buffer_init(&rgb_buffer, &attr)))
		goto err;
	if ((ret = ps_buffer_init(&scale_buffer, &attr)))
		goto err;

	if ((ret = ps_bufferattr_destroy(&attr)))
		goto err;

	if ((ret = unpack_init(&unpack, &play->glc)))
		goto err;
	if ((ret = scale_init(&scale, &play->glc)))
		goto err;
	if (play->scale_width && play->scale_height)
		scale_set_size(scale, play->scale_width, play->scale_height);
	else
		scale_set_scale(scale, play->scale_factor);

	/* pipeline... */
	if ((ret = unpack_process_start(unpack, &compressed_buffer, &uncompressed_buffer)))
		goto err;
	if ((rgb = rgb_init(&play->glc, &uncompressed_buffer, &rgb_buffer)) == NULL)
		goto err;
	if ((ret = scale_process_start(scale, &rgb_buffer, &scale_buffer)))
		goto err;
	if ((color = color_init(&play->glc, &scale_buffer, &color_buffer)) == NULL)
		goto err;
	if ((img = img_init(&play->glc, &color_buffer)) == NULL)
		goto err;

	/* ok, read the file */
	if ((ret = file_read(play->file, &compressed_buffer)))
		goto err;

	/* wait 'till its done and clean up the mess... */
	if ((ret = img_wait(img)))
		goto err;
	if ((ret = color_wait(color)))
		goto err;
	if ((ret = scale_process_wait(scale)))
		goto err;
	if ((ret = rgb_wait(rgb)))
		goto err;
	if ((ret = unpack_process_wait(unpack)))
		goto err;

	unpack_destroy(unpack);

	ps_buffer_destroy(&compressed_buffer);
	ps_buffer_destroy(&uncompressed_buffer);
	ps_buffer_destroy(&color_buffer);
	ps_buffer_destroy(&scale_buffer);
	ps_buffer_destroy(&rgb_buffer);

	scale_destroy(scale);

	return 0;
err:
	if (!ret) {
		fprintf(stderr, "exporting images failed: initializing filters failed\n");
		return EAGAIN;
	} else {
		fprintf(stderr, "exporting images failed: %s (%d)\n", strerror(ret), ret);
		return ret;
	}
}

int export_yuv4mpeg(struct play_s *play)
{
	/*
	 Export yuv4mpeg uses following pipeline:

	 file -(uncompressed_buffer)->     reads data from stream file
	 unpack -(uncompressed_buffer)->   decompresses lzo/quicklz packets
	 scale -(scale)->           does rescaling
	 color -(color)->           applies color correction
	 ycbcr -(ycbcr)->           does conversion to Y'CbCr (if necessary)
	 yuv4mpeg                   writes yuv4mpeg stream
	*/

	ps_bufferattr_t attr;
	ps_buffer_t uncompressed_buffer, compressed_buffer,
		    ycbcr_buffer, color_buffer, scale_buffer;
	void *color, *yuv4mpeg;
	ycbcr_t ycbcr;
	scale_t scale;
	unpack_t unpack;
	int ret = 0;

	if ((ret = ps_bufferattr_init(&attr)))
		goto err;

	/* buffers */
	if ((ret = ps_bufferattr_setsize(&attr, play->compressed_size)))
		goto err;
	if ((ret = ps_buffer_init(&compressed_buffer, &attr)))
		goto err;

	if ((ret = ps_bufferattr_setsize(&attr, play->uncompressed_size)))
		goto err;
	if ((ret = ps_buffer_init(&uncompressed_buffer, &attr)))
		goto err;
	if ((ret = ps_buffer_init(&color_buffer, &attr)))
		goto err;
	if ((ret = ps_buffer_init(&ycbcr_buffer, &attr)))
		goto err;
	if ((ret = ps_buffer_init(&scale_buffer, &attr)))
		goto err;

	if ((ret = ps_bufferattr_destroy(&attr)))
		goto err;

	/* initialize filters */
	if ((ret = unpack_init(&unpack, &play->glc)))
		goto err;
	if ((ret = ycbcr_init(&ycbcr, &play->glc)))
		goto err;
	if ((ret = scale_init(&scale, &play->glc)))
		goto err;
	if (play->scale_width && play->scale_height)
		scale_set_size(scale, play->scale_width, play->scale_height);
	else
		scale_set_scale(scale, play->scale_factor);

	/* construct the pipeline */
	if ((ret = unpack_process_start(unpack, &compressed_buffer, &uncompressed_buffer)))
		goto err;
	if ((ret = scale_process_start(scale, &uncompressed_buffer, &scale_buffer)))
		goto err;
	if ((color = color_init(&play->glc, &scale_buffer, &color_buffer)) == NULL)
		goto err;
	if ((ret = ycbcr_process_start(ycbcr, &color_buffer, &ycbcr_buffer)))
		goto err;
	if ((yuv4mpeg = yuv4mpeg_init(&play->glc, &ycbcr_buffer)) == NULL)
		goto err;

	/* feed it with data */
	if ((ret = file_read(play->file, &compressed_buffer)))
		goto err;

	/* threads will do the dirty work... */
	if ((ret = yuv4mpeg_wait(yuv4mpeg)))
		goto err;
	if ((ret = color_wait(color)))
		goto err;
	if ((ret = scale_process_wait(scale)))
		goto err;
	if ((ret = ycbcr_process_wait(ycbcr)))
		goto err;
	if ((ret = unpack_process_wait(unpack)))
		goto err;

	unpack_destroy(unpack);
	ycbcr_destroy(ycbcr);
	scale_destroy(scale);

	ps_buffer_destroy(&compressed_buffer);
	ps_buffer_destroy(&uncompressed_buffer);
	ps_buffer_destroy(&color_buffer);
	ps_buffer_destroy(&scale_buffer);
	ps_buffer_destroy(&ycbcr_buffer);

	return 0;
err:
	if (!ret) {
		fprintf(stderr, "exporting  yuv4mpegfailed: initializing filters failed\n");
		return EAGAIN;
	} else {
		fprintf(stderr, "exporting yuv4mpeg failed: %s (%d)\n", strerror(ret), ret);
		return ret;
	}
}

int export_wav(struct play_s *play)
{
	/*
	 Export wav uses following pipeline:

	 file -(uncompressed_buffer)->     reads data from stream file
	 unpack -(uncompressed_buffer)->   decompresses lzo/quicklz packets
	 wav -(rgb)->               write audio to file in wav format
	*/

	ps_bufferattr_t attr;
	ps_buffer_t uncompressed_buffer, compressed_buffer;
	void *wav;
	unpack_t unpack;
	int ret = 0;

	if ((ret = ps_bufferattr_init(&attr)))
		goto err;

	/* buffers */
	if ((ret = ps_bufferattr_setsize(&attr, play->compressed_size)))
		goto err;
	if ((ret = ps_buffer_init(&compressed_buffer, &attr)))
		goto err;

	if ((ret = ps_bufferattr_setsize(&attr, play->uncompressed_size)))
		goto err;
	if ((ret = ps_buffer_init(&uncompressed_buffer, &attr)))
		goto err;

	if ((ret = ps_bufferattr_destroy(&attr)))
		goto err;

	if ((ret = unpack_init(&unpack, &play->glc)))
		goto err;

	/* start the threads */
	if ((ret = unpack_process_start(unpack, &compressed_buffer, &uncompressed_buffer)))
		goto err;
	if ((wav = wav_init(&play->glc, &uncompressed_buffer)) == NULL)
		goto err;
	if ((ret = file_read(play->file, &compressed_buffer)))
		goto err;

	/* wait and clean up */
	if ((ret = wav_wait(wav)))
		goto err;
	if ((ret = unpack_process_wait(unpack)))
		goto err;

	unpack_destroy(unpack);

	ps_buffer_destroy(&compressed_buffer);
	ps_buffer_destroy(&uncompressed_buffer);

	return 0;
err:
	if (!ret) {
		fprintf(stderr, "exporting wav failed: initializing filters failed\n");
		return EAGAIN;
	} else {
		fprintf(stderr, "exporting wav failed: %s (%d)\n", strerror(ret), ret);
		return ret;
	}
}
