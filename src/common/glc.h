/**
 * \file src/common/glc.h
 * \brief glc types and structures
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 */

/* glc.h -- OpenGL video capture tool
  version 0.3.10, October 29th, 2007

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

#ifndef _GLC_H
#define _GLC_H

#include <sys/types.h>
#include <semaphore.h>

/**
 * \defgroup lib wrapper library
 * \defgroup stream stream processing
 * \defgroup common common utilities and data structures
 * \defgroup support optional support libraries
 */

/**
 * \addtogroup common
 *  \{
 */

/** always export this object  */
#define __PUBLIC __attribute__ ((visibility ("default")))
/** always hide this object */
#define __PRIVATE __attribute__ ((visibility ("hidden")))

/** stream version */
#define GLC_STREAM_VERSION              0x2
/** file signature = "GLC" */
#define GLC_SIGNATURE            0x00434c47

/* TODO better signal framework */
/** gl capture has finished */
#define GLC_SIGNAL_GL_CAPTURE_FINISHED    0
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
/** gl playback has finished */
#define GLC_SIGNAL_GL_PLAY_FINISHED      12
/** color correction filter has finished */
#define GLC_SIGNAL_COLOR_FINISHED        13
/** number of signals */
#define GLC_SIGNALS                      14

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
#define GLC_CAPTURE                     0x1
/** glc is cancelled */
#define GLC_CANCEL                      0x2
/** scaling (ycbcr or scale) is active */
#define GLC_SCALE                       0x4
/** capture from GL_BACK */
#define GLC_CAPTURE_BACK                0x8
/** capture from GL_FRONT */
#define GLC_CAPTURE_FRONT              0x10
/** draw indicator when capturing */
#define GLC_DRAW_INDICATOR             0x20
/** allow skipping audio capture if not ready */
#define GLC_AUDIO_ALLOW_SKIP           0x40
/** capture as BGRA frames, convert to BGR/Y'CbCr */
#define GLC_CAPTURE_BGRA               0x80
/** try GL_ARB_pixel_buffer_object */
#define GLC_TRY_PBO                   0x100
/** do colorspace conversion to Y'CbCr 420jpeg */
#define GLC_CONVERT_420JPEG           0x200
/** crop pictures */
#define GLC_CROP                      0x400
/** use GL_PACK_ALIGNMENT 8 for readback */
#define GLC_CAPTURE_DWORD_ALIGNED     0x800
/** compress stream with LZO */
#define GLC_COMPRESS_LZO             0x1000
/** compress stream with QuickLZ */
#define GLC_COMPRESS_QUICKLZ         0x2000
/** cap fps */
#define GLC_LOCK_FPS                 0x4000
/** enable log */
#define GLC_LOG                      0x8000
/** disable writing errors to stderr */
#define GLC_NOERR                   0x10000

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
	/** fps */
	double fps;
	/** flags */
	glc_flags_t flags;
	/** captured program pid */
	u_int32_t pid;
	/** size of captured program's name */
	u_int32_t name_size;
	/** size of date */
	u_int32_t date_size;
} glc_stream_info_t;
/** sizeof(glc_stream_info_t) */
#define GLC_STREAM_INFO_SIZE             32

/**
 * \brief global settings
 */
typedef struct {
	/** active flags */
	glc_flags_t flags;
	/** signals */
	sem_t signal[GLC_SIGNALS];
	/** stream file */
	char *stream_file;
	/** log file */
	char *log_file;
	/** log verbosity */
	int log_level;
	/** fps */
	double fps;
	/** scale for rescaling */
	double scale;
	/** playback/export silence threshold in microseconds */
	glc_utime_t silence_threshold;

	/** crop width */
	unsigned int crop_width;
	/** crop height */
	unsigned int crop_height;
	/** crop upper left corner x coordinate */
	unsigned int crop_x;
	/** crop upper left corner y coordinate */
	unsigned int crop_y;

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

	/** uncompressed data buffer size */
	size_t uncompressed_size;
	/** compressed data buffer size */
	size_t compressed_size;
} glc_t;

/** error */
#define GLC_ERROR                         0
/** warning */
#define GLC_WARNING                       1
/** performance information */
#define GLC_PERFORMANCE                   2
/** information */
#define GLC_INFORMATION                   3
/** debug */
#define GLC_DEBUG                         4

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
/** quicklz-compressed packet */
#define GLC_MESSAGE_QUICKLZ            0x07
/** color correction information */
#define GLC_MESSAGE_COLOR              0x08
/** plain container */
#define GLC_MESSAGE_CONTAINER          0x09

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
/** sizeof(glc_lzo_header_t) */
#define GLC_LZO_HEADER_SIZE               9

/**
 * \brief quicklz-compressed message header
 */
typedef struct {
	/** uncompressed data size */
	glc_size_t size;
	/** original message header */
	glc_message_header_t header;
} glc_quicklz_header_t;
/** sizeof(glc_quicklz_header_t) */
#define GLC_QUICKLZ_HEADER_SIZE           9

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
/** double-word aligned rows (GL_PACK_ALIGNMENT = 8) */
#define GLC_CTX_DWORD_ALIGNED            32

/**
 * \brief audio format message
 */
typedef struct {
	/** stream flags */
	glc_flags_t flags;
	/** audio stream number */
	glc_audio_i audio;
	/** rate */
	u_int32_t rate;
	/** number of channels */
	u_int32_t channels;
} glc_audio_format_message_t;
/** sizeof(glc_audio_format_message_t) */
#define GLC_AUDIO_FORMAT_MESSAGE_SIZE    16

/** interleaved */
#define GLC_AUDIO_INTERLEAVED             1
/** unknown/unsupported format */
#define GLC_AUDIO_FORMAT_UNKNOWN          2
/** signed 16bit little-endian */
#define GLC_AUDIO_S16_LE                  4
/** signed 24bit little-endian */
#define GLC_AUDIO_S24_LE                  8
/** signed 32bit little-endian */
#define GLC_AUDIO_S32_LE                 16

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

/**
 * \brief color correction information message
 */
typedef struct {
	/** context */
	glc_ctx_i ctx;
	/** brightness */
	float brightness;
	/** contrast */
	float contrast;
	/** red gamma */
	float red;
	/** green gamma */
	float green;
	/** blue gamma */
	float blue;
} glc_color_message_t;
/** sizeof(glc_color_message_t) */
#define GLC_COLOR_MESSAGE_SIZE           24

/**
 * \brief container message
 */
typedef struct {
	/** size */
	glc_size_t size;
	/** header */
	glc_message_header_t header;
} glc_container_message_t;
/** sizeof(glc_container_message_t) */
#define GLC_CONTAINER_MESSAGE_SIZE        9

/**  \} */

#endif
