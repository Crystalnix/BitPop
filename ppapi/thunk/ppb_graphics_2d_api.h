// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/c/pp_bool.h"
#include "ppapi/c/pp_completion_callback.h"
#include "ppapi/c/pp_point.h"
#include "ppapi/c/pp_rect.h"
#include "ppapi/c/pp_resource.h"
#include "ppapi/c/pp_size.h"

namespace ppapi {
namespace thunk {

class PPB_Graphics2D_API {
 public:
  virtual PP_Bool Describe(PP_Size* size, PP_Bool* is_always_opaque) = 0;
  virtual void PaintImageData(PP_Resource image_data,
                              const PP_Point* top_left,
                              const PP_Rect* src_rect) = 0;
  virtual void Scroll(const PP_Rect* clip_rect,
                      const PP_Point* amount) = 0;
  virtual void ReplaceContents(PP_Resource image_data) = 0;
  virtual int32_t Flush(PP_CompletionCallback callback) = 0;
};

}  // namespace thunk
}  // namespace ppapi
