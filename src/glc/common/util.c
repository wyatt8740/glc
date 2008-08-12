/**
 * \file glc/common/util.c
 * \brief utility functions
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup util
 *  \{
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#include "glc.h"
#include "core.h"
#include "log.h"
#include "util.h"

/**
 * \brief util private structure
 */
struct glc_util_s {
	double fps;
	int pid;
};

/**
 * \brief acquire application name
 *
 * Currently this function resolves /proc/self/exe.
 * \param glc glc
 * \param path returned application name
 * \param path_size size of name string, including 0
 * \return 0 on success otherwise an error code
 */
int glc_util_app_name(glc_t *glc, char **path, u_int32_t *path_size);

/**
 * \brief acquire current date as UTC string
 * \param glc glc
 * \param date returned date
 * \param date_size size of date string, including 0
 * \return 0 on success otherwise an error code
 */
int glc_util_utc_date(glc_t *glc, char **date, u_int32_t *date_size);

int glc_util_init(glc_t *glc)
{
	glc->util = (glc_util_t) malloc(sizeof(struct glc_util_s));
	memset(glc->util, 0, sizeof(struct glc_util_s));

	glc->util->fps = 30;
	glc->util->pid = getpid();

	return 0;
}

int glc_util_destroy(glc_t *glc)
{
	free(glc->util);
	return 0;
}

int glc_util_info_fps(glc_t *glc, double fps)
{
	glc->util->fps = fps;
	return 0;
}

int glc_util_info_create(glc_t *glc, glc_stream_info_t **stream_info,
			 char **info_name, char **info_date)
{
	*stream_info = (glc_stream_info_t *) malloc(sizeof(glc_stream_info_t));
	memset(*stream_info, 0, sizeof(glc_stream_info_t));

	(*stream_info)->signature = GLC_SIGNATURE;
	(*stream_info)->version = GLC_STREAM_VERSION;
	(*stream_info)->flags = 0;
	(*stream_info)->pid = glc->util->pid;
	(*stream_info)->fps = glc->util->fps;

	glc_util_app_name(glc, info_name, &(*stream_info)->name_size);
	glc_util_utc_date(glc, info_date, &(*stream_info)->date_size);

	return 0;
}

int glc_util_app_name(glc_t *glc, char **path, u_int32_t *path_size)
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

int glc_util_utc_date(glc_t *glc, char **date, u_int32_t *date_size)
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

int glc_util_write_end_of_stream(glc_t *glc, ps_buffer_t *to)
{
	int ret = 0;
	ps_packet_t packet;
	glc_message_header_t header;
	header.type = GLC_MESSAGE_CLOSE;

	if ((ret = ps_packet_init(&packet, to)))
		goto finish;
	if ((ret = ps_packet_open(&packet, PS_PACKET_WRITE)))
		goto finish;
	if ((ret = ps_packet_write(&packet, &header, sizeof(glc_message_header_t))))
		goto finish;
	if ((ret = ps_packet_close(&packet)))
		goto finish;
	if ((ret = ps_packet_destroy(&packet)))
		goto finish;

finish:
	return ret;
}

int glc_util_log_info(glc_t *glc)
{
	char *name, *date;
	u_int32_t unused;
	glc_util_app_name(glc, &name, &unused);
	glc_util_utc_date(glc, &date, &unused);

	glc_log(glc, GLC_INFORMATION, "util", "system information\n" \
		"  threads hint = %ld", glc_threads_hint(glc));

	glc_log(glc, GLC_INFORMATION, "util", "stream information\n" \
		"  signature    = 0x%08x\n" \
		"  version      = 0x%02x\n" \
		"  flags        = %d\n" \
		"  fps          = %f\n" \
		"  pid          = %d\n" \
		"  name         = %s\n" \
		"  date         = %s",
		GLC_SIGNATURE, GLC_STREAM_VERSION, 0, glc->util->fps,
		glc->util->pid, name, date);

	free(name);
	free(date);

	return 0;
}

int glc_util_log_version(glc_t *glc)
{
	glc_log(glc, GLC_INFORMATION, "util",
		"version %s", GLC_VERSION);
	glc_log(glc, GLC_DEBUG, "util",
		"%s %s, %s", __DATE__, __TIME__, __VERSION__);
	return 0;
}

char *glc_util_str_replace(const char *str, const char *find, const char *replace)
{
	/* calculate destination string size */
	size_t replace_len = strlen(replace);
	size_t find_len = strlen(find);
	ssize_t copy, add_per_replace = (ssize_t) replace_len - (ssize_t) find_len;
	ssize_t size = strlen(str) + 1;
	const char* p = str;
	while ((p = strstr(p, find)) != NULL) {
		size += add_per_replace;
		p = &p[find_len];
	}

	if (size < 0)
		return NULL;

	char *result = (char *) malloc(size * sizeof(char));
	p = str;
	const char *s = str;
	char *r = result;

	while ((p = strstr(p, find)) != NULL) {
		copy = (size_t) p - (size_t) s; /* naughty casts */

		/* copy string before previous replace (or start) */
		if (copy > 0) {
			memcpy(r, s, copy * sizeof(char));
			r = &r[copy];
		}

		/* copy replace string */
		memcpy(r, replace, replace_len * sizeof(char));
		r = &r[replace_len];

		p = &p[find_len];
		s = p;
	}

	copy = (size_t) p - (size_t) s;
	if (copy > 0)
		memcpy(result, s, copy * sizeof(char));

	result[size-1] = '\0';

	return result;
}

/**  \} */
