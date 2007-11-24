/**
 * \file src/capture.c
 * \brief capture utility
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* capture.c -- capture utility
 * Copyright (C) 2007 Pyry Haulos
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/* for setenv() */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>

int main(int argc, char *argv[])
{
	char opt;
	int ret = 0;
	int option_index = 0;
	int posixly_correct_was_set = 0;
	char *program = NULL;
	char **program_args = NULL;
	const char *ld_preload_old = NULL;
	const char *library = "libglc-hook.so";
	char *ld_preload;
	size_t ld_preload_len;

	struct option long_options[] = {
		{"out",			1, NULL, 'o'},
		{"fps",			1, NULL, 'f'},
		{"resize",		1, NULL, 'r'},
		{"crop",		1, NULL, 'y'},
		{"start",		0, NULL, 's'},
		{"colorspace",		1, NULL, 'e'},
		{"hotkey",		1, NULL, 'k'},
		{"lock-fps",		0, NULL, 'n'},
		{"no-pbo",		0, NULL, 'p'},
		{"compression",		1, NULL, 'z'},
		{"byte-aligned",	0, NULL, 'm'},
		{"draw-indicator",	0, NULL, 'i'},
		{"log",			1, NULL, 'v'},
		{"log-file",		1, NULL, 'l'},
		{"no-audio-skip",	0, NULL, 'w'},
		{"disable-audio",	0, NULL, 'a'},
		{"sighandler",		0, NULL, 'q'},
		{"glfinish",		0, NULL, 'g'},
		{"force-sdl-alsa-drv",	1, NULL, 'j'},
		{"capture",		1, NULL, 'b'},
		{"compressed",		1, NULL, 'c'},
		{"uncompressed",	1, NULL, 'u'},
		{"unscaled",		1, NULL, 'd'},
		{"help",		0, NULL, 'h'},
		{0, 0, 0, 0}
	};

	if (getenv("POSIXLY_CORRECT"))
		posixly_correct_was_set = 1;

	/* force getopt_long() to stop when non-option argument
	   is encountered. */
	setenv("POSIXLY_CORRECT", "1", 1);

	while ((opt = getopt_long(argc, argv, "o:f:r:y:se:k:npz:miv:l:waqgjb:c:u:d:h", long_options, &option_index)) != -1) {
		switch(opt) {
		case 'o':
			setenv("GLC_FILE", optarg, 1);
			break;
		case 'f':
			setenv("GLC_FPS", optarg, 1);
			break;
		case 'r':
			setenv("GLC_SCALE", optarg, 1);
			break;
		case 'y':
			setenv("GLC_CROP", optarg, 1);
			break;
		case 's':
			setenv("GLC_START", "1", 1);
			break;
		case 'e':
			setenv("GLC_COLORSPACE", optarg, 1);
			break;
		case 'k':
			setenv("GLC_HOTKEY", optarg, 1);
			break;
		case 'n':
			setenv("GLC_LOCK_FPS", "1", 1);
			break;
		case 'p':
			setenv("GLC_TRY_PBO", "0", 1);
			break;
		case 'z':
			setenv("GLC_COMPRESS", optarg, 1);
			break;
		case 'm':
			setenv("GLC_CAPTURE_DWORD_ALIGNED", "0", 1);
			break;
		case 'i':
			setenv("GLC_INDICATOR", "1", 1);
			break;
		case 'v':
			setenv("GLC_LOG", optarg, 1);
			break;
		case 'l':
			setenv("GLC_LOG_FILE", optarg, 1);
			break;
		case 'w':
			setenv("GLC_AUDIO_SKIP", "0", 1);
			break;
		case 'a':
			setenv("GLC_AUDIO", "0", 1);
			break;
		case 'q':
			setenv("GLC_SIGHANDLER", "1", 1);
			break;
		case 'g':
			setenv("GLC_CAPTURE_GLFINISH", "1", 1);
			break;
		case 'j':
			setenv("SDL_AUDIODRIVER", "alsa", 1);
			break;
		case 'b':
			setenv("GLC_CAPTURE", optarg, 1);
			break;
		case 'c':
			setenv("GLC_COMPRESSED_BUFFER_SIZE", optarg, 1);
			break;
		case 'u':
			setenv("GLC_UNCOMPRESSED_BUFFER_SIZE", optarg, 1);
			break;
		case 'd':
			setenv("GLC_UNSCALED_BUFFER_SIZE", optarg, 1);
			break;
		case 'h':
		default:
			goto usage;
		}
	}

	ld_preload_old = getenv("LD_PRELOAD");

	if (ld_preload_old != NULL) {
		ld_preload_len = strlen(ld_preload_old) + strlen(library) + 2;
		ld_preload = malloc(ld_preload_len);
		
		memcpy(ld_preload, ld_preload_old, strlen(ld_preload_old));
		memcpy(&ld_preload[strlen(ld_preload_old)+1], library, strlen(library));
		
		ld_preload[strlen(ld_preload_old)] = ':';
		ld_preload[ld_preload_len-1] = '\0';
	} else
		ld_preload = (char *) library;
	
	setenv("LD_PRELOAD", ld_preload, 1);

	if (optind >= argc)
		goto usage;

	program = argv[optind];
	program_args = &argv[optind];

	if (!posixly_correct_was_set)
		unsetenv("POSIXLY_CORRECT");
	
	if ((ret = execvp(program, program_args))) {
		fprintf(stderr, "can't execute \"%s", program);
		optind++;
		while (optind < argc)
			fprintf(stderr, " %s", argv[optind++]);
		fprintf(stderr, "\"\n");
		fprintf(stderr, "%s (%d)\n", strerror(ret), ret);
		return ret;
	}

	return EXIT_SUCCESS;
usage:
	printf("%s [capture option]... [application] [application argument]...\n", argv[0]);
	printf("  -o, --out=FILE             write to FILE, pid-%%d.glc by default\n"
	       "  -f, --fps=FPS              capture at FPS, default value is 30\n"
	       "  -r, --resize=FACTOR        resize pictures with scale factor FACTOR\n"
	       "  -y, --crop=WxH+X+Y         capture only [width]x[height][+[x][+[y]]]\n"
	       "  -s, --start                start capturing immediately\n"
	       "  -e, --colorspace=CSP       keep as 'bgr' or convert to '420jpeg'\n"
	       "                               default value is '420jpeg'\n"
	       "  -k, --hotkey=HOTKEY        capture hotkey, <Ctrl> and <Shift> modifiers are\n"
	       "                               supported, default hotkey is '<Shift>F8'\n"
	       "  -n, --lock-fps             lock fps when capturing\n"
	       "  -p, --no-pbo               don't try GL_ARB_pixel_buffer_object\n"
	       "  -z, --compression=METHOD   compress stream using METHOD\n"
	       "                               'none', 'quicklz' and 'lzo' are supported\n"
	       "                               'quicklz' is used by default\n"
	       "  -m, --byte-aligned         use GL_PACK_ALIGNMENT 1 instead of 8\n"
	       "  -i, --draw-indicator       draw indicator when capturing\n"
	       "                               indicator does not work with -b 'front'\n"
	       "  -v, --log=LEVEL            log >=LEVEL messages\n"
	       "                               0: errors\n"
	       "                               1: warnings\n"
	       "                               2: performance information\n"
	       "                               3: information\n"
	       "                               4: debug\n"
	       "  -l, --log-file=FILE        write log to FILE, pid-%%d.log by default\n"
	       "  -w, --no-audio-skip        always capture audio data\n"
	       "  -a, --disable-audio        don't capture audio\n"
	       "  -q, --sighandler           use custom signal handler\n"
	       "  -g, --glfinish             capture at glFinish()\n"
	       "  -j, --force-sdl-alsa-drv   force SDL to use ALSA audio driver\n"
	       "  -b, --capture=BUFFER       capture 'front' or 'back' buffer\n"
	       "                               default is 'front'\n"
	       "  -c, --compressed=SIZE      compressed stream buffer size in MiB\n"
	       "                               default is 50 MiB\n"
	       "  -u, --uncompressed=SIZE    uncompressed stream buffer size in MiB\n"
	       "                               default is 25 MiB\n"
	       "  -d, --unscaled=SIZE        unscaled picture stream buffer size in MiB,\n"
	       "                               default is 25 MiB\n"
	       "  -h, --help                 show this help\n");
	return EXIT_FAILURE;
}
