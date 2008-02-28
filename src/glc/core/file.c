/**
 * \file glc/core/file.c
 * \brief file io
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup file
 *  \{
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <packetstream.h>
#include <errno.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>

#include <glc/common/glc.h>
#include <glc/common/state.h>
#include <glc/common/core.h>
#include <glc/common/log.h>
#include <glc/common/thread.h>
#include <glc/common/util.h>

#include "file.h"

#define FILE_READING       0x1
#define FILE_WRITING       0x2
#define FILE_RUNNING       0x4
#define FILE_INFO_WRITTEN  0x8
#define FILE_INFO_READ    0x10
#define FILE_INFO_VALID   0x20

struct file_s {
	glc_t *glc;
	glc_flags_t flags;
	glc_thread_t thread;
	int fd;
};

void file_finish_callback(void *ptr, int err);
int file_read_callback(glc_thread_state_t *state);

int file_init(file_t *file, glc_t *glc)
{
	*file = malloc(sizeof(struct file_s));
	memset(*file, 0, sizeof(struct file_s));

	(*file)->glc = glc;
	(*file)->fd = -1;

	(*file)->thread.flags = GLC_THREAD_READ;
	(*file)->thread.ptr = *file;
	(*file)->thread.read_callback = &file_read_callback;
	(*file)->thread.finish_callback = &file_finish_callback;
	(*file)->thread.threads = 1;

	return 0;
}

int file_destroy(file_t file)
{
	free(file);
	return 0;
}

int file_open_target(file_t file, const char *filename)
{
	int fd, ret = 0;
	if (file->fd >= 0)
		return EBUSY;

	glc_log(file->glc, GLC_INFORMATION, "file",
		 "opening %s for writing stream", filename);

	fd = open(filename, O_CREAT | O_WRONLY | O_SYNC, 0644);

	if (fd == -1) {
		glc_log(file->glc, GLC_ERROR, "file", "can't open %s: %s (%d)",
			 filename, strerror(errno), errno);
		return errno;
	}

	if ((ret = file_set_target(file, fd)))
		close(fd);

	return ret;
}

int file_set_target(file_t file, int fd)
{
	if (file->fd >= 0)
		return EBUSY;

	if (flock(fd, LOCK_EX | LOCK_NB) == -1) {
		glc_log(file->glc, GLC_ERROR, "file",
			 "can't lock file: %s (%d)", strerror(errno), errno);
		return errno;
	}

	/* truncate file when we have locked it */
	lseek(file->fd, 0, SEEK_SET);
	ftruncate(file->fd, 0);

	file->fd = fd;
	file->flags |= FILE_WRITING;
	return 0;
}

int file_close_target(file_t file)
{
	if ((file->fd < 0) | (file->flags & FILE_RUNNING) |
	    (!(file->flags & FILE_WRITING)))
		return EAGAIN;

	/* try to remove lock */
	if (flock(file->fd, LOCK_UN) == -1)
		glc_log(file->glc, GLC_WARNING,
			 "file", "can't unlock file: %s (%d)",
			 strerror(errno), errno);

	if (close(file->fd))
		glc_log(file->glc, GLC_ERROR, "file",
			 "can't close file: %s (%d)",
			 strerror(errno), errno);

	file->fd = -1;
	file->flags &= ~(FILE_RUNNING | FILE_WRITING | FILE_INFO_WRITTEN);

	return 0;
}

int file_write_info(file_t file, glc_stream_info_t *info,
		    const char *info_name, const char *info_date)
{
	if ((file->fd < 0) | (file->flags & FILE_RUNNING) |
	    (!(file->flags & FILE_WRITING)))
		return EAGAIN;

	if (write(file->fd, info, GLC_STREAM_INFO_SIZE) != GLC_STREAM_INFO_SIZE)
		goto err;
	if (write(file->fd, info_name, info->name_size) != info->name_size)
		goto err;
	if (write(file->fd, info_date, info->date_size) != info->date_size)
		goto err;

	file->flags |= FILE_INFO_WRITTEN;
	return 0;
err:
	glc_log(file->glc, GLC_ERROR, "file",
		 "can't write stream information: %s (%d)",
		 strerror(errno), errno);
	return errno;
}

int file_write_process_start(file_t file, ps_buffer_t *from)
{
	int ret;
	if ((file->fd < 0) | (file->flags & FILE_RUNNING) |
	    (!(file->flags & FILE_WRITING)) |
	    (!(file->flags & FILE_INFO_WRITTEN)))
		return EAGAIN;

	if ((ret = glc_thread_create(file->glc, &file->thread, from, NULL)))
		return ret;
	/** \todo cancel buffer if this fails? */
	file->flags |= FILE_RUNNING;

	return 0;
}

int file_write_process_wait(file_t file)
{
	if ((file->fd < 0) | (!(file->flags & FILE_RUNNING)) |
	    (!(file->flags & FILE_WRITING)) |
	    (!(file->flags & FILE_INFO_WRITTEN)))
		return EAGAIN;

	glc_thread_wait(&file->thread);
	file->flags &= ~(FILE_RUNNING | FILE_INFO_WRITTEN);

	return 0;
}

void file_finish_callback(void *ptr, int err)
{
	file_t file = (file_t) ptr;

	if (err)
		glc_log(file->glc, GLC_ERROR, "file", "%s (%d)", strerror(err), err);
}

int file_read_callback(glc_thread_state_t *state)
{
	file_t file = (file_t) state->ptr;
	glc_container_message_t *container;
	glc_size_t glc_size;

	if (state->header.type == GLC_MESSAGE_CONTAINER) {
		container = (glc_container_message_t *) state->read_data;

		if (write(file->fd, &container->header, GLC_MESSAGE_HEADER_SIZE)
		    != GLC_MESSAGE_HEADER_SIZE)
			goto err;
		if (write(file->fd, &container->size, GLC_SIZE_SIZE) != GLC_SIZE_SIZE)
			goto err;
		if (write(file->fd, &state->read_data[GLC_CONTAINER_MESSAGE_SIZE], container->size)
		    != container->size)
			goto err;
	} else {
		if (write(file->fd, &state->header, GLC_MESSAGE_HEADER_SIZE)
		    != GLC_MESSAGE_HEADER_SIZE)
			goto err;
		glc_size = state->read_size;
		if (write(file->fd, &glc_size, GLC_SIZE_SIZE) != GLC_SIZE_SIZE)
			goto err;
		if (write(file->fd, state->read_data, state->read_size) != state->read_size)
			goto err;
	}

	return 0;

err:
	glc_log(file->glc, GLC_ERROR, "file", "%s (%d)", strerror(errno), errno);
	return errno;
}

int file_open_source(file_t file, const char *filename)
{
	int fd, ret = 0;
	if (file->fd >= 0)
		return EBUSY;

	glc_log(file->glc, GLC_INFORMATION, "file",
		 "opening %s for reading stream", filename);

	fd = open(filename, O_SYNC);

	if (fd == -1) {
		glc_log(file->glc, GLC_ERROR, "file", "can't open %s: %s (%d)",
			 filename, strerror(errno), errno);
		return errno;
	}

	if ((ret = file_set_source(file, fd)))
		close(fd);

	return ret;
}

int file_set_source(file_t file, int fd)
{
	if (file->fd >= 0)
		return EBUSY;

	/* seek to beginning */
	lseek(file->fd, 0, SEEK_SET);

	file->fd = fd;
	file->flags |= FILE_READING;
	return 0;
}

int file_close_source(file_t file)
{
	if ((file->fd < 0) | (!(file->flags & FILE_READING)))
		return EAGAIN;

	if (close(file->fd))
		glc_log(file->glc, GLC_ERROR, "file",
			 "can't close file: %s (%d)",
			 strerror(errno), errno);

	file->fd = -1;
	file->flags &= ~(FILE_READING | FILE_INFO_READ | FILE_INFO_VALID);

	return 0;	
}

int file_read_info(file_t file, glc_stream_info_t *info,
		   char **info_name, char **info_date)
{
	if ((file->fd < 0) | (!(file->flags & FILE_READING)))
		return EAGAIN;

	if (read(file->fd, info, GLC_STREAM_INFO_SIZE) != GLC_STREAM_INFO_SIZE) {
		glc_log(file->glc, GLC_ERROR, "file",
			 "can't read stream info header");
		return errno;
	}
	file->flags |= FILE_INFO_READ;

	if (info->signature != GLC_SIGNATURE) {
		glc_log(file->glc, GLC_ERROR, "file",
			 "signature 0x%08x does not match 0x%08x",
			 info->signature, GLC_SIGNATURE);
		return EINVAL;
	}

	if (info->version != GLC_STREAM_VERSION) {
		glc_log(file->glc, GLC_ERROR, "file",
			 "unsupported stream version 0x%02x (0x%02x is supported)",
			 info->version, GLC_STREAM_VERSION);
		return ENOTSUP;
	}

	if (info->name_size > 0) {
		*info_name = (char *) malloc(info->name_size);
		if (read(file->fd, *info_name, info->name_size) != info->name_size)
			return errno;
	}

	if (info->date_size > 0) {
		*info_date = (char *) malloc(info->date_size);
		if (read(file->fd, *info_date, info->date_size) != info->date_size)
			return errno;
	}

	file->flags |= FILE_INFO_VALID;
	return 0;
}

int file_read(file_t file, ps_buffer_t *to)
{
	int ret = 0;
	glc_message_header_t header;
	size_t packet_size = 0;
	ps_packet_t packet;
	char *dma;
	glc_size_t glc_ps;

	if ((file->fd < 0) | (!(file->flags & FILE_READING)))
		return EAGAIN;

	if (!(file->flags & FILE_INFO_READ)) {
		glc_log(file->glc, GLC_ERROR, "file",
			 "stream info header not read");
		return EAGAIN;
	}

	if (!(file->flags & FILE_INFO_VALID)) {
		glc_log(file->glc, GLC_ERROR, "file",
			 "stream info header not valid");
		file->flags &= ~FILE_INFO_READ;
		return EINVAL;
	}

	ps_packet_init(&packet, to);

	do {
		if (read(file->fd, &header, GLC_MESSAGE_HEADER_SIZE) != GLC_MESSAGE_HEADER_SIZE)
			goto send_eof;
		if (read(file->fd, &glc_ps, GLC_SIZE_SIZE) != GLC_SIZE_SIZE)
			goto send_eof;

		packet_size = glc_ps;

		if ((ret = ps_packet_open(&packet, PS_PACKET_WRITE)))
			goto err;
		if ((ret = ps_packet_write(&packet, &header, GLC_MESSAGE_HEADER_SIZE)))
			goto err;
		if ((ret = ps_packet_dma(&packet, (void *) &dma, packet_size, PS_ACCEPT_FAKE_DMA)))
			goto err;

		if (read(file->fd, dma, packet_size) != packet_size)
			goto read_fail;

		if ((ret = ps_packet_close(&packet)))
			goto err;
	} while ((header.type != GLC_MESSAGE_CLOSE) &&
		 (!glc_state_test(file->glc, GLC_STATE_CANCEL)));

finish:
	ps_packet_destroy(&packet);

	file->flags &= ~(FILE_INFO_READ | FILE_INFO_VALID);
	return 0;

send_eof:
	header.type = GLC_MESSAGE_CLOSE;
	ps_packet_open(&packet, PS_PACKET_WRITE);
	ps_packet_write(&packet, &header, GLC_MESSAGE_HEADER_SIZE);
	ps_packet_close(&packet);

	glc_log(file->glc, GLC_ERROR, "file", "unexpected EOF");
	goto finish;

read_fail:
	ret = EBADMSG;
err:
	if (ret == EINTR)
		goto finish; /* just cancel */

	glc_log(file->glc, GLC_ERROR, "file", "%s (%d)", strerror(ret), ret);
	glc_log(file->glc, GLC_DEBUG, "file", "packet size is %zd", packet_size);
	ps_buffer_cancel(to);

	file->flags &= ~(FILE_INFO_READ | FILE_INFO_VALID);
	return ret;
}

/**  \} */
