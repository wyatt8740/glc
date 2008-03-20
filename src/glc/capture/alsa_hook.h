/**
 * \file glc/capture/alsa_hook.h
 * \brief audio capture hooks
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup capture
 *  \{
 * \defgroup alsa_hook audio capture hooks
 *  \{
 */

#ifndef _ALSA_HOOK_H
#define _ALSA_HOOK_H

#include <packetstream.h>
#include <alsa/asoundlib.h>
#include <glc/common/glc.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief alsa_hook object
 */
typedef struct alsa_hook_s* alsa_hook_t;

/**
 * \brief initialize alsa_hook
 *
 * Initializes alsa_hook and binds it into given glc.
 * \param alsa_hook alsa_hook object
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
__PUBLIC int alsa_hook_init(alsa_hook_t *alsa_hook, glc_t *glc);

/**
 * \brief allow audio skipping in some cases
 *
 * If audio_skip is enabled, alsa_hook may skip some audio data
 * if buffer is not ready and pcm is opened in asynchronous mode.
 * \param alsa_hook alsa_hook object
 * \param allow_skip 1 allows skipping, 0 disallows
 * \return 0 on success otherwise an error code
 */
__PUBLIC int alsa_hook_allow_skip(alsa_hook_t alsa_hook, int allow_skip);

/**
 * \brief set target buffer
 * \param alsa_hook alsa_hook object
 * \param buffer target buffer
 * \return 0 on success otherwise an error code
 */
__PUBLIC int alsa_hook_set_buffer(alsa_hook_t alsa_hook, ps_buffer_t *buffer);

/**
 * \brief start capturing hooked audio
 * \param alsa_hook alsa_hook object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int alsa_hook_start(alsa_hook_t alsa_hook);

/**
 * \brief stop capturing
 * \param alsa_hook alsa_hook object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int alsa_hook_stop(alsa_hook_t alsa_hook);

/**
 * \brief destroy alsa_hook object
 * \param alsa_hook alsa_hook object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int alsa_hook_destroy(alsa_hook_t alsa_hook);

/**
 * \brief hook function for snd_pcm_open()
 *
 * Call after calling real snd_pcm_open().
 * \code
 * if (snd_pcm_open(pcmp, name, stream, mode) == 0)
 * 	alsa_hook_alsa_open(alsa_hook, *pcmp, nome, stream, mode);
 * \endcode
 * \param alsa_hook alsa_hook object
 * \param pcm pcm pointer given by snd_pcm_open()
 * \param name pcm name
 * \param pcm_stream pcm stream
 * \param mode open mode
 * \return 0 on success otherwise an error code
 */
__PUBLIC int alsa_hook_open(alsa_hook_t alsa_hook, snd_pcm_t *pcm, const char *name,
			    snd_pcm_stream_t pcm_stream, int mode);

/**
 * \brief hook function for snd_pcm_close()
 * \code
 * snd_pcm_close(pcm);
 * alsa_hook_alsa_close(alsa_hook, pcm);
 * \endcode
 * \param alsa_hook alsa_hook object
 * \param pcm closed pcm
 * \return 0 on success otherwise an error code
 */
__PUBLIC int alsa_hook_close(alsa_hook_t alsa_hook, snd_pcm_t *pcm);

/**
 * \brief hook function for snd_pcm_hw_params()
 * \code
 * if (snd_pcm_hw_params(pcm, params))
 * 	alsa_hook_alsa_hw_params(alsa_hook, pcm, params);
 * \endcode
 * \param alsa_hook alsa_hook object
 * \param pcm pcm
 * \param params active hardware parameters
 * \return 0 on success otherwise an error code
 */
__PUBLIC int alsa_hook_hw_params(alsa_hook_t alsa_hook, snd_pcm_t *pcm,
				 snd_pcm_hw_params_t *params);

/**
 * \brief hook function for snd_pcm_writei()
 * \code
 * snd_pcm_sframes_t ret = snd_pcm_writei(pcm, buffer, size);
 * if (ret > 0)
 * 	alsa_hook_alsa_i(alsa_hook, pcm, buffer, ret);
 * \endcode
 * \param alsa_hook alsa_hook object
 * \param pcm pcm
 * \param buffer audio data
 * \param size actually written frames
 * \return 0 on success otherwise an error code
 */
__PUBLIC int alsa_hook_writei(alsa_hook_t alsa_hook, snd_pcm_t *pcm,
			      const void *buffer, snd_pcm_uframes_t size);

/**
 * \brief hook function for snd_pcm_writen()
 * \code
 * snd_pcm_sframes_t ret = snd_pcm_writen(pcm, bufs, size);
 * if (ret > 0)
 * 	alsa_hook_alsa_n(alsa_hook, pcm, bufs, size);
 * \endcode
 * \param alsa_hook alsa_hook object
 * \param pcm pcm
 * \param bufs channel data areas
 * \param size actually written frames
 * \return 0 on success otherwise an error code
 */
__PUBLIC int alsa_hook_writen(alsa_hook_t alsa_hook, snd_pcm_t *pcm,
			      void **bufs, snd_pcm_uframes_t size);

/**
 * \brief hook function for snd_pcm_mmap_begin()
 * \code
 * if (snd_pcm_mmap_begin(pcm, areas, offset, frames) >= 0)
 * 	alsa_hook_alsa_mmap_begin(alsa_hook, pcm, *areas, *offset, *frames);
 * \endcode
 * \param alsa_hook alsa_hook object
 * \param pcm pcm
 * \param areas channel data areas given by snd_pcm_mmap_begin()
 * \param offset offset given by snd_pcm_mmap_begin()
 * \param frames frames given by snd_pcm_mmap_begin()
 * \return 0 on success otherwise an error code
 */
__PUBLIC int alsa_hook_mmap_begin(alsa_hook_t alsa_hook, snd_pcm_t *pcm,
				  const snd_pcm_channel_area_t *areas,
				  snd_pcm_uframes_t offset, snd_pcm_uframes_t frames);

/**
 * \brief hook function for snd_pcm_mmap_commit()
 *
 * Must be called before calling real snd_pcm_mmap_commit().
 * \code
 * alsa_hook_alsa_mmap_commit(alsa_hook, pcm, offset, frames);
 * snd_pcm_mmap_commit(pcm, offset, frames);
 * \endcode
 * \param alsa_hook alsa_hook object
 * \param pcm pcm
 * \param offset offset
 * \param frames number of frames written
 * \return 0 on success otherwise an error code
 */
__PUBLIC int alsa_hook_mmap_commit(alsa_hook_t alsa_hook, snd_pcm_t *pcm,
				   snd_pcm_uframes_t offset, snd_pcm_uframes_t frames);

#ifdef __cplusplus
}
#endif

#endif

/**  \} */
/**  \} */
