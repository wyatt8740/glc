/**
 * \file src/common/thread.h
 * \brief thread interface
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

#ifndef _THREAD_H
#define _THREAD_H

#include <packetstream.h>
#include <glc/common/glc.h>

/** currently unused legacy */
#define GLC_THREAD_UNUSED1                    1
/** currently unused legacy */
#define GLC_THREAD_UNUSED2                    2
/** thread does not yet know final packet size, so write dma
    is not acquired */
#define GLC_THREAD_STATE_UNKNOWN_FINAL_SIZE   4
/** thread wants to skip reading a packet */
#define GLC_THREAD_STATE_SKIP_READ            8
/** thread wants to skip writing a packet */
#define GLC_THREAD_STATE_SKIP_WRITE          16
/** just copy data to write packet, skip write callback */
#define GLC_THREAD_COPY                      32
/** thread wants to stop */
#define GLC_THREAD_STOP                      64

/**
 * \brief thread state
 *
 * Thread state structure holds information about
 * current read and write packet and thread state.
 * Callback functions modify data and thread state
 * using this structure.
 */
typedef struct {
	/** flags */
	glc_flags_t flags;
	/** current message header */
	glc_message_header_t header;
	/** read data excluding header */
	char *read_data;
	/** data to be written excluding header */
	char *write_data;
	/** read data size */
	size_t read_size;
	/** write data size, read callback should set this
	    so thread knows how big dma to request */
	size_t write_size;
	/** global argument pointer */
	void *ptr;
	/** per-thread argument pointer */
	void *threadptr;
} glc_thread_state_t;

/** thread does read operations */
#define GLC_THREAD_READ                       1
/** thread does write operations */
#define GLC_THREAD_WRITE                      2
/**
 * \brief thread
 *
 * glc_thread_t holds information about thread callbacks
 * and features. Mandatory values are flags, and threads.
 * If callback is NULL, it is ignored.
 */
typedef struct {
	/** flags, GLC_THREAD_READ or GLC_THREAD_WRITE or both */
	glc_flags_t flags;
	/** global argument pointer */
	void *ptr;
	/** number of threads to create */
	size_t threads;
	/** implementation specific */
	void *priv;

	/** thread create callback is called when a thread starts */
	int (*thread_create_callback)(void *, void **);
	/** thread finish callback is called when a thread is finished */
	void (*thread_finish_callback)(void *, void *, int);
	/** open callback is called before thread tries to
	    open read (or write if GLC_THREAD_READ is not specified)
	    packet */
	int (*open_callback)(glc_thread_state_t *);
	/** header callback is called when thread has read
	    header from packet */
	int (*header_callback)(glc_thread_state_t *);
	/** read callback is called when thread has read the
	    whole packet */
	int (*read_callback)(glc_thread_state_t *);
	/** write callback is called when thread has opened
	    dma to write packet */
	int (*write_callback)(glc_thread_state_t *);
	/** close callback is called when both packets are closed */
	int (*close_callback)(glc_thread_state_t *);
	/** finish callback is called only once, when all threads have
	    finished */
	void (*finish_callback)(void *, int);
} glc_thread_t;

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
__PUBLIC int glc_thread_create(glc_t *glc, glc_thread_t *thread, ps_buffer_t *from, ps_buffer_t *to);

/**
 * \brief block until threads have finished and clean up
 * \param thread thread
 * \return 0 on success otherwise an error code
 */
__PUBLIC int glc_thread_wait(glc_thread_t *thread);

#endif

/**  \} */
/**  \} */
