/**
 * \file src/common/thread.c
 * \brief generic stream processor thread
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup common
 *  \{
 * \defgroup thread generic thread
 *  \{
 */

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <packetstream.h>
#include <errno.h>

#include "glc.h"
#include "thread.h"
#include "util.h"

/**
 * \brief thread private variables
 */
struct glc_thread_private_s {
	glc_t *glc;
	ps_buffer_t *from;
	ps_buffer_t *to;

	pthread_t *pthread_thread;
	pthread_mutex_t open, finish;

	glc_thread_t *thread;

	int stop;
	int ret;
};

void *glc_thread(void *argptr);

/**
 * \brief create thread
 *
 * Creates thread.threads threads (glc_thread()).
 * \param glc glc
 * \param thread thread information structure
 * \param from buffer where data is read from
 * \param to buffer where data is written to
 * \return 0 on success otherwise an error code
 */
int glc_thread_create(glc_t *glc, glc_thread_t *thread, ps_buffer_t *from, ps_buffer_t *to)
{
	int ret;
	struct glc_thread_private_s *private;
	size_t t;

	if (thread->threads < 1)
		return EINVAL;

	if (!(private = (struct glc_thread_private_s *) malloc(sizeof(struct glc_thread_private_s))))
		return ENOMEM;
	memset(private, 0, sizeof(struct glc_thread_private_s));

	private->glc = glc;
	private->from = from;
	private->to = to;
	private->thread = thread;

	pthread_mutex_init(&private->open, NULL);
	pthread_mutex_init(&private->finish, NULL);

	private->pthread_thread = malloc(sizeof(pthread_t) * thread->threads);
	for (t = 0; t < thread->threads; t++) {
		if ((ret = pthread_create(&private->pthread_thread[t], NULL, glc_thread, private)))
			return ret;
	}
	return 0;
}

/**
 * \brief thread loop
 *
 * Actual reading, writing and calling callbacks is
 * done here.
 * \param argptr pointer to thread state structure
 * \return always NULL
 */
void *glc_thread(void *argptr)
{
	int has_locked, ret, write_size_set;

	struct glc_thread_private_s *private = (struct glc_thread_private_s *) argptr;
	glc_thread_t *thread = private->thread;
	glc_thread_state_t state;

	ps_packet_t read, write;

	write_size_set = ret = has_locked = 0;
	state.flags = state.read_size = state.write_size = 0;
	state.ptr = thread->ptr;

	if (thread->flags & GLC_THREAD_READ) {
		if ((ret = ps_packet_init(&read, private->from)))
			goto err;
	}

	if (thread->flags & GLC_THREAD_WRITE) {
		if ((ps_packet_init(&write, private->to)))
			goto err;
	}

	/* create callback */
	if (thread->thread_create_callback) {
		if ((ret = thread->thread_create_callback(state.ptr, &state.threadptr)))
			goto err;
	}

	do {
		/* open callback */
		if (thread->open_callback) {
			if ((ret = thread->open_callback(&state)))
				goto err;
		}

		if ((thread->flags & GLC_THREAD_WRITE) && (thread->flags & GLC_THREAD_READ)) {
			pthread_mutex_lock(&private->open); /* preserve packet order */
			has_locked = 1;
		}

		if ((thread->flags & GLC_THREAD_READ) && (!(state.flags & GLC_THREAD_STATE_SKIP_READ))) {
			if ((ret = ps_packet_open(&read, PS_PACKET_READ)))
				goto err;
			if ((ret = ps_packet_read(&read, &state.header, GLC_MESSAGE_HEADER_SIZE)))
				goto err;
			if ((ret = ps_packet_getsize(&read, &state.read_size)))
				goto err;
			state.read_size -= GLC_MESSAGE_HEADER_SIZE;
			state.write_size = state.read_size;

			/* header callback */
			if (thread->header_callback) {
				if ((ret = thread->header_callback(&state)))
					goto err;
			}

			if ((ret = ps_packet_dma(&read, (void *) &state.read_data,
						 state.read_size, PS_ACCEPT_FAKE_DMA)))
				goto err;

			/* read callback */
			if (thread->read_callback) {
				if ((ret = thread->read_callback(&state)))
					goto err;
			}
		}

		if ((thread->flags & GLC_THREAD_WRITE) && (!(state.flags & GLC_THREAD_STATE_SKIP_WRITE))) {
			if ((ret = ps_packet_open(&write, PS_PACKET_WRITE)))
				goto err;

			if (has_locked) {
				has_locked = 0;
				pthread_mutex_unlock(&private->open);
			}

			/* reserve space for header */
			if ((ret = ps_packet_seek(&write, GLC_MESSAGE_HEADER_SIZE)))
				goto err;

			if (!(state.flags & GLC_THREAD_STATE_UNKNOWN_FINAL_SIZE)) {
				/* 'unlock' write */
				if ((ret = ps_packet_setsize(&write,
					                     GLC_MESSAGE_HEADER_SIZE + state.write_size)))
					goto err;
				write_size_set = 1;
			}

			if (state.flags & GLC_THREAD_COPY) {
				/* should be faster, no need for fake dma */
				if ((ret = ps_packet_write(&write, state.read_data, state.write_size)))
					goto err;
			} else {
				if ((ret = ps_packet_dma(&write, (void *) &state.write_data,
							 state.write_size, PS_ACCEPT_FAKE_DMA)))
						goto err;

				/* write callback */
				if (thread->write_callback) {
					if ((ret = thread->write_callback(&state)))
						goto err;
				}
			}

			/* write header */
			if ((ret = ps_packet_seek(&write, 0)))
				goto err;
			if ((ret = ps_packet_write(&write, &state.header, GLC_MESSAGE_HEADER_SIZE)))
				goto err;
		}

		/* in case of we skipped writing */
		if (has_locked) {
			has_locked = 0;
			pthread_mutex_unlock(&private->open);
		}

		if ((thread->flags & GLC_THREAD_READ) && (!(state.flags & GLC_THREAD_STATE_SKIP_READ))) {
			ps_packet_close(&read);
			state.read_data = NULL;
			state.read_size = 0;
		}

		if ((thread->flags & GLC_THREAD_WRITE) && (!(state.flags & GLC_THREAD_STATE_SKIP_WRITE))) {
			if (!write_size_set) {
				if ((ret = ps_packet_setsize(&write,
							     GLC_MESSAGE_HEADER_SIZE + state.write_size)))
					goto err;
			}
			ps_packet_close(&write);
			state.write_data = NULL;
		state.write_size = 0;
		}

		/* close callback */
		if (thread->close_callback) {
			if ((ret = thread->close_callback(&state)))
				goto err;
		}

		if (state.flags & GLC_THREAD_STOP)
			break; /* no error, just stop, please */

		state.flags = 0;
		write_size_set = 0;
	} while ((!(private->glc->flags & GLC_CANCEL)) &&
		 (state.header.type != GLC_MESSAGE_CLOSE) &&
		 (!private->stop));

finish:
	if (thread->flags & GLC_THREAD_READ)
		ps_packet_destroy(&read);
	if (thread->flags & GLC_THREAD_WRITE)
		ps_packet_destroy(&write);

	/* wake up remaining threads */
	if ((thread->flags & GLC_THREAD_READ) && (!private->stop)) {
		private->stop = 1;
		ps_buffer_cancel(private->from);

		/* error might have happened @ write buffer
		   so there could be blocking threads */
		if (thread->flags & GLC_THREAD_WRITE)
			ps_buffer_cancel(private->to);
	}

	/* thread finish callback */
	if (thread->thread_finish_callback)
		thread->thread_finish_callback(state.ptr, state.threadptr, ret);

	pthread_mutex_lock(&private->finish);
	thread->threads--;

	/* let other threads know about the error */
	if (ret)
		private->ret = ret;

	if (thread->threads > 0) {
		pthread_mutex_unlock(&private->finish);
		return NULL;
	}

	/* it is safe to unlock now */
	pthread_mutex_unlock(&private->finish);

	/* finish callback */
	if (thread->finish_callback)
		thread->finish_callback(state.ptr, private->ret);

	free(private->pthread_thread);
	pthread_mutex_destroy(&private->finish);
	pthread_mutex_destroy(&private->open);
	free(private);
	return NULL;

err:
	if (has_locked)
		pthread_mutex_unlock(&private->open);

	if (ret == EINTR)
		ret = 0;
	else
		util_log(private->glc, GLC_ERROR, "glc_thread", "%s (%d)", strerror(ret), ret);

	goto finish;
}

/**  \} */
/**  \} */
