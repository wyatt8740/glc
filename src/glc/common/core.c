/**
 * \file glc/common/core.c
 * \brief glc core
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup common_core
 *  \{
 */

#include <stdlib.h>
#include <sys/time.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "glc.h"
#include "core.h"
#include "log.h"
#include "util.h"

struct glc_core_s {
	struct timeval init_time;
	long int threads_hint;
};

int glc_init(glc_t *glc)
{
	int ret = 0;

	/* clear 'em */
	glc->core = NULL;
	glc->state = NULL;
	glc->util = NULL;
	glc->log = NULL;

	glc->core = (glc_core_t) malloc(sizeof(struct glc_core_s));
	memset(glc->core, 0, sizeof(struct glc_core_s));

	gettimeofday(&glc->core->init_time, NULL);
	glc->core->threads_hint = sysconf(_SC_NPROCESSORS_ONLN);

	if ((ret = glc_log_init(glc)))
		return ret;
	if ((ret = glc_util_init(glc)))
		return ret;

	return 0;
}

int glc_destroy(glc_t *glc)
{
	glc_util_destroy(glc);
	glc_log_destroy(glc);

	free(glc->core);

	/* and clear */
	glc->core = NULL;
	glc->state = NULL;
	glc->util = NULL;
	glc->log = NULL;

	return 0;
}

glc_utime_t glc_time(glc_t *glc)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);

	tv.tv_sec -= glc->core->init_time.tv_sec;
	tv.tv_usec -= glc->core->init_time.tv_usec;

	if (tv.tv_usec < 0) {
		tv.tv_sec--;
		tv.tv_usec += 1000000;
	}

	return (glc_utime_t) (tv.tv_sec * 1000000 + (glc_utime_t) tv.tv_usec);
}

long int glc_threads_hint(glc_t *glc)
{
	return glc->core->threads_hint;
}

int glc_set_threads_hint(glc_t *glc, long int count)
{
	if (count <= 0)
		return EINVAL;
	glc->core->threads_hint = count;
	return 0;
}

/**  \} */
