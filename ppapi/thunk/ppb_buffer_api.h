// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PPAPI_THUNK_PPB_BUFFER_API_H_
#define PPAPI_THUNK_PPB_BUFFER_API_H_

#include "ppapi/c/pp_bool.h"
#include "ppapi/c/pp_stdint.h"

namespace ppapi {
namespace thunk {

class PPB_Buffer_API {
 public:
  virtual ~PPB_Buffer_API() {}

  virtual PP_Bool Describe(uint32_t* size_in_bytes) = 0;
  virtual PP_Bool IsMapped() = 0;
  virtual void* Map() = 0;
  virtual void Unmap() = 0;
};

}  // namespace thunk
}  // namespace ppapi

#endif  // PPAPI_THUNK_PPB_BUFFER_API_H_
