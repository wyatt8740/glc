/**
 * \file glc/core/tracker.h
 * \brief glc state tracker interface
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup core
 *  \{
 * \defgroup tracker glc state tracker
 *  \{
 */

#ifndef _TRACKER_H
#define _TRACKER_H

#include <glc/common/glc.h>

#ifdef __cplusplus
extern "C" {
#endif

/** glc state tracker object */
typedef struct tracker_s* tracker_t;

/** callback prototype for iterating */
typedef int (*tracker_callback_t)(glc_message_header_t *header, void *message,
				  size_t message_size, void *arg);

/**
 * \brief initialize glc state tracker
 * \param tracker state tracker object
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
__PUBLIC int tracker_init(tracker_t *tracker, glc_t *glc);

/**
 * \brief destroy glc state tracker
 * \param tracker state tracker object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int tracker_destroy(tracker_t tracker);

/**
 * \brief submit message to tracker
 * \param tracker state tracker object
 * \param header message header
 * \param message pointer to message data
 * \param message_size message size
 * \return 0 on success otherwise an error code
 */
__PUBLIC int tracker_submit(tracker_t tracker, glc_message_header_t *header,
			    void *message, size_t message_size);

/**
 * \brief iterate current state
 * \param tracker state tracker object
 * \param callback callback function
 * \param arg custom argument to callback
 * \return 0 on success otherwise an error code
 */
__PUBLIC int tracker_iterate_state(tracker_t tracker, tracker_callback_t callback,
				   void *arg);

#ifdef __cplusplus
}
#endif

#endif

/**  \} */
/**  \} */
