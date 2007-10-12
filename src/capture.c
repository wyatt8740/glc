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

/**
 * \defgroup capture capture utility
 *  \{
 */

int main(int argc, char *argv[])
{
	char opt;
	int ret = 0;
	int option_index = 0;
	int posixly_correct_was_set = 0;
	char *program = NULL;
	char **program_args = NULL;
	const char *ld_preload_old = NULL;
	const char *library = "libglc-capture.so";
	char *ld_preload;
	size_t ld_preload_len;

	struct option long_options[] = {
		{"out",			1, NULL, 'o'},
		{"fps",			1, NULL, 'f'},
		{"resize",		1, NULL, 'r'},
		{"start",		0, NULL, 's'},
		{"colorspace",		1, NULL, 'e'},
		{"hotkey",		1, NULL, 'k'},
		{"no-pbo",		0, NULL, 'p'},
		{"disable-compression",	0, NULL, 'z'},
		{"bgra",		0, NULL, 'j'},
		{"disable-indicator",	0, NULL, 'n'},
		{"no-audio-skip",	0, NULL, 'w'},
		{"disable-audio",	0, NULL, 'a'},
		{"sighandler",		0, NULL, 'q'},
		{"glfinish",		0, NULL, 'g'},
		{"force-sdl-alsa-drv",	1, NULL, 'v'},
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

	while ((opt = getopt_long(argc, argv, "o:f:r:se:k:pzjnwaqgvb:c:u:d:h", long_options, &option_index)) != -1) {
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
		case 's':
			setenv("GLC_START", "1", 1);
			break;
		case 'e':
			setenv("GLC_COLORSPACE", optarg, 1);
			break;
		case 'k':
			setenv("GLC_HOTKEY", optarg, 1);
			break;
		case 'p':
			setenv("GLC_TRY_PBO", "0", 1);
			break;
		case 'z':
			setenv("GLC_COMPRESS", "0", 1);
			break;
		case 'j':
			setenv("GLC_CAPTURE_BGRA", "1", 1);
			break;
		case 'n':
			setenv("GLC_INDICATOR", "0", 1);
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
		case 'v':
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
			setenv("GLC_UNSCALED", optarg, 1);
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
	       "  -s, --start                start capturing immediately\n"
	       "  -e, --colorspace=CSP       keep as 'bgr' or convert to '420jpeg'\n"
	       "                               default value is '420jpeg'\n"
	       "  -k, --hotkey=HOTKEY        capture hotkey, <Ctrl> and <Shift> modifiers are\n"
	       "                               supported, default hotkey is '<Shift>F8'\n"
	       "  -p, --no-pbo               don't try GL_ARB_pixel_buffer_object\n"
	       "  -z, --disable-compression  don't compress stream\n"
	       "                               by default stream is compressed with LZO\n"
	       "  -j, --bgra                 capture as BGRA and convert to BGR\n"
	       "  -n, --disable-indicator    disable drawing indicator while capturing\n"
	       "  -w, --no-audio-skip        always capture audio data\n"
	       "  -a, --disable-audio        don't capture audio\n"
	       "  -q, --sighandler           use custom signal handler\n"
	       "  -g, --glfinish             capture at glFinish()\n"
	       "  -v, --force-sdl-alsa-drv   force SDL to use ALSA audio driver\n"
	       "  -b, --capture=BUFFER       capture 'front' or 'back' buffer\n"
	       "                               default is 'back'\n"
	       "  -c, --compressed=SIZE      compressed stream buffer size in MiB\n"
	       "                               default is 50 MiB\n"
	       "  -u, --uncompressed=SIZE    uncompressed stream buffer size in MiB\n"
	       "                               default is 10 MiB\n"
	       "  -d, --unscaled=SIZE        unscaled picture stream buffer size in MiB,\n"
	       "                               default is 10 MiB\n"
	       "  -h, --help                 show this help\n");
	return EXIT_FAILURE;
}

/**  \} */
