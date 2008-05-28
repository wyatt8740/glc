/**
 * \file glc/core/file.h
 * \brief file io
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup core
 *  \{
 * \defgroup file file io
 *  \{
 */

#ifndef _FILE_H
#define _FILE_H

#include <packetstream.h>
#include <glc/common/glc.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief file object
 */
typedef struct file_s* file_t;

/**
 * \brief initialize file object
 * Writing is done in its own thread.
 * \code
 * // writing example
 * file_init(*file, glc);
 * file_open_target(file, "/tmp/stream.glc");
 * ...
 * file_write_info(file, &info, name, date);
 * file_write_process_start(file, buffer);
 * ...
 * file_write_process_wait(file);
 * file_close_target(file);
 * file_destroy(file);
 * \endcode
 *
 * Reading stream from file is done in same thread.
 * \code
 * // reading example
 * file_init(*file, glc);
 * file_open_source(file, "/tmp/stream.glc");
 * ...
 * file_read_info(file, &info, &name, &date);
 * file_read(file, buffer);
 * file_close_source(file);
 * ...
 * file_destroy(file);
 * free(name);
 * free(date);
 * \endcode
 *
 * file_write_info() must be called before starting write
 * process.
 *
 * Like in writing, file_read_info() must be called before
 * calling file_read().
 *
 * One stream file can actually hold multiple individual
 * streams: [info0][stream0][info1][stream1]...
 * \param file file object
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
__PUBLIC int file_init(file_t *file, glc_t *glc);

/**
 * \brief set sync mode
 * \note this must be set before opening file
 * \param file file object
 * \param sync 0 = no forced synchronization, 1 = force writing immediately to device
 * \return 0 on success otherwise an error code
 */
__PUBLIC int file_set_sync(file_t file, int sync);

/**
 * \brief open file for writing
 * \note this calls file_set_target()
 * \param file file object
 * \param filename target file
 * \return 0 on success otherwise an error code
 */
__PUBLIC int file_open_target(file_t file, const char *filename);

/**
 * \brief setup file descriptor for writing
 *
 * This locks file descriptor and truncates it. If file descriptor
 * can't be locked this will fail.
 * \param file file object
 * \param fd file descriptor
 * \return 0 on success otherwise an error code
 */
__PUBLIC int file_set_target(file_t file, int fd);

/**
 * \brief close target file descriptor
 * \param file file object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int file_close_target(file_t file);

/**
 * \brief write stream information header to file
 * \param file file object
 * \param info info structure
 * \param info_name app name
 * \param info_date date
 * \return 0 on success otherwise an error code
 */
__PUBLIC int file_write_info(file_t file, glc_stream_info_t *info,
			     const char *info_name, const char *info_date);

/**
 * \brief start writing process
 *
 * file will write all data from source buffer to target file
 * in a custom format that can be read back using file_read()
 * \param file file object
 * \param from source buffer
 * \return 0 on success otherwise an error code
 */
__PUBLIC int file_write_process_start(file_t file, ps_buffer_t *from);

/**
 * \brief block until process has finished
 * \param file file object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int file_write_process_wait(file_t file);
/**
 * \brief open file for reading
 *
 * \note this calls file_set_source()
 * \param file file object
 * \param filename source file
 * \return 0 on success otherwise an error code
 */
__PUBLIC int file_open_source(file_t file, const char *filename);

/**
 * \brief set source file descriptor
 * \param file file object
 * \param fd file descriptor
 * \return 0 on success otherwise an error code
 */
__PUBLIC int file_set_source(file_t file, int fd);

/**
 * \brief close source file
 * \param file file object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int file_close_source(file_t file);

/**
 * \brief read stream information
 * \note info_name and info_date are allocated but file_destroy()
 *       won't free them.
 * \param file file object
 * \param info info structure
 * \param info_name app name
 * \param info_date date
 * \return 0 on success otherwise an error code
 */
__PUBLIC int file_read_info(file_t file, glc_stream_info_t *info,
			    char **info_name, char **info_date);

/**
 * \brief read stream from file and write it into buffer
 * \param file file object
 * \param to buffer
 * \return 0 on success otherwise an error code
 */
__PUBLIC int file_read(file_t file, ps_buffer_t *to);

/**
 * \brief destroy file object
 * \param file file object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int file_destroy(file_t file);

#ifdef __cplusplus
}
#endif

#endif

/**  \} */
/**  \} */
