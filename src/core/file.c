/**
 * \file src/core/file.c
 * \brief file io
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup core
 *  \{
 * \defgroup file file io
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

#include "../common/glc.h"
#include "../common/thread.h"
#include "../common/util.h"
#include "file.h"

struct file_private_s {
	glc_t *glc;
	glc_thread_t thread;
	int fd;
};

void file_finish_callback(void *ptr, int err);
int file_read_callback(glc_thread_state_t *state);

void *file_init(glc_t *glc, ps_buffer_t *from)
{
	struct file_private_s *file;

	file = malloc(sizeof(struct file_private_s));
	memset(file, 0, sizeof(struct file_private_s));

	file->glc = glc;

	util_log(file->glc, GLC_INFORMATION, "file",
		 "opening %s for stream", file->glc->stream_file);

	file->fd = open(file->glc->stream_file,
			O_CREAT | O_WRONLY | O_TRUNC | O_SYNC, 0644);

	if (file->fd == -1) {
		util_log(file->glc, GLC_ERROR, "file", "can't open %s: %s (%d)",
			 file->glc->stream_file, strerror(errno), errno);
		goto cancel;
	}

	if (flock(file->fd, LOCK_EX | LOCK_NB) == -1) {
		util_log(file->glc, GLC_ERROR, "file", "can't lock %s: %s (%d)",
			 file->glc->stream_file, strerror(errno), errno);
		goto cancel;
	}

	if (!file->glc->info) {
		util_log(file->glc, GLC_ERROR, "file", "stream info not available");
		goto cancel;
	}

	write(file->fd, file->glc->info, GLC_STREAM_INFO_SIZE);
	write(file->fd, file->glc->info_name, file->glc->info->name_size);
	write(file->fd, file->glc->info_date, file->glc->info->date_size);

	file->thread.flags = GLC_THREAD_READ;
	file->thread.ptr = file;
	file->thread.read_callback = &file_read_callback;
	file->thread.finish_callback = &file_finish_callback;
	file->thread.threads = 1;

	if (glc_thread_create(glc, &file->thread, from, NULL))
		return NULL;

	return file;

cancel:
	file->glc->flags |= GLC_CANCEL;
	ps_buffer_cancel(from);
	return NULL;
}

int file_wait(void *filepriv)
{
	struct file_private_s *file = filepriv;

	glc_thread_wait(&file->thread);
	free(file);

	return 0;
}

void file_finish_callback(void *ptr, int err)
{
	struct file_private_s *file = (struct file_private_s *) ptr;

	if (err)
		util_log(file->glc, GLC_ERROR, "file", "%s (%d)", strerror(err), err);

	/* try to remove lock */
	if (flock(file->fd, LOCK_UN) == -1)
		util_log(file->glc, GLC_WARNING, "file", "can't unlock file: %s (%d)",
			 strerror(errno), errno);

	if (close(file->fd))
		util_log(file->glc, GLC_ERROR, "file",
			 "can't close file: %s (%d)", strerror(errno), errno);
}

int file_read_callback(glc_thread_state_t *state)
{
	struct file_private_s *file = (struct file_private_s *) state->ptr;
	glc_container_message_t *container;
	glc_size_t glc_size;

	if (state->header.type == GLC_MESSAGE_CONTAINER) {
		container = (glc_container_message_t *) state->read_data;

		if (write(file->fd, &container->header, GLC_MESSAGE_HEADER_SIZE)
		    != GLC_MESSAGE_HEADER_SIZE)
			return ENOSTR;
		if (write(file->fd, &container->size, GLC_SIZE_SIZE) != GLC_SIZE_SIZE)
			return ENOSTR;
		if (write(file->fd, &state->read_data[GLC_CONTAINER_MESSAGE_SIZE], container->size)
		    != container->size)
			return ENOSTR;
	} else {
		if (write(file->fd, &state->header, GLC_MESSAGE_HEADER_SIZE) != GLC_MESSAGE_HEADER_SIZE)
			return ENOSTR;
		glc_size = state->read_size;
		if (write(file->fd, &glc_size, GLC_SIZE_SIZE) != GLC_SIZE_SIZE)
			return ENOSTR;
		if (write(file->fd, state->read_data, state->read_size) != state->read_size)
			return ENOSTR;
	}

	return 0;
}

int file_read(glc_t *glc, ps_buffer_t *to)
{
	int fd, ret = 0;
	glc_stream_info_t *info;
	glc_message_header_t header;
	size_t packet_size = 0;
	ps_packet_t packet;
	char *dma;
	glc_size_t glc_ps;

	fd = open(glc->stream_file, O_SYNC);
	if (!fd) {
		util_log(glc, GLC_ERROR, "file",
			 "can't open %s: %s (%d)", glc->stream_file, strerror(errno), errno);
		return -1;
	}

	info = (glc_stream_info_t *) malloc(sizeof(glc_stream_info_t));
	memset(info, 0, sizeof(glc_stream_info_t));
	read(fd, info, GLC_STREAM_INFO_SIZE);

	if (info->signature != GLC_SIGNATURE) {
		util_log(glc, GLC_ERROR, "file", "signature does not match");
		goto err;
	}

	if (info->version != GLC_STREAM_VERSION) {
		util_log(glc, GLC_ERROR, "file",
			 "unsupported stream version 0x%02x", glc->info->version);
		goto err;
	}

	if (info->name_size > 0)
		lseek(fd, info->name_size, SEEK_CUR);
	if (info->date_size > 0)
		lseek(fd, info->date_size, SEEK_CUR);

	free(info);

	ps_packet_init(&packet, to);

	do {
		if (read(fd, &header, GLC_MESSAGE_HEADER_SIZE) != GLC_MESSAGE_HEADER_SIZE)
			goto send_eof;
		if (read(fd, &glc_ps, GLC_SIZE_SIZE) != GLC_SIZE_SIZE)
			goto send_eof;

		packet_size = glc_ps;

		if ((ret = ps_packet_open(&packet, PS_PACKET_WRITE)))
			goto err;
		if ((ret = ps_packet_write(&packet, &header, GLC_MESSAGE_HEADER_SIZE)))
			goto err;
		if ((ret = ps_packet_dma(&packet, (void *) &dma, packet_size, PS_ACCEPT_FAKE_DMA)))
			goto err;

		if (read(fd, dma, packet_size) != packet_size)
			goto read_fail;

		if ((ret = ps_packet_close(&packet)))
			goto err;
	} while ((header.type != GLC_MESSAGE_CLOSE) && (!(glc->flags & GLC_CANCEL)));

finish:
	ps_packet_destroy(&packet);
	close(fd);

	return 0;

send_eof:
	header.type = GLC_MESSAGE_CLOSE;
	ps_packet_open(&packet, PS_PACKET_WRITE);
	ps_packet_write(&packet, &header, GLC_MESSAGE_HEADER_SIZE);
	ps_packet_close(&packet);

	util_log(glc, GLC_ERROR, "file", "unexpected EOF");
	goto finish;

read_fail:
	ret = EBADMSG;
err:
	if (ret == EINTR)
		goto finish; /* just cancel */

	util_log(glc, GLC_ERROR, "file", "%s (%d)", strerror(ret), ret);
	util_log(glc, GLC_DEBUG, "file", "packet size is %zd", packet_size);
	close(fd);
	ps_buffer_cancel(to);
	return ret;
}

/**  \} */
/**  \} */
