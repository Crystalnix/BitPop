// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PPAPI_THUNK_DIRECTORY_READER_API_H_
#define PPAPI_THUNK_DIRECTORY_READER_API_H_

#include "ppapi/c/dev/ppb_directory_reader_dev.h"

namespace ppapi {
namespace thunk {

class PPB_DirectoryReader_API {
 public:
  virtual int32_t GetNextEntry(PP_DirectoryEntry_Dev* entry,
                               PP_CompletionCallback callback) = 0;
};

}  // namespace thunk
}  // namespace ppapi

#endif  // PPAPI_THUNK_DIRECTORY_READER_API_H_
