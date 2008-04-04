/**
 * \file glc/core/copy.h
 * \brief generic stream demuxer
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup core
 *  \{
 * \defgroup copy generic stream demuxer
 *  \{
 */

#ifndef _COPY_H
#define _COPY_H

#include <packetstream.h>
#include <glc/common/glc.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief copy object
 */
typedef struct copy_s* copy_t;

/**
 * \brief initialize copy object
 * \param copy copy object
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
int copy_init(copy_t *copy, glc_t *glc);

/**
 * \brief destroy copy object
 * \param copy copy object
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
int copy_destroy(copy_t copy);

/**
 * \brief add copy target
 *
 * Only messages with selected type are copied into target buffer.
 * If type is 0 all messages are copied. A target can be added several times
 * so several message types can be selected for copying.
 *
 * Remember to add GLC_MESSAGE_CLOSE if you want to close objects behind
 * target buffer when stream ends.
 * \param copy copy object
 * \param target target buffer
 * \param type copy only selected messages or
 *             if this is 0, all messages are copied
 *             into this buffer
 * \return 0 on success otherwise an error code
 */
int copy_add(copy_t copy, ps_buffer_t *target, glc_message_type_t type);

/**
 * \brief start copy process
 * \param copy copy object
 * \param from source buffer
 * \return 0 on success otherwise an error code
 */
int copy_process_start(copy_t copy, ps_buffer_t *from);

/**
 * \brief block until copy process has finished
 * \param copy copy object
 * \return 0 on success otherwise an error code
 */
int copy_process_wait(copy_t copy);

#ifdef __cplusplus
}
#endif

#endif

/**  \} */
/**  \} */
