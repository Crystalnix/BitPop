// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PPAPI_THUNK_PPB_FILE_REF_API_H_
#define PPAPI_THUNK_PPB_FILE_REF_API_H_

#include "ppapi/c/dev/ppb_file_ref_dev.h"

namespace ppapi {
namespace thunk {

class PPB_FileRef_API {
 public:
  virtual PP_FileSystemType_Dev GetFileSystemType() const = 0;
  virtual PP_Var GetName() const = 0;
  virtual PP_Var GetPath() const = 0;
  virtual PP_Resource GetParent() = 0;
  virtual int32_t MakeDirectory(PP_Bool make_ancestors,
                                PP_CompletionCallback callback) = 0;
  virtual int32_t Touch(PP_Time last_access_time,
                        PP_Time last_modified_time,
                        PP_CompletionCallback callback) = 0;
  virtual int32_t Delete(PP_CompletionCallback callback) = 0;
  virtual int32_t Rename(PP_Resource new_file_ref,
                         PP_CompletionCallback callback) = 0;
};

}  // namespace thunk
}  // namespace ppapi

#endif  // PPAPI_THUNK_PPB_FILE_REF_API_H_
