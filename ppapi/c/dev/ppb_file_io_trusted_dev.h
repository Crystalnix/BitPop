/* Copyright (c) 2010 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef PPAPI_C_DEV_PPB_FILE_IO_TRUSTED_DEV_H_
#define PPAPI_C_DEV_PPB_FILE_IO_TRUSTED_DEV_H_

#include "ppapi/c/pp_resource.h"
#include "ppapi/c/pp_stdint.h"

struct PP_CompletionCallback;

#define PPB_FILEIOTRUSTED_DEV_INTERFACE_0_2 "PPB_FileIOTrusted(Dev);0.2"
#define PPB_FILEIOTRUSTED_DEV_INTERFACE PPB_FILEIOTRUSTED_DEV_INTERFACE_0_2

// Available only to trusted implementations.
struct PPB_FileIOTrusted_Dev {
  // Returns a file descriptor corresponding to the given FileIO object. On
  // Windows, returns a HANDLE; on all other platforms, returns a POSIX file
  // descriptor. The FileIO object must have been opened with a successful
  // call to FileIO::Open.  The file descriptor will be closed automatically
  // when the FileIO object is closed or destroyed.
  int32_t (*GetOSFileDescriptor)(PP_Resource file_io);

  // Notifies the browser that underlying file will be modified.  This gives
  // the browser the opportunity to apply quota restrictions and possibly
  // return an error to indicate that the write is not allowed.
  int32_t (*WillWrite)(PP_Resource file_io,
                       int64_t offset,
                       int32_t bytes_to_write,
                       struct PP_CompletionCallback callback);

  // Notifies the browser that underlying file will be modified.  This gives
  // the browser the opportunity to apply quota restrictions and possibly
  // return an error to indicate that the write is not allowed.
  int32_t (*WillSetLength)(PP_Resource file_io,
                           int64_t length,
                           struct PP_CompletionCallback callback);

  // TODO(darin): Maybe unify the above into a single WillChangeFileSize
  // method?  The above methods have the advantage of mapping to PPB_FileIO
  // Write and SetLength calls.  WillChangeFileSize would require the caller to
  // compute the file size resulting from a Write call, which may be
  // undesirable.
};

#endif  /* PPAPI_C_DEV_PPB_FILE_IO_TRUSTED_DEV_H_ */

