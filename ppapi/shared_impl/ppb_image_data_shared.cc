// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/shared_impl/ppb_image_data_shared.h"

#include "third_party/skia/include/core/SkTypes.h"

namespace ppapi {

// static
PP_ImageDataFormat PPB_ImageData_Shared::GetNativeImageDataFormat() {
  if (SK_B32_SHIFT == 0)
    return PP_IMAGEDATAFORMAT_BGRA_PREMUL;
  else if (SK_R32_SHIFT == 0)
    return PP_IMAGEDATAFORMAT_RGBA_PREMUL;
  else
    return PP_IMAGEDATAFORMAT_BGRA_PREMUL;  // Default to something on failure.
}

// static
bool PPB_ImageData_Shared::IsImageDataFormatSupported(
    PP_ImageDataFormat format) {
  return format == PP_IMAGEDATAFORMAT_BGRA_PREMUL ||
         format == PP_IMAGEDATAFORMAT_RGBA_PREMUL;
}

}  // namespace ppapi
