/**
 * \file src/common/util.c
 * \brief utility functions
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup common
 *  \{
 * \defgroup util utility functions
 *  \{
 */

#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <packetstream.h>
#include <pthread.h>

#include "glc.h"
#include "util.h"

/**
 * \brief util private structure
 */
struct util_private_s {
	glc_t *glc;
	struct timeval init_time;
	glc_stime_t timediff;
	FILE *log_file;
	pthread_mutex_t log_mutex;

	glc_audio_i audio_streams;
	pthread_mutex_t audio_streams_mutex;
};

int util_app_name(char **path, u_int32_t *path_size);
int util_utc_date(char **date, u_int32_t *date_size);

void util_write_log_prefix(glc_t *glc, FILE *stream, int level, const char *module);

glc_utime_t util_real_time(glc_t *glc);

/**
 * \brief create glc_t
 *
 * Allocates new glc_t and initializes it.
 * \param glc returned glc
 * \return 0 on success otherwise an error code
 */
int glc_create(glc_t **glc)
{
	int i;
	*glc = (glc_t *) malloc(sizeof(glc_t));
	for (i = 0; i < GLC_SIGNALS; i++)
		sem_init(&(*glc)->signal[i], 0, 0);
	return 0;
}

/**
 * \brief destroy glc_t
 *
 * Destroy signals and free glc_t.
 * \param glc glc_t to destroy
 * \return 0 on success otherwise an error code
 */
int glc_destroy(glc_t *glc)
{
	int i;
	if (!glc)
		return EINVAL;
	for (i = 0; i < GLC_SIGNALS; i++)
		sem_destroy(&glc->signal[i]);
	free(glc);
	return 0;
}

/**
 * \brief initialize utilities
 *
 * Initializes utilities and sets global startup time.
 * \param glc glc to attach utilities to
 * \return 0 on success otherwise an error code
 */
int util_init(glc_t *glc)
{
	struct util_private_s *util = malloc(sizeof(struct util_private_s));
	memset(util, 0, sizeof(struct util_private_s));

	util->glc = glc;
	gettimeofday(&util->init_time, NULL);
	util->audio_streams = 0;
	pthread_mutex_init(&util->log_mutex, NULL);
	pthread_mutex_init(&util->audio_streams_mutex, NULL);

	glc->util = util;
	return 0;
}

/**
 * \brief free utilities
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
int util_free(glc_t *glc)
{
	struct util_private_s *util = (struct util_private_s *) glc->util;
	pthread_mutex_destroy(&util->log_mutex);
	pthread_mutex_destroy(&util->audio_streams_mutex);
	free(util);
	return 0;
}

/**
 * \brief current time in microseconds with timediff applied
 *
 * Time is absolute time - startup time - active time difference.
 * \see util_timediff()
 * \param glc glc
 * \return current relative time
 */
glc_utime_t util_time(glc_t *glc)
{
	return util_real_time(glc) - ((struct util_private_s *) glc->util)->timediff;
}

/**
 * \brief current time in microseconds without timediff
 * \param glc glc
 * \return time elapsed since initialization
 */
glc_utime_t util_real_time(glc_t *glc)
{
	struct util_private_s *util = (struct util_private_s *) glc->util;
	struct timeval tv;

	gettimeofday(&tv, NULL);

	tv.tv_sec -= util->init_time.tv_sec;
	tv.tv_usec -= util->init_time.tv_usec;

	if (tv.tv_usec < 0) {
		tv.tv_sec--;
		tv.tv_usec += 1000000;
	}

	return (glc_utime_t) (tv.tv_sec * 1000000 + (glc_utime_t) tv.tv_usec);
}

/**
 * \brief apply time difference
 *
 * Adds difference to active time difference.
 * \param glc glc
 * \param diff new time difference
 * \return 0 on success otherwise an error code
 */
int util_timediff(glc_t *glc, glc_stime_t diff)
{
	struct util_private_s *util = (struct util_private_s *) glc->util;
	util_log(glc, GLC_DEBUG, "util", "applying %ld usec time difference", diff);
	util->timediff += diff;
	return 0;
}

/**
 * \brief load stream information from file
 *
 * Loads stream information from file and initializes
 * global stream information structure (glc.info).
 * \param glc glc
 * \param filename where to read info from
 * \return 0 on success otherwise an error code
 */
int util_load_info(glc_t *glc, const char *filename)
{
	FILE *file = fopen(filename, "r");
	if (!file) {
		fprintf(stderr, "can't open %s: %s (%d)\n", filename, strerror(errno), errno);
		return ENOENT;
	}

	util_create_info(glc);
	if (fread(glc->info, 1, GLC_STREAM_INFO_SIZE, file) != GLC_STREAM_INFO_SIZE) {
		fprintf(stderr, "can't read stream information\n");
		return ENOSTR;
	}

	if (glc->info->signature != GLC_SIGNATURE) {
		fprintf(stderr, "signature does not match\n");
		fclose(file);
		return EINVAL;
	}

	if (glc->info->version != GLC_STREAM_VERSION) {
		fprintf(stderr, "unsupported stream version 0x%02x\n", glc->info->version);
		fclose(file);
		return ENOTSUP;
	}

	if (glc->info->name_size > 0) {
		glc->info_name = (char *) malloc(glc->info->name_size);
		fread(glc->info_name, 1, glc->info->name_size, file);
	}

	if (glc->info->date_size > 0) {
		glc->info_date = (char *) malloc(glc->info->date_size);
		fread(glc->info_date, 1, glc->info->date_size, file);
	}

	fclose(file);
	return 0;
}

/**
 * \brief allocate info structure
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
int util_create_info(glc_t *glc)
{
	glc->info = (glc_stream_info_t *) malloc(sizeof(glc_stream_info_t));
	memset(glc->info, 0, sizeof(glc_stream_info_t));
	return 0;
}

/**
 * \brief initialize info structure based on current capture environment
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
int util_init_info(glc_t *glc)
{
	glc->info->signature = GLC_SIGNATURE;
	glc->info->version = GLC_STREAM_VERSION;
	glc->info->flags = 0;
	glc->info->pid = getpid();
	glc->info->fps = glc->fps;
	util_app_name(&glc->info_name, &glc->info->name_size);
	util_utc_date(&glc->info_date, &glc->info->date_size);

	return 0;
}

/**
 * \brief free info structure
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
int util_free_info(glc_t *glc)
{
	if (glc->info_name)
		free(glc->info_name);
	if (glc->info_date)
		free(glc->info_date);
	if (glc->info)
		free(glc->info);
	return 0;
}

/**
 * \brief acquire application name
 *
 * Currently this function resolves /proc/self/exe.
 * \param path returned application name
 * \param path_size size of name string, including 0
 * \return 0 on success otherwise an error code
 */
int util_app_name(char **path, u_int32_t *path_size)
{
	*path = (char *) malloc(1024);
	ssize_t len;

	if ((len = readlink("/proc/self/exe", *path, 1023)) != -1) {
		(*path)[len] = '\0';
		*path_size = len;
	} else {
		*path_size = 0;
		(*path)[0] = '\0';
	}

	(*path_size)++;

	return 0;
}

/**
 * \brief acquire current date as UTC string
 * \param date returned date
 * \param date_size size of date string, including \0
 * \return 0 on success otherwise an error code
 */
int util_utc_date(char **date, u_int32_t *date_size)
{
	time_t t = time(NULL);
	char *strt = ctime(&t);

	if (strt[strlen(strt)-1] == '\n')
		strt[strlen(strt)-1] = '\0';

	*date_size = strlen(strt) + 1;
	*date = (char *) malloc(*date_size);
	memcpy(*date, strt, *date_size);

	return 0;
}

/**
 * \brief number of CPUs in system
 * \return number of online CPUs
 */
long int util_cpus()
{
	return sysconf(_SC_NPROCESSORS_ONLN);
}

/**
 * \brief write "end of stream" message to buffer
 * \param glc glc
 * \param to buffer where to write close message
 * \return 0 on success otherwise an error code
 */
int util_write_end_of_stream(glc_t *glc, ps_buffer_t *to)
{
	int ret = 0;
	ps_packet_t packet;
	glc_message_header_t header;
	header.type = GLC_MESSAGE_CLOSE;

	if ((ret = ps_packet_init(&packet, to)))
		goto finish;
	if ((ret = ps_packet_open(&packet, PS_PACKET_WRITE)))
		goto finish;
	if ((ret = ps_packet_write(&packet, &header, GLC_MESSAGE_HEADER_SIZE)))
		goto finish;
	if ((ret = ps_packet_close(&packet)))
		goto finish;
	if ((ret = ps_packet_destroy(&packet)))
		goto finish;

finish:
	return ret;
}

/**
 * \brief give unique audio stream
 * \param glc glc
 * \return unique audio stream number
 */
glc_audio_i util_audio_stream_id(glc_t *glc)
{
	struct util_private_s *util = glc->util;
	glc_audio_i id;

	pthread_mutex_lock(&util->audio_streams_mutex);
	id = ++util->audio_streams;
	pthread_mutex_unlock(&util->audio_streams_mutex);

	return id;
}

/**
 * \brief initialize log
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
int util_log_init(glc_t *glc)
{
	struct util_private_s *util = glc->util;

	if (!(util->log_file = fopen(glc->log_file, "w")))
		return errno;

	util_log(glc, GLC_INFORMATION, "util", "opened %s for log", glc->log_file);
	return 0;
}

/**
 * \brief write message to log
 *
 * Message is actually written to log if level is
 * lesser than, or equal to current log verbosity level and
 * logging is enabled.
 *
 * Errors are always written to stderr also.
 * \param glc glc
 * \param level message level
 * \param format passed to fprintf()
 * \param ... passed to fprintf()
 */
void util_log(glc_t *glc, int level, const char *module, const char *format, ...)
{
	struct util_private_s *util = glc->util;
	va_list ap;

	if (((!(glc->flags & GLC_LOG)) && (level > GLC_ERROR)) |
	    (level > glc->log_level))
		return;

	if ((level <= GLC_ERROR) &&
	    (!(glc->flags & GLC_NOERR))) {
		va_start(ap, format);

		util_write_log_prefix(glc, util->log_file, level, module);
		vfprintf(stderr, format, ap);
		fputc('\n', stderr);

		va_end(ap);
	}

	if (!(glc->flags & GLC_LOG))
		return;

	va_start(ap, format);

	/* this is highly threaded application and we want
	   non-corrupted logs */
	pthread_mutex_lock(&util->log_mutex);
	util_write_log_prefix(glc, util->log_file, level, module);
	vfprintf(util->log_file, format, ap);
	fputc('\n', util->log_file);

	pthread_mutex_unlock(&util->log_mutex);

	va_end(ap);
}

/**
 * \brief close log
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
int util_log_close(glc_t *glc)
{
	struct util_private_s *util = glc->util;

	if (!(glc->flags & GLC_LOG))
		return EINVAL;

	util_log(glc, GLC_INFORMATION, "util", "log closed");
	if (fclose(util->log_file))
		return errno;

	return 0;
}

/**
 * \brief write stream and system information to log
 * \param glc glc
 */
void util_log_info(glc_t *glc)
{
	struct util_private_s *util = glc->util;

	if ((!(glc->flags & GLC_LOG)) | (glc->log_level < GLC_INFORMATION))
		return;

	util_write_log_prefix(glc, util->log_file, GLC_INFORMATION, "util");
	fprintf(util->log_file, "system information");
	fprintf(util->log_file, "  processors  = %ld\n", util_cpus());

	util_write_log_prefix(glc, util->log_file, GLC_INFORMATION, "util");
	fprintf(util->log_file, "stream information\n");
	fprintf(util->log_file, "  signature   = 0x%08x\n", glc->info->signature);
	fprintf(util->log_file, "  version     = 0x%02x\n", glc->info->version);
	fprintf(util->log_file, "  flags       = %d\n", glc->info->flags);
	fprintf(util->log_file, "  fps         = %f\n", glc->info->fps);
	fprintf(util->log_file, "  pid         = %d\n", glc->info->pid);
	fprintf(util->log_file, "  name        = %s\n", glc->info_name);
	fprintf(util->log_file, "  date        = %s\n", glc->info_date);
}

void util_write_log_prefix(glc_t *glc, FILE *stream, int level, const char *module)
{
	const char *level_str = NULL;

	/* human-readable msg level */
	switch (level) {
		case GLC_ERROR:
			level_str = "error";
			break;
		case GLC_WARNING:
			level_str = "warning";
			break;
		case GLC_PERFORMANCE:
			level_str = "perf";
			break;
		case GLC_INFORMATION:
			level_str = "info";
			break;
		case GLC_DEBUG:
			level_str = "dbg";
			break;
		default:
			level_str = "unknown";
			break;
	}

	fprintf(stream, "[%7.2fs %10s %5s ] ",
		(double) util_real_time(glc) / 1000000.0, module, level_str);
}

/**  \} */
