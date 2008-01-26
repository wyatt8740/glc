/**
 * \file src/play.c
 * \brief stream player
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
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

enum glc_play_action_t {play, info, img, yuv4mpeg, wav, val};

int show_info_value(glc_t *glc, const char *value);

int play_stream(glc_t *glc);
int stream_info(glc_t *glc);
int export_img(glc_t *glc);
int export_yuv4mpeg(glc_t *glc);
int export_wav(glc_t *glc);

int main(int argc, char *argv[])
{
	glc_t glc;
	const char *val_str = NULL;
	int opt, option_index;
	enum glc_play_action_t action = play;

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
		{"compressed",		1, NULL, 'c'},
		{"uncompressed",	1, NULL, 'u'},
		{"show",		1, NULL, 's'},
		{"verbosity",		1, NULL, 'v'},
		{"help",		0, NULL, 'h'},
		{0, 0, 0, 0}
	};
	option_index = 0;

	/* initialize glc with some sane values */
	glc.flags = 0;
	glc.fps = 0;
	glc.filename_format = NULL; /* user has to specify */

	glc.silence_threshold = 200000; /* 0.2 sec accuracy */
	glc.alsa_playback_device = "default";

	/* don't scale by default */
	glc.scale = 1;
	glc.scale_width = glc.scale_height = 0;

	/* default buffer size is 10MiB */
	glc.compressed_size = 10 * 1024 * 1024;
	glc.uncompressed_size = 10 * 1024 * 1024;

	/* log to stderr */
	glc.log_file = "/dev/stderr";
	glc.log_level = 0;

	/* global color correction */
	glc.brightness = glc.contrast = 0;
	glc.red_gamma = 1.0;
	glc.green_gamma = 1.0;
	glc.blue_gamma = 1.0;

	while ((opt = getopt_long(argc, argv, "i:a:b:p:y:o:f:r:g:l:d:c:u:s:v:h",
				  long_options, &optind)) != -1) {
		switch (opt) {
		case 'i':
			glc.info_level = atoi(optarg);
			if (glc.info_level < 1)
				goto usage;
			action = info;
			break;
		case 'a':
			glc.export_audio = atoi(optarg);
			if (glc.export_audio < 1)
				goto usage;
			action = wav;
			break;
		case 'p':
			glc.flags |= GLC_EXPORT_PNG;
		case 'b':
			glc.export_ctx = atoi(optarg);
			if (glc.export_ctx < 1)
				goto usage;
			action = img;
			break;
		case 'y':
			glc.export_ctx = atoi(optarg);
			if (glc.export_ctx < 1)
				goto usage;
			action = yuv4mpeg;
			break;
		case 'f':
			glc.fps = atof(optarg);
			if (glc.fps <= 0)
				goto usage;
			break;
		case 'r':
			if (strstr(optarg, "x")) {
				sscanf(optarg, "%ux%u", &glc.scale_width, &glc.scale_height);
				if ((!glc.scale_width) | (!glc.scale_height))
					goto usage;
				glc.flags |= GLC_SCALE_SIZE;
			} else {
				glc.scale = atof(optarg);
				if (glc.scale <= 0)
					goto usage;
			}
			break;
		case 'g':
			glc.flags |= GLC_OVERRIDE_COLOR_CORRECTION;
			sscanf(optarg, "%f;%f;%f;%f;%f", &glc.brightness, &glc.contrast,
			       &glc.red_gamma, &glc.green_gamma, &glc.blue_gamma);
			break;
		case 'l':
			/* glc_utime_t so always positive */
			glc.silence_threshold = atof(optarg) * 1000000;
			break;
		case 'd':
			glc.alsa_playback_device = optarg;
			break;
		case 'o':
			if (!strcmp(optarg, "-")) /** \todo fopen(1) ? */
				glc.filename_format = "/dev/stdout";
			else
				glc.filename_format = optarg;
			break;
		case 'c':
			glc.compressed_size = atoi(optarg) * 1024 * 1024;
			if (glc.compressed_size <= 0)
				goto usage;
			break;
		case 'u':
			glc.uncompressed_size = atoi(optarg) * 1024 * 1024;
			if (glc.uncompressed_size <= 0)
				goto usage;
			break;
		case 's':
			val_str = optarg;
			action = val;
			break;
		case 'v':
			glc.log_level = atoi(optarg);
			if (glc.log_level < 0)
				goto usage;
			glc.flags |= GLC_LOG | GLC_NOERR;
			break;
		case 'h':
		default:
			goto usage;
		}
	}

	/* stream file is mandatory */
	if (optind >= argc)
		goto usage;
	glc.stream_file = argv[optind];

	/* same goes to output file */
	if (((action == img) |
	     (action == wav) |
	     (action == yuv4mpeg)) &&
	    (glc.filename_format == NULL))
		goto usage;

	/* we do global initialization */
	util_init(&glc);
	if (glc.flags & GLC_LOG) {
		util_log_init(&glc);
		util_log_version(&glc);
	}

	/* load information and check that the file is valid */
	if (util_load_info(&glc, glc.stream_file))
		return EXIT_FAILURE;

	/*
	 If the fps hasn't been specified read it from the
	 stream information.
	*/
	if (glc.fps == 0)
		glc.fps = glc.info->fps;

	switch (action) {
	case play:
		if (play_stream(&glc))
			return EXIT_FAILURE;
		break;
	case wav:
		if (export_wav(&glc))
			return EXIT_FAILURE;
		break;
	case yuv4mpeg:
		if (export_yuv4mpeg(&glc))
			return EXIT_FAILURE;
		break;
	case img:
		if (export_img(&glc))
			return EXIT_FAILURE;
		break;
	case info:
		if (stream_info(&glc))
			return EXIT_FAILURE;
		break;
	case val:
		if (show_info_value(&glc, val_str))
			return EXIT_FAILURE;
		break;
	}

	/* our cleanup */
	util_log_close(&glc);
	util_free_info(&glc);
	util_free(&glc);

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

int show_info_value(glc_t *glc, const char *value)
{
	if (!strcmp("all", value)) {
		printf("  signature   = 0x%08x\n", glc->info->signature);
		printf("  version     = 0x%02x\n", glc->info->version);
		printf("  flags       = %d\n", glc->info->flags);
		printf("  fps         = %f\n", glc->info->fps);
		printf("  pid         = %d\n", glc->info->pid);
		printf("  name        = %s\n", glc->info_name);
		printf("  date        = %s\n", glc->info_date);
	} else if (!strcmp("signature", value))
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
		return ENOTSUP;
	return 0;
}

int play_stream(glc_t *glc)
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
	void *unpack, *color, *scale, *rgb, *demux;
	int ret = 0;

	if ((ret = ps_bufferattr_init(&attr)))
		goto err;

	/*
	 'compressed_buffer' buffer holds raw data from file and
	 has its own size.
	*/
	if ((ret = ps_bufferattr_setsize(&attr, glc->compressed_size)))
		goto err;
	if ((ret = ps_buffer_init(&compressed_buffer, &attr)))
		goto err;

	/* rest use 'uncompressed_buffer' size */
	if ((ret = ps_bufferattr_setsize(&attr, glc->uncompressed_size)))
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

	/* construct a pipeline for playback */
	if ((unpack = unpack_init(glc, &compressed_buffer, &uncompressed_buffer)) == NULL)
		goto err;
	if ((rgb = rgb_init(glc, &uncompressed_buffer, &rgb_buffer)) == NULL)
		goto err;
	if ((scale = scale_init(glc, &rgb_buffer, &scale_buffer)) == NULL)
		goto err;
	if ((color = color_init(glc, &scale_buffer, &color_buffer)) == NULL)
		goto err;
	if ((demux = demux_init(glc, &color_buffer)) == NULL)
		goto err;

	/* the pipeline is ready - lets give it some data */
	if ((ret = file_read(glc, &compressed_buffer)))
		goto err;

	/* we've done our part - just wait for the threads */
	if ((ret = demux_wait(demux)))
		goto err; /* wait for demux, since when it quits, others should also */
	if ((ret = color_wait(color)))
		goto err;
	if ((ret = scale_wait(scale)))
		goto err;
	if ((ret = rgb_wait(rgb)))
		goto err;
	if ((ret = unpack_wait(unpack)))
		goto err;

	/* stream processed - clean up time */
	ps_buffer_destroy(&compressed_buffer);
	ps_buffer_destroy(&uncompressed_buffer);
	ps_buffer_destroy(&color_buffer);
	ps_buffer_destroy(&scale_buffer);
	ps_buffer_destroy(&rgb_buffer);

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

int stream_info(glc_t *glc)
{
	/*
	 Info uses following pipeline:

	 file -(uncompressed_buffer)->     reads data from stream file
	 unpack -(uncompressed_buffer)->   decompresses lzo/quicklz packets
	 info -(rgb)->              shows stream information
	*/

	ps_bufferattr_t attr;
	ps_buffer_t uncompressed_buffer, compressed_buffer;
	void *unpack, *info;
	int ret = 0;

	if ((ret = ps_bufferattr_init(&attr)))
		goto err;

	/* initialize buffers */
	if ((ret = ps_bufferattr_setsize(&attr, glc->compressed_size)))
		goto err;
	if ((ret = ps_buffer_init(&compressed_buffer, &attr)))
		goto err;

	if ((ret = ps_bufferattr_setsize(&attr, glc->uncompressed_size)))
		goto err;
	if ((ret = ps_buffer_init(&uncompressed_buffer, &attr)))
		goto err;

	if ((ret = ps_bufferattr_destroy(&attr)))
		goto err;

	/* run it */
	if ((info = unpack_init(glc, &compressed_buffer, &uncompressed_buffer)) == NULL)
		goto err;
	if ((unpack = info_init(glc, &uncompressed_buffer)) == NULL)
		goto err;
	if ((ret = file_read(glc, &compressed_buffer)))
		goto err;

	/* wait for threads and do cleanup */
	if ((ret = info_wait(info)))
		goto err;
	if ((ret = unpack_wait(unpack)))
		goto err;

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

int export_img(glc_t *glc)
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
	void *unpack, *rgb, *color, *scale, *img;
	int ret = 0;

	if ((ret = ps_bufferattr_init(&attr)))
		goto err;

	/* buffers */
	if ((ret = ps_bufferattr_setsize(&attr, glc->compressed_size)))
		goto err;
	if ((ret = ps_buffer_init(&compressed_buffer, &attr)))
		goto err;

	if ((ret = ps_bufferattr_setsize(&attr, glc->uncompressed_size)))
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

	/* pipeline... */
	if ((unpack = unpack_init(glc, &compressed_buffer, &uncompressed_buffer)) == NULL)
		goto err;
	if ((rgb = rgb_init(glc, &uncompressed_buffer, &rgb_buffer)) == NULL)
		goto err;
	if ((scale = scale_init(glc, &rgb_buffer, &scale_buffer)) == NULL)
		goto err;
	if ((color = color_init(glc, &scale_buffer, &color_buffer)) == NULL)
		goto err;
	if ((img = img_init(glc, &color_buffer)) == NULL)
		goto err;

	/* ok, read the file */
	if ((ret = file_read(glc, &compressed_buffer)))
		goto err;

	/* wait 'till its done and clean up the mess... */
	if ((ret = img_wait(img)))
		goto err;
	if ((ret = color_wait(color)))
		goto err;
	if ((ret = scale_wait(scale)))
		goto err;
	if ((ret = rgb_wait(rgb)))
		goto err;
	if ((ret = unpack_wait(unpack)))
		goto err;

	ps_buffer_destroy(&compressed_buffer);
	ps_buffer_destroy(&uncompressed_buffer);
	ps_buffer_destroy(&color_buffer);
	ps_buffer_destroy(&scale_buffer);
	ps_buffer_destroy(&rgb_buffer);

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

int export_yuv4mpeg(glc_t *glc)
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
	void *unpack, *ycbcr, *color, *scale, *yuv4mpeg;
	int ret = 0;

	if ((ret = ps_bufferattr_init(&attr)))
		goto err;

	/* buffers */
	if ((ret = ps_bufferattr_setsize(&attr, glc->compressed_size)))
		goto err;
	if ((ret = ps_buffer_init(&compressed_buffer, &attr)))
		goto err;

	if ((ret = ps_bufferattr_setsize(&attr, glc->uncompressed_size)))
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

	/* construct the pipeline */
	if ((unpack = unpack_init(glc, &compressed_buffer, &uncompressed_buffer)) == NULL)
		goto err;
	if ((scale = scale_init(glc, &uncompressed_buffer, &scale_buffer)) == NULL)
		goto err;
	if ((color = color_init(glc, &scale_buffer, &color_buffer)) == NULL)
		goto err;
	if ((ycbcr = ycbcr_init(glc, &color_buffer, &ycbcr_buffer)) == NULL)
		goto err;
	if ((yuv4mpeg = yuv4mpeg_init(glc, &ycbcr_buffer)) == NULL)
		goto err;

	/* feed it with data */
	if ((ret = file_read(glc, &compressed_buffer)))
		goto err;

	/* threads will do the dirty work... */
	if ((ret = yuv4mpeg_wait(yuv4mpeg)))
		goto err;
	if ((ret = color_wait(color)))
		goto err;
	if ((ret = scale_wait(scale)))
		goto err;
	if ((ret = ycbcr_wait(ycbcr)))
		goto err;
	if ((ret = unpack_wait(unpack)))
		goto err;

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

int export_wav(glc_t *glc)
{
	/*
	 Export wav uses following pipeline:

	 file -(uncompressed_buffer)->     reads data from stream file
	 unpack -(uncompressed_buffer)->   decompresses lzo/quicklz packets
	 wav -(rgb)->               write audio to file in wav format
	*/

	ps_bufferattr_t attr;
	ps_buffer_t uncompressed_buffer, compressed_buffer;
	void *unpack, *wav;
	int ret = 0;

	if ((ret = ps_bufferattr_init(&attr)))
		goto err;

	/* buffers */
	if ((ret = ps_bufferattr_setsize(&attr, glc->compressed_size)))
		goto err;
	if ((ret = ps_buffer_init(&compressed_buffer, &attr)))
		goto err;

	if ((ret = ps_bufferattr_setsize(&attr, glc->uncompressed_size)))
		goto err;
	if ((ret = ps_buffer_init(&uncompressed_buffer, &attr)))
		goto err;

	if ((ret = ps_bufferattr_destroy(&attr)))
		goto err;

	/* start the threads */
	if ((unpack = unpack_init(glc, &compressed_buffer, &uncompressed_buffer)) == NULL)
		goto err;
	if ((wav = wav_init(glc, &uncompressed_buffer)) == NULL)
		goto err;
	if ((ret = file_read(glc, &compressed_buffer)))
		goto err;

	/* wait and clean up */
	if ((ret = wav_wait(wav)))
		goto err;
	if ((ret = unpack_wait(unpack)))
		goto err;

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
