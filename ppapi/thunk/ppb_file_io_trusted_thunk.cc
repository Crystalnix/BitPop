// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/c/dev/ppb_file_io_trusted_dev.h"
#include "ppapi/c/pp_completion_callback.h"
#include "ppapi/c/pp_errors.h"
#include "ppapi/thunk/thunk.h"
#include "ppapi/thunk/enter.h"
#include "ppapi/thunk/ppb_file_io_api.h"
#include "ppapi/thunk/resource_creation_api.h"

namespace ppapi {
namespace thunk {

namespace {

int32_t GetOSFileDescriptor(PP_Resource file_io) {
  EnterResource<PPB_FileIO_API> enter(file_io, true);
  if (enter.failed())
    return PP_ERROR_BADRESOURCE;
  return enter.object()->GetOSFileDescriptor();
}

int32_t WillWrite(PP_Resource file_io,
                  int64_t offset,
                  int32_t bytes_to_write,
                  PP_CompletionCallback callback) {
  EnterResource<PPB_FileIO_API> enter(file_io, true);
  if (enter.failed())
    return PP_ERROR_BADRESOURCE;
  return enter.object()->WillWrite(offset, bytes_to_write, callback);
}

int32_t WillSetLength(PP_Resource file_io,
                      int64_t length,
                      PP_CompletionCallback callback) {
  EnterResource<PPB_FileIO_API> enter(file_io, true);
  if (enter.failed())
    return PP_ERROR_BADRESOURCE;
  return enter.object()->WillSetLength(length, callback);
}

const PPB_FileIOTrusted_Dev g_ppb_file_io_trusted_thunk = {
  &GetOSFileDescriptor,
  &WillWrite,
  &WillSetLength
};

}  // namespace

const PPB_FileIOTrusted_Dev* GetPPB_FileIOTrusted_Thunk() {
  return &g_ppb_file_io_trusted_thunk;
}

}  // namespace thunk
}  // namespace ppapi
