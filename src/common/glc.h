/**
 * \file src/common/glc.h
 * \brief glc types and structures
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* glc.h -- OpenGL video capture tool
  version 0.2.8, October 7th, 2007

  Copyright (C) 2007 Pyry Haulos

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

  Pyry Haulos <pyry.haulos@gmail.com>
*/

/*
 Changelog:

  0.2.8 (2007/10/07)
   some code documentation (will someone really dare to look at this
    code...)
   Worked around some signal problems by starting alsa & opengl when
    capturing really starts.

  0.2.7 (2007/10/07)
   Y'CbCr -> BGR conversion (rgb.c)
   Threading improvements, for example scale, ycbcr, rgb and unpack
    create one thread per CPU. glc should now scale nicely up to n
    CPUs.

  0.2.6 (2007/10/05)
   BGR -> Y'CbCr conversion target (ycbcr.c) with scaling support
    Set GLC_COLORSPACE=420jpeg to convert to Y'CbCr 420jpeg at capture
    time. Scaling is pretty cheap operation if colorspace conversion
    is enabled and vice versa.
   yuv4mpeg target
   export as yuv4mpeg stream, also to stdout
    f.ex. glc-play file.glc -o - -y 1 | mplayer -
   export one audio/picture stream
   export wav to stdout
    f.ex. glc-play file.glc -o - -a 1 | mplayer -demuxer 35 -
   new example script glc-play-mplayer
   modified glc-encode script to take advantage of pipes and yuv4mpeg
    export capability (mplayer's bgr24 -> yv12 is very ugly)
   cleaned up img.c and wav.c
   cleaned up glc-play, especially command line arguments
   read fps from file and use it for exporting pictures/video

  0.2.5 (2007/10/04)
   initialize pack & file processors when recording is actually started
    => no more empty stream files
   compression can be disabled by setting GLC_COMPRESS=0
   some audio threading hacks
    Hooked snd_pcm_...() calls shouldn't block anymore if target buffer
    is not ready. Of course this results skipping in captured stream.
    Setting GLC_AUDIO_SKIP=0 allows snd_pcm_...() calls to enter busy
    waiting state and thus ensures high-quality audio capture.
   use GL_ARB_pixel_buffer_object, if available, to asynchronously transfer
    pictures from GPU
    set GLC_TRY_PBO=0 to disable
   GLC_CAPTURE_BGRA=1 enables capturing BGRA frames and converting them
    to BGR in software at scale

  0.2.4 (2007/10/03)
   alsa hook bugfix

  0.2.3 (2007/10/03)
   reorganized wrapper library code
   start capturing immediately (GLC_START=1) or when specific key combination
    is pressed (GLC_HOTKEY="<Shift>F8")
   draw simple indicator at upper left corner when recording
    set GLC_INDICATOR=0 to disable
    indicator is not included in the recorded stream

  0.2.2 (2007/10/02)
   unhook only alsa modules
   audio playback support
   improved source tree layout and Makefile

  0.2.1 (2007/10/01)
   ALSA capture redone, should be more compatible and much simplier
   GLC_AUDIO=0 environment variable disables audio capture
   custom signal handler to ensure that lib_close() is called at exit
    set GLC_SIGHANDLER=0 to disable
   non-interleaved audio stream support in (wav.c)
   unhook all alsa libraries => no audio stream duplication

  0.2.0 (2007/09/30)
   ALSA audio capture support (audio.c)
    needs ALSA file plugin
   wav target (wav.c)

  0.1.4 (2007/09/29)
   thread.c cleanups, utilize the new and improved fake dma implementation in
    packetstream 0.0.11

  0.1.3 (2007/09/23)
   stream format improvements

  0.1.2 (2007/09/22)
   generic stream processor thread (thread.c)
   use the new thread model where possible
    (gl_show, img, scale, info, pack, file)
    => much cleaner code and better error handling
   img has now better multictx support

  0.1.1 (2007/09/21)
   improved info
   support for applications that have multiple drawables,
    even multithreaded

  0.1.0 (2007/09/21)
   architechture improvements (= another massive cleanup)
   scaler has now its own thread (scale.c)
   primitive info target (info.c)
   deprecated environment variables:
     GLC_STATS
     GLC_UBUF
     GLC_CBUF
   new environment variables:
     GLC_CAPTURED_BUFFER_SIZE
     GLC_SCALED_BUFFER_SIZE
     GLC_COMPRESSED_BUFFER_SIZE

  0.0.10 (2007/09/20)
   support for capturing at glFinish() (GLC_CAPTURE_GLFINISH)
    for example compiz needs this
   some additional cleanups

  0.0.9 (2007/09/20)
   massive cleanups (nuked globals where possible and
    free memory at exit)
   fixed scaler segfault at resize

  0.0.8 (2007/09/20)
   bilinear filtering support for scaling with ratio != 0.5
   adjustable capture buffer (GLC_CAPTURE)
   ajdustable uncompressed and compressed stream buffer
    size (GLC_UBUF and GLC_CBUF)

  0.0.7 (2007/09/18)
   BMP bugfixes

  0.0.6 (2007/09/17)
   minilzo support
   support for exporting frames as Win 3.x BGR BMP images
   example encoder.sh script for encoding exported BMP images to
    x264-encoded video
   configurable stats (GLC_STATS)

  0.0.5 (2007/09/15)
   initial arbitrary ratio (< 1.0) scaler
   use 'elfhacks' to hook dlsym()

  0.0.4 (2007/09/14)
   scaler improvements (GLC_SCALE)
   use PS_PACKET_TRY, insufficient disk performance shouldn't affect
    running application any more (unless it is also waiting for disk)

  0.0.3 (2007/09/10)
   adjustable capture fps and stream file (GLC_FILE and GLC_FPS)
   initial scaling support (GLC_RESIZE)

  0.0.2 (2007/09/10)
   initial release
*/

#ifndef _GLC_H
#define _GLC_H

#include <sys/types.h>
#include <semaphore.h>

/**
 * \defgroup lib wrapper library
 * \defgroup stream stream processing
 * \defgroup common common utilities and data structures
 */

/**
 * \addtogroup common
 *  \{
 */

/** stream version */
#define GLC_STREAM_VERSION              0x1
/** file signature = "GLC\0" */
#define GLC_SIGNATURE            0x00434c47

/* TODO better signal framework */
/** gl capture/playback has finished */
#define GLC_SIGNAL_GL_FINISHED            0
/** pack/unpack has finished */
#define GLC_SIGNAL_PACK_FINISHED          1
/** file has finished */
#define GLC_SIGNAL_FILE_FINISHED          2
/** img has finished */
#define GLC_SIGNAL_IMG_FINISHED           3
/** scale has finished */
#define GLC_SIGNAL_SCALE_FINISHED         4
/** info has finished */
#define GLC_SIGNAL_INFO_FINISHED          5
/** audio has finished */
#define GLC_SIGNAL_AUDIO_FINISHED         6
/** wav has finished */
#define GLC_SIGNAL_WAV_FINISHED           7
/** demux has finished */
#define GLC_SIGNAL_DEMUX_FINISHED         8
/** ycbcr has finished */
#define GLC_SIGNAL_YCBCR_FINISHED         9
/** yuv4mpeg has finished */
#define GLC_SIGNAL_YUV4MPEG_FINISHED     10
/** rgb has finished */
#define GLC_SIGNAL_RGB_FINISHED          11
/** number of signals */
#define GLC_SIGNALS                      12

/** unsigned time in microseconds */
typedef u_int64_t glc_utime_t;
/** signed time in microseconds */
typedef int64_t glc_stime_t;

/** picture context number */
typedef int32_t glc_ctx_i;
/** audio stream number */
typedef int32_t glc_audio_i;
/** size, used in stream to ensure compability */
typedef u_int64_t glc_size_t;
/** sizeof(glc_size_t) */
#define GLC_SIZE_SIZE                     8

/** flags */
typedef u_int32_t glc_flags_t;

/** glc is capturing */
#define GLC_CAPTURE                       1
/** glc is cancelled */
#define GLC_CANCEL                        2
/** scaling (ycbcr or scale) is active */
#define GLC_SCALE                         4
/** capture from GL_BACK */
#define GLC_CAPTURE_BACK                  8
/** capture from GL_FRONT */
#define GLC_CAPTURE_FRONT                16
/** draw indicator when capturing */
#define GLC_DRAW_INDICATOR               32
/** allow skipping audio capture if not ready */
#define GLC_AUDIO_ALLOW_SKIP             64
/** capture as BGRA frames, convert to BGR/Y'CbCr */
#define GLC_CAPTURE_BGRA                128
/** try GL_ARB_pixel_buffer_object */
#define GLC_TRY_PBO                     256
/** do colorspace conversion to Y'CbCr 420jpeg */
#define GLC_CONVERT_420JPEG             512

/**
 * \brief stream info structure
 *
 * Each glc stream file should start with stream info
 * structure. [name_size + date_size] sized data area should
 * follow stream info:
 *
 * First [name_size] bytes contain null-terminated application
 * path string. [date_size] bytes starting at [name_size]
 * contain null-terminated date string in UTC format.
 */
typedef struct {
	/** file signature */
	u_int32_t signature;
	/** stream version */
	u_int32_t version;
	/** flags */
	glc_flags_t flags;
	/** fps */
	u_int32_t fps;
	/** captured program pid */
	u_int32_t pid;
	/** size of captured program's name */
	u_int32_t name_size;
	/** size of date */
	u_int32_t date_size;
} glc_stream_info_t;
/** sizeof(glc_stream_info_t) */
#define GLC_STREAM_INFO_SIZE             28

/**
 * \brief global settings
 */
typedef struct {
	/** active flags */
	glc_flags_t flags;
	/** signals */
	sem_t signal[GLC_SIGNALS];
	/** stream file path */
	char *stream_file;
	/** fps */
	int fps;
	/** scale for rescaling */
	double scale;
	/** util uses this to store internal state */
	void *util;

	/** filename format for exporting wav/pictures/yuv4mpeg */
	const char *filename_format;
	/** exported audio stream number */
	glc_audio_i export_audio;
	/** exported video stream number */
	glc_ctx_i export_ctx;
	/** info level */
	int info_level;

	/** stream info structure */
	glc_stream_info_t *info;
	/** captured program's name */
	char *info_name;
	/** date */
	char *info_date;
} glc_t;

/** stream message type */
typedef char glc_message_type_t;
/** end of stream */
#define GLC_MESSAGE_CLOSE              0x01
/** picture */
#define GLC_MESSAGE_PICTURE            0x02
/** picture context message */
#define GLC_MESSAGE_CTX                0x03
/** lzo-compressed packet */
#define GLC_MESSAGE_LZO                0x04
/** audio format message */
#define GLC_MESSAGE_AUDIO_FORMAT       0x05
/** audio data message */
#define GLC_MESSAGE_AUDIO              0x06

/**
 * \brief stream message header
 */
typedef struct {
	/** stream message type */
	glc_message_type_t type;
} glc_message_header_t;
/** sizeof(glc_message_header_t) */
#define GLC_MESSAGE_HEADER_SIZE           1

/**
 * \brief lzo-compressed message header
 */
typedef struct {
	/** uncompressed data size */
	glc_size_t size;
	/** original message header */
	glc_message_header_t header;
} glc_lzo_header_t;
/** sizeof(glc_lzo_header_size) */
#define GLC_LZO_HEADER_SIZE               9

/**
 * \brief picture header
 */
typedef struct {
	/** time */
	glc_utime_t timestamp;
	/** picture context number */
	glc_ctx_i ctx;
} glc_picture_header_t;
/** sizeof(glc_picture_header_size) */
#define GLC_PICTURE_HEADER_SIZE          12

/**
 * \brief picture context message
 */
typedef struct {
	/** context flags */
	glc_flags_t flags;
	/** context number */
	glc_ctx_i ctx;
	/** width */
	u_int32_t w;
	/** height */
	u_int32_t h;
} glc_ctx_message_t;
/** sizeof(glc_ctx_message_t) */
#define GLC_CTX_MESSAGE_SIZE             16

/** create context */
#define GLC_CTX_CREATE                    1
/** update existing context */
#define GLC_CTX_UPDATE                    2
/** 24bit BGR, last row first */
#define GLC_CTX_BGR                       4
/** 32bit BGRA, last row first */
#define GLC_CTX_BGRA                      8
/** planar YV12 420jpeg */
#define GLC_CTX_YCBCR_420JPEG            16

/** audio format */
typedef u_int32_t glc_audio_format_t;
/** unknown/unsupported format */
#define GLC_AUDIO_FORMAT_UNKNOWN          1
/** signed 16bit little-endian */
#define GLC_AUDIO_FORMAT_S16_LE           2
/** signed 24bit little-endian */
#define GLC_AUDIO_FORMAT_S24_LE           3
/** signed 32bit little-endian */
#define GLC_AUDIO_FORMAT_S32_LE           4

/**
 * \brief audio format message
 */
typedef struct {
	/** audio stream number */
	glc_audio_i audio;
	/** stream format */
	glc_audio_format_t format;
	/** rate */
	u_int32_t rate;
	/** number of channels */
	u_int32_t channels;
	/** 0 = non-interleaved, 1 = interleaved */
	u_int32_t interleaved;
} glc_audio_format_message_t;
/** sizeof(glc_audio_format_message_t) */
#define GLC_AUDIO_FORMAT_MESSAGE_SIZE    24

/**
 * \brief audio data message header
 */
typedef struct {
	/** time */
	glc_utime_t timestamp;
	/** data size */
	glc_size_t size;
	/** audio stream number */
	glc_audio_i audio;
} glc_audio_header_t;
/** sizeof(glc_audio_header_t) */
#define GLC_AUDIO_HEADER_SIZE            20

/**  \} */

#endif
