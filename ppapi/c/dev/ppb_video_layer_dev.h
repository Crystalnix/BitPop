/* Copyright (c) 2011 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef PPAPI_C_DEV_PPB_VIDEO_LAYER_DEV_H_
#define PPAPI_C_DEV_PPB_VIDEO_LAYER_DEV_H_

#include "ppapi/c/dev/pp_video_dev.h"
#include "ppapi/c/pp_completion_callback.h"

#define PPB_VIDEOLAYER_DEV_INTERFACE "PPB_VideoLayer(Dev);0.1"

// Enumeration for pixel format of the video layer.
enum PP_VideoLayerPixelFormat_Dev {
  PP_VIDEOLAYERPIXELFORMAT_RGBA = 0,
  PP_VIDEOLAYERPIXELFORMAT_YV12 = 1,
};
PP_COMPILE_ASSERT_ENUM_SIZE_IN_BYTES(PP_VideoLayerPixelFormat_Dev, 4);

// TODO(hclam): Add options to customize color conversion.

// Enumeration for operation mode of the video layer.
// PPB_VideoLayer_Dev needs to be created with one of these enums in order to
// determine the operation mode.
enum PP_VideoLayerMode_Dev {
  // In this mode user needs to update content of the video layer manually by
  // calling UpdateContent().
  PP_VIDEOLAYERMODE_SOFTWARE = 0,

  // In this mode content of the video layer is updated by a hardware video
  // decoder, calling UpdateContent() will always return PP_FALSE.
  PP_VIDEOLAYERMODE_HARDWARE = 1,
};
PP_COMPILE_ASSERT_ENUM_SIZE_IN_BYTES(PP_VideoLayerMode_Dev, 4);

// PPB_VideoLayer is a mechanism to enhance rendering performance of video
// content. Rendering is generally done by using PPB_Context3D or
// PPB_Graphics2D, however for video content it is redundant to go through
// PPB_Context3D or PPB_Graphics2D. PPB_VideoLayer allows video content to be
// rendered directly.

// PPB_VideoLayer can be used in two modes:
//
// Software Decoding Mode
// In this mode the video layer needs to be updated with system memory manually
// using UpdateContent().
//
// Hardware Decoding Mode
// In this mode the content of the video layer is updated by a hardware video
// decoder.
struct PPB_VideoLayer_Dev {
  // Creates a video layer.
  PP_Resource (*Create)(PP_Instance instance, enum PP_VideoLayerMode_Dev mode);

  // Returns true if the input parameter is a video layer.
  PP_Bool (*IsVideoLayer)(PP_Resource layer);

  // Set the pixel format of this video layer. By default it is RGBA.
  //
  // This method must be called before the video layer can be displayed.
  //
  // The updated size will be effective after SwapBuffers() is called.
  void (*SetPixelFormat)(PP_Resource layer,
                         enum PP_VideoLayerPixelFormat_Dev pixel_format);

  // Set the native size of the video layer. This method must be called before
  // the video layer can be displayed.
  //
  // The updated size will be effective after SwapBuffers() is called.
  void (*SetNativeSize)(PP_Resource layer, const struct PP_Size* size);

  // Set the clipping rectangle for this video layer relative to the native
  // size. Only content within this rect is displayed.
  //
  // The clip rectangle will be effective after SwapBuffers() is called.
  void (*SetClipRect)(PP_Resource layer, const struct PP_Rect* clip_rect);

  // Return PP_TRUE if this video layer can be displayed. If this returns
  // PP_FALSE it can mean that the size is unknown or the video layer doesn't
  // have video memory allocated or not initialized.
  PP_Bool (*IsReady)(PP_Resource layer);

  // Update the content of a video layer from system memory. SetNativeSize()
  // must be called before making this method call.
  //
  // NOTE: This method has no effect in hardware decoding mode.
  //
  // |no_of_planes| is the number of planes in |planes|.
  // |planes| is an array of memory planes to be uploaded.
  //
  // Number of planes and format for planes is based on pixel format.
  //
  // PP_VIDEOLAYERPIXELFORMAT_RGBA:
  //
  // There will be one memory plane in RGBA format.
  //
  // planes[0] - RGBA plane, packed
  //
  // PP_VIDEOLAYERPIXELFORMAT_YV12:
  //
  // There will be three planes. In the order of Y, U and V. U and V planes
  // are 2x2 subsampled.
  //
  // planes[0] - Y plane
  // planes[1] - U plane, 2x2 subsampled
  // planes[2] - V plane, 2x2 subsampled
  //
  // Return true if successful.
  PP_Bool (*UpdateContent)(PP_Resource layer, uint32_t no_of_planes,
                           const void** planes);
};

#endif  /* PPAPI_C_DEV_PPB_VIDEO_LAYER_DEV_H_ */
