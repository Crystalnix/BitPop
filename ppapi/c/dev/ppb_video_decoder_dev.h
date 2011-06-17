/* Copyright (c) 2011 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef PPAPI_C_DEV_PPB_VIDEO_DECODER_DEV_H_
#define PPAPI_C_DEV_PPB_VIDEO_DECODER_DEV_H_

#include "ppapi/c/dev/pp_video_dev.h"
#include "ppapi/c/pp_completion_callback.h"
#include "ppapi/c/pp_var.h"

#define PPB_VIDEODECODER_DEV_INTERFACE "PPB_VideoDecoder(Dev);0.5"

// Video decoder interface.
//
// Basic usage:
//   1. Use GetConfigs() to query potential configurations. Configuration
//   information includes:
//     a. Bitstream format.
//     b. Output picture format.
//     c. Output picture buffer storage type.
//   2. Select configuration that suits you and Create() the decoder with the
//   chosen configuration.
//   3. Get the input bitstream data and Decode() it until streaming should
//   stop or pause.
//
//   Once decoder has processed a data from the bitstream buffer provided for
//   decoding, it will call callback provided by the plug-in for letting the
//   plug-in to know when it can release or recycle each buffer.
//
//   Once decoder knows the output picture properties it needs, it will call
//   callback provided by the plug-in for providing the needed buffers. Plug-in
//   must provide there to the decoder.
//
//   Errors are reported asynchronously to plug-in by calling the callback
//   provided by the plug-in for error handling.
//
// Information to be conveyed over the API:
//   1. Bitstream format.
//   2. Output picture format.
//   3. Output picture buffer storage type.
//
struct PPB_VideoDecoder_Dev {
  // Queries capability of the decoder implementation for a specific codec.
  //
  // Parameters:
  //   |instance| is the instance handle for the plugin.
  //   |proto_config| is a pointer to a prototype decoder configuration
  //   whose values are matched against supported configs. The intersection
  //   of prototype configuration and supported configs is stored in
  //   |matching_configs|.
  //   |matching_configs| is a pointer to a buffer where information about
  //   supported configuration elements that match the |proto_config| are
  //   stored.
  //   |matching_configs_size| tells for how many PP_VideoConfig_Dev elements
  //   the buffer pointed by |matching_configs| has space for.
  //   |num_of_matching_configs| is output parameter telling how many configs
  //   are filled with valid video config elements in the buffer pointed by
  //   |matching_configs| after successful call to the function.
  //
  // After the call |num_config| - 1 of PP_VideoConfig_Dev elements are filled
  // with valid elements to the buffer pointed by |matching_configs|. No more
  // than |config_size| PP_VideoConfig_Devs will be returned even if more would
  // be available for the decoder device.
  //
  // When this function is called with |matching_configs| = NULL, then no
  // configurations are returned, but the total number of PP_VideoConfig_Devs
  // available will be returned in |num_of_matching_configs|.
  //
  // Returns PP_TRUE on success, PP_FALSE otherwise.
  PP_Bool (*GetConfigs)(PP_Instance instance,
                        struct PP_VideoDecoderConfig_Dev* proto_config,
                        struct PP_VideoDecoderConfig_Dev* matching_configs,
                        int32_t matching_configs_size,
                        int32_t* num_of_matching_configs);

  // Creates a video decoder with requested |decoder_config|.
  // |input_format| in |decoder_config| specifies the format of input access
  // unit, with PP_VIDEOKEY_CODECID and PP_VIDEOKEY_PAYLOADFORMAT required.
  // Plugin has the option to specify codec profile/level and other
  // information such as PP_VIDEOKEY_ACCELERATION, to let browser choose
  // the most appropriate decoder.
  //
  // Parameters:
  //   |instance| pointer to the plugin instance.
  //   |dec_config| the configuration which to use to initialize the decoder.
  //
  // The created decoder is returned as PP_Resource. NULL means failure.
  PP_Resource (*Create)(PP_Instance instance,
                        struct PP_VideoDecoderConfig_Dev* dec_config);

  // Tests whether |resource| is a video decoder created through Create
  // function of this interface.
  //
  // Parameters:
  //   |resource| is handle to resource to test.
  //
  // Returns true if is a video decoder, false otherwise.
  PP_Bool (*IsVideoDecoder)(PP_Resource resource);

  // Dispatches bitstream buffer to the decoder. This is asynchronous and
  // non-blocking function.
  //
  // Parameters:
  //   |video_decoder| is the previously created handle to the decoder instance.
  //   |bitstream_buffer| is the bitstream buffer that contains the input data.
  //   |callback| will be called when |bitstream_buffer| has been processed by
  //   the decoder.
  //
  // Returns PP_TRUE on decoder successfully accepting buffer, PP_FALSE
  // otherwise.
  PP_Bool (*Decode)(PP_Resource video_decoder,
                    struct PP_VideoBitstreamBuffer_Dev* bitstream_buffer,
                    struct PP_CompletionCallback callback);

  // Provides the decoder with picture buffers for video decoding. This
  // function should be called when decoder has issued ProvidePictureBuffers
  // callback to the plugin with buffer requirements.
  //
  // If the plugin is can determine how many and what kind of buffers are
  // needed by the decoder it can provide them in advance. For this purpose
  // the configuration map can provide how many extra buffers the decoder
  // implementation requires for seamless operation.
  //
  // The decoder will pause if AssignPictureBuffer hasn't been called with
  // sufficient buffers.
  //
  // If the decoder rejects the buffers it will return the buffers and issue
  // ProvidePictureBuffers again.
  //
  // If AssignPictureBuffer is called multiple times the decoder will add them
  // to its pool of output pictures.
  //
  // TODO(vmr): this API feels too flexible... should we make it more strict
  // where the decoder errors on bad buffers/duplicate buffers? Perhaps
  // AssignPictureBuffer should only get called in response to
  // ProvidePictureBuffer?
  //
  // Parameters:
  //   |video_decoder| is the previously created handle to the decoder instance.
  //   |no_of_buffers| how many buffers are behind picture buffer pointer.
  //   |picture_buffer| contains the reference to the picture buffer that was
  //   processed.
  void (*AssignPictureBuffer)(PP_Resource video_decoder,
                              uint32_t no_of_buffers,
                              union PP_PictureData_Dev* picture_buffer);

  // Tells the decoder to reuse given picture buffer. Typical use of this
  // function is to call from PictureReady callback to recycle picture buffer
  // back to the decoder after blitting the image so that decoder can use the
  // image for output again.
  //
  // The decoder will ignore any picture buffer not previously provided via
  // AssignPictureBuffer.
  //
  // TODO(vmr): figure out how the sync will be handled with command buffer as
  //            there will be possibly lag due to command buffer implementation
  //            to actual GL swap. At that moment decoder may have already taken
  //            the GL textures for writing output again.
  //
  // Parameters:
  //   |video_decoder| is the previously created handle to the decoder instance.
  //   |picture_buffer| contains the reference to the picture buffer that was
  //   processed.
  void (*ReusePictureBuffer)(PP_Resource video_decoder,
                             union PP_PictureData_Dev* picture_buffer);

  // Dispatches flushing request to the decoder to flush both input and output
  // buffers. Successful flushing will result in output of the pictures and
  // buffers held inside the decoder and returning of bitstream buffers using
  // the callbacks implemented by the plug-in. Once done with flushing, the
  // decode will call the |callback|.
  //
  // Parameters:
  //   |video_decoder| is the previously created handle to the decoder instance.
  //   |callback| is one-time callback that will be called once the flushing
  //   request has been completed.
  //
  // Returns PP_TRUE on acceptance of flush request and PP_FALSE if request to
  // flush is rejected by the decoder.
  PP_Bool (*Flush)(PP_Resource video_decoder,
                   struct PP_CompletionCallback callback);

  // Dispatches abortion request to the decoder to abort decoding as soon as
  // possible and will not output anything or generate new callbacks. |callback|
  // will be called as soon as abortion has been finished. After abortion all
  // buffers can be considered dismissed even when there has not been callbacks
  // to dismiss them.
  //
  // Parameters:
  //   |video_decoder| is the previously created handle to the decoder instance.
  //   |callback| is one-time callback that will be called once the abortion
  //   request has been completed.
  //
  // Returns PP_TRUE on acceptance of abort request and PP_FALSE if request to
  // abort is rejected by the decoder.
  PP_Bool (*Abort)(PP_Resource video_decoder,
                   struct PP_CompletionCallback callback);
};

#endif  /* PPAPI_C_DEV_PPB_VIDEO_DECODER_DEV_H_ */
