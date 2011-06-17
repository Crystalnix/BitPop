/* Copyright (c) 2011 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef PPAPI_C_PPB_AUDIO_H_
#define PPAPI_C_PPB_AUDIO_H_

#include "ppapi/c/pp_bool.h"
#include "ppapi/c/pp_instance.h"
#include "ppapi/c/pp_module.h"
#include "ppapi/c/pp_resource.h"
#include "ppapi/c/pp_stdint.h"

#define PPB_AUDIO_INTERFACE "PPB_Audio;0.6"

/**
 * @file
 * This file defines the PPB_Audio interface, which provides realtime stereo
 * audio streaming capabilities. Please refer to the
 * <a href="/chrome/nativeclient/docs/audio.html">Pepper Audio API Code
 * Walkthrough</a> for information on using this interface.
 */

/**
 * @addtogroup Typedefs
 * @{
 */

/**
 * PPB_Audio_Callback defines the type of an audio callback function used to
 * fill the audio buffer with data. Please see the Create() function in the
 * PPB_Audio interface for more details on this callback.
 */
typedef void (*PPB_Audio_Callback)(void* sample_buffer,
                                   uint32_t buffer_size_in_bytes,
                                   void* user_data);
/**
 * @}
 */

/**
 * @addtogroup Interfaces
 * @{
 */
/**
 * The PPB_Audio interface contains pointers to several functions for handling
 * audio resources. Please see descriptions for each PPB_Audio and
 * PPB_AudioConfig function for more details.
 *
 * A C example using PPB_Audio and PPB_AudioConfig:
 *
 * void audio_callback(void* sample_buffer,
 *                     uint32_t buffer_size_in_bytes,
 *                     void* user_data) {
 *   ... quickly fill in the buffer with samples and return to caller ...
 *  }
 *
 * ...Assume the application has cached the audio configuration interface in
 * |audio_config_interface| and the audio interface in |audio_interface|...
 *
 * uint32_t count = audio_config_interface->RecommendSampleFrameCount(
 *     PP_AUDIOSAMPLERATE_44100, 4096);
 * PP_Resource pp_audio_config = audio_config_interface->CreateStereo16Bit(
 *     pp_instance, PP_AUDIOSAMPLERATE_44100, count);
 * PP_Resource pp_audio = audio_interface->Create(pp_instance, pp_audio_config,
 *     audio_callback, NULL);
 * audio_interface->StartPlayback(pp_audio);
 *
 * ...audio_callback() will now be periodically invoked on a seperate thread...
 */
struct PPB_Audio {
 /**
  * Create is a pointer to a function that creates an audio resource.
  * No sound will be heard until StartPlayback() is called. The callback
  * is called with the buffer address and given user data whenever the
  * buffer needs to be filled. From within the callback, you should not
  * call PPB_Audio functions. The callback will be called on a different
  * thread than the one which created the interface. For performance-critical
  * applications (i.e. low-latency audio), the callback should avoid blocking
  * or calling functions that can obtain locks, such as malloc. The layout and
  * the size of the buffer passed to the audio callback will be determined by
  * the device configuration and is specified in the AudioConfig documentation.
  *
  * @param[in] instance A PP_Instance indentifying one instance of a module.
  * @param[in] config A PP_Resource containing the audio config resource.
  * @param[in] audio_callback A PPB_Audio_Callback callback function that the
  * browser calls when it needs more samples to play.
  * @param[in] user_data A pointer to user data used in the callback function.
  * @return A PP_Resource containing the audio resource if successful or
  * 0 if the configuration cannot be honored or the callback is null.
  */
  PP_Resource (*Create)(PP_Instance instance, PP_Resource config,
                        PPB_Audio_Callback audio_callback, void* user_data);
  /**
   * IsAudio is a pointer to a function that determines if the given
   * resource is an audio resource.
   *
   * @param[in] resource A PP_Resource containing a resource.
   * @return A PP_BOOL containing containing PP_TRUE if the given resource is
   * an Audio resource, otherwise PP_FALSE.
   */
  PP_Bool (*IsAudio)(PP_Resource resource);

  /**
   * GetCurrrentConfig is a pointer to a function that returns an audio config
   * resource for the given audio resource.
   *
   * @param[in] config A PP_Resource containing the audio resource.
   * @return A PP_Resource containing the audio config resource if successful.
   */
  PP_Resource (*GetCurrentConfig)(PP_Resource audio);

  /**
   * StartPlayback is a pointer to a function that starts the playback of
   * the audio resource and begins periodically calling the callback.
   *
   * @param[in] config A PP_Resource containing the audio resource.
   * @return A PP_BOOL containing PP_TRUE if successful, otherwise PP_FALSE.
   * Also returns PP_TRUE (and be a no-op) if called while playback is already
   * in progress.
   */
  PP_Bool (*StartPlayback)(PP_Resource audio);

  /**
   * StopPlayback is a pointer to a function that stops the playback of
   * the audio resource.
   *
   * @param[in] config A PP_Resource containing the audio resource.
   * @return A PP_BOOL containing PP_TRUE if successful, otherwise PP_FALSE.
   * Also returns PP_TRUE (and is a no-op) if called while playback is already
   * stopped. If a callback is in progress, StopPlayback will block until the
   * callback completes.
   */
  PP_Bool (*StopPlayback)(PP_Resource audio);
};
/**
 * @}
 */

#endif  /* PPAPI_C_PPB_DEVICE_CONTEXT_AUDIO_H_ */

