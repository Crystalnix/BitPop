// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PPAPI_THUNK_PPB_FONT_API_H_
#define PPAPI_THUNK_PPB_FONT_API_H_

#include "ppapi/c/dev/ppb_font_dev.h"
#include "ppapi/shared_impl/api_id.h"
#include "ppapi/thunk/ppapi_thunk_export.h"

namespace ppapi {
namespace thunk {

// API for static font functions.
class PPB_Font_FunctionAPI {
 public:
  virtual ~PPB_Font_FunctionAPI() {}

  virtual PP_Var GetFontFamilies(PP_Instance instance) = 0;

  static const ApiID kApiID = API_ID_PPB_FONT;
};

// API for font resources.
class PPAPI_THUNK_EXPORT PPB_Font_API {
 public:
  virtual ~PPB_Font_API() {}

  virtual PP_Bool Describe(PP_FontDescription_Dev* description,
                           PP_FontMetrics_Dev* metrics) = 0;
  virtual PP_Bool DrawTextAt(PP_Resource image_data,
                             const PP_TextRun_Dev* text,
                             const PP_Point* position,
                             uint32_t color,
                             const PP_Rect* clip,
                             PP_Bool image_data_is_opaque) = 0;
  virtual int32_t MeasureText(const PP_TextRun_Dev* text) = 0;
  virtual uint32_t CharacterOffsetForPixel(const PP_TextRun_Dev* text,
                                           int32_t pixel_position) = 0;
  virtual int32_t PixelOffsetForCharacter(const PP_TextRun_Dev* text,
                                          uint32_t char_offset) = 0;
};

}  // namespace thunk
}  // namespace ppapi

#endif  // PPAPI_THUNK_PPB_FONT_API_H_
