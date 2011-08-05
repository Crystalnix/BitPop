// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/plugins/ppapi/ppb_file_io_impl.h"

#include "base/callback.h"
#include "base/file_util.h"
#include "base/file_util_proxy.h"
#include "base/message_loop_proxy.h"
#include "base/platform_file.h"
#include "base/logging.h"
#include "base/time.h"
#include "ppapi/c/dev/ppb_file_io_dev.h"
#include "ppapi/c/dev/ppb_file_io_trusted_dev.h"
#include "ppapi/c/pp_completion_callback.h"
#include "ppapi/c/pp_errors.h"
#include "ppapi/thunk/enter.h"
#include "ppapi/thunk/ppb_file_ref_api.h"
#include "webkit/plugins/ppapi/common.h"
#include "webkit/plugins/ppapi/file_type_conversions.h"
#include "webkit/plugins/ppapi/plugin_module.h"
#include "webkit/plugins/ppapi/ppapi_plugin_instance.h"
#include "webkit/plugins/ppapi/ppb_file_ref_impl.h"
#include "webkit/plugins/ppapi/resource_tracker.h"

using ppapi::thunk::EnterResourceNoLock;
using ppapi::thunk::PPB_FileIO_API;
using ppapi::thunk::PPB_FileRef_API;

namespace webkit {
namespace ppapi {

PPB_FileIO_Impl::PPB_FileIO_Impl(PluginInstance* instance)
    : Resource(instance),
      ALLOW_THIS_IN_INITIALIZER_LIST(callback_factory_(this)),
      file_(base::kInvalidPlatformFileValue),
      callback_(),
      info_(NULL),
      read_buffer_(NULL) {
}

PPB_FileIO_Impl::~PPB_FileIO_Impl() {
  Close();
}

// static
PP_Resource PPB_FileIO_Impl::Create(PP_Instance pp_instance) {
  PluginInstance* instance = ResourceTracker::Get()->GetInstance(pp_instance);
  if (!instance)
    return 0;
  PPB_FileIO_Impl* file_io = new PPB_FileIO_Impl(instance);
  return file_io->GetReference();
}

PPB_FileIO_API* PPB_FileIO_Impl::AsPPB_FileIO_API() {
  return this;
}

int32_t PPB_FileIO_Impl::Open(PP_Resource pp_file_ref,
                              int32_t open_flags,
                              PP_CompletionCallback callback) {
  EnterResourceNoLock<PPB_FileRef_API> enter(pp_file_ref, true);
  if (enter.failed())
    return PP_ERROR_BADRESOURCE;
  PPB_FileRef_Impl* file_ref = static_cast<PPB_FileRef_Impl*>(enter.object());

  int32_t rv = CommonCallValidation(false, callback);
  if (rv != PP_OK)
    return rv;

  int flags = 0;
  if (!PepperFileOpenFlagsToPlatformFileFlags(open_flags, &flags))
    return PP_ERROR_BADARGUMENT;

  file_system_type_ = file_ref->GetFileSystemType();
  switch (file_system_type_) {
    case PP_FILESYSTEMTYPE_EXTERNAL:
      if (!instance()->delegate()->AsyncOpenFile(
              file_ref->GetSystemPath(), flags,
              callback_factory_.NewCallback(
                  &PPB_FileIO_Impl::AsyncOpenFileCallback)))
        return PP_ERROR_FAILED;
      break;
    case PP_FILESYSTEMTYPE_LOCALPERSISTENT:
    case PP_FILESYSTEMTYPE_LOCALTEMPORARY:
      if (!instance()->delegate()->AsyncOpenFileSystemURL(
              file_ref->GetFileSystemURL(), flags,
              callback_factory_.NewCallback(
                  &PPB_FileIO_Impl::AsyncOpenFileCallback)))
        return PP_ERROR_FAILED;
      break;
    default:
      return PP_ERROR_FAILED;
  }

  RegisterCallback(callback);
  return PP_OK_COMPLETIONPENDING;
}

int32_t PPB_FileIO_Impl::Query(PP_FileInfo_Dev* info,
                               PP_CompletionCallback callback) {
  int32_t rv = CommonCallValidation(true, callback);
  if (rv != PP_OK)
    return rv;

  if (!info)
    return PP_ERROR_BADARGUMENT;

  DCHECK(!info_);  // If |info_|, a callback should be pending (caught above).
  info_ = info;

  if (!base::FileUtilProxy::GetFileInfoFromPlatformFile(
          instance()->delegate()->GetFileThreadMessageLoopProxy(), file_,
          callback_factory_.NewCallback(&PPB_FileIO_Impl::QueryInfoCallback)))
    return PP_ERROR_FAILED;

  RegisterCallback(callback);
  return PP_OK_COMPLETIONPENDING;
}

int32_t PPB_FileIO_Impl::Touch(PP_Time last_access_time,
                      PP_Time last_modified_time,
                      PP_CompletionCallback callback) {
  int32_t rv = CommonCallValidation(true, callback);
  if (rv != PP_OK)
    return rv;

  if (!base::FileUtilProxy::Touch(
          instance()->delegate()->GetFileThreadMessageLoopProxy(),
          file_, base::Time::FromDoubleT(last_access_time),
          base::Time::FromDoubleT(last_modified_time),
          callback_factory_.NewCallback(&PPB_FileIO_Impl::StatusCallback)))
    return PP_ERROR_FAILED;

  RegisterCallback(callback);
  return PP_OK_COMPLETIONPENDING;
}

int32_t PPB_FileIO_Impl::Read(int64_t offset,
                              char* buffer,
                              int32_t bytes_to_read,
                              PP_CompletionCallback callback) {
  int32_t rv = CommonCallValidation(true, callback);
  if (rv != PP_OK)
    return rv;

  // If |read_buffer__|, a callback should be pending (caught above).
  DCHECK(!read_buffer_);
  read_buffer_ = buffer;

  if (!base::FileUtilProxy::Read(
          instance()->delegate()->GetFileThreadMessageLoopProxy(),
          file_, offset, bytes_to_read,
          callback_factory_.NewCallback(&PPB_FileIO_Impl::ReadCallback)))
    return PP_ERROR_FAILED;

  RegisterCallback(callback);
  return PP_OK_COMPLETIONPENDING;
}

int32_t PPB_FileIO_Impl::Write(int64_t offset,
                               const char* buffer,
                               int32_t bytes_to_write,
                               PP_CompletionCallback callback) {
  int32_t rv = CommonCallValidation(true, callback);
  if (rv != PP_OK)
    return rv;

  if (!base::FileUtilProxy::Write(
          instance()->delegate()->GetFileThreadMessageLoopProxy(),
          file_, offset, buffer, bytes_to_write,
          callback_factory_.NewCallback(&PPB_FileIO_Impl::WriteCallback)))
    return PP_ERROR_FAILED;

  RegisterCallback(callback);
  return PP_OK_COMPLETIONPENDING;
}

int32_t PPB_FileIO_Impl::SetLength(int64_t length,
                          PP_CompletionCallback callback) {
  int32_t rv = CommonCallValidation(true, callback);
  if (rv != PP_OK)
    return rv;

  if (!base::FileUtilProxy::Truncate(
          instance()->delegate()->GetFileThreadMessageLoopProxy(),
          file_, length,
          callback_factory_.NewCallback(&PPB_FileIO_Impl::StatusCallback)))
    return PP_ERROR_FAILED;

  RegisterCallback(callback);
  return PP_OK_COMPLETIONPENDING;
}

int32_t PPB_FileIO_Impl::Flush(PP_CompletionCallback callback) {
  int32_t rv = CommonCallValidation(true, callback);
  if (rv != PP_OK)
    return rv;

  if (!base::FileUtilProxy::Flush(
          instance()->delegate()->GetFileThreadMessageLoopProxy(), file_,
          callback_factory_.NewCallback(&PPB_FileIO_Impl::StatusCallback)))
    return PP_ERROR_FAILED;

  RegisterCallback(callback);
  return PP_OK_COMPLETIONPENDING;
}

void PPB_FileIO_Impl::Close() {
  if (file_ != base::kInvalidPlatformFileValue) {
    base::FileUtilProxy::Close(
        instance()->delegate()->GetFileThreadMessageLoopProxy(), file_, NULL);
    file_ = base::kInvalidPlatformFileValue;
  }
}

int32_t PPB_FileIO_Impl::GetOSFileDescriptor() {
#if defined(OS_POSIX)
  return file_;
#elif defined(OS_WIN)
  return reinterpret_cast<uintptr_t>(file_);
#else
#error "Platform not supported."
#endif
}

int32_t PPB_FileIO_Impl::WillWrite(int64_t offset,
                                   int32_t bytes_to_write,
                                   PP_CompletionCallback callback) {
  // TODO(dumi): implement me
  return PP_OK;
}

int32_t PPB_FileIO_Impl::WillSetLength(int64_t length,
                                       PP_CompletionCallback callback) {
  // TODO(dumi): implement me
  return PP_OK;
}

int32_t PPB_FileIO_Impl::CommonCallValidation(bool should_be_open,
                                              PP_CompletionCallback callback) {
  // Only asynchronous operation is supported.
  if (!callback.func) {
    NOTIMPLEMENTED();
    return PP_ERROR_BADARGUMENT;
  }

  if (should_be_open) {
    if (file_ == base::kInvalidPlatformFileValue)
      return PP_ERROR_FAILED;
  } else {
    if (file_ != base::kInvalidPlatformFileValue)
      return PP_ERROR_FAILED;
  }

  if (callback_.get() && !callback_->completed())
    return PP_ERROR_INPROGRESS;

  return PP_OK;
}

void PPB_FileIO_Impl::RegisterCallback(PP_CompletionCallback callback) {
  DCHECK(callback.func);
  DCHECK(!callback_.get() || callback_->completed());

  PP_Resource resource_id = GetReferenceNoAddRef();
  CHECK(resource_id);
  callback_ = new TrackedCompletionCallback(
      instance()->module()->GetCallbackTracker(), resource_id, callback);
}

void PPB_FileIO_Impl::RunPendingCallback(int32_t result) {
  scoped_refptr<TrackedCompletionCallback> callback;
  callback.swap(callback_);
  callback->Run(result);  // Will complete abortively if necessary.
}

void PPB_FileIO_Impl::StatusCallback(base::PlatformFileError error_code) {
  RunPendingCallback(PlatformFileErrorToPepperError(error_code));
}

void PPB_FileIO_Impl::AsyncOpenFileCallback(
    base::PlatformFileError error_code,
    base::PlatformFile file) {
  DCHECK(file_ == base::kInvalidPlatformFileValue);
  file_ = file;
  RunPendingCallback(PlatformFileErrorToPepperError(error_code));
}

void PPB_FileIO_Impl::QueryInfoCallback(
    base::PlatformFileError error_code,
    const base::PlatformFileInfo& file_info) {
  DCHECK(info_);
  if (error_code == base::PLATFORM_FILE_OK) {
    info_->size = file_info.size;
    info_->creation_time = file_info.creation_time.ToDoubleT();
    info_->last_access_time = file_info.last_accessed.ToDoubleT();
    info_->last_modified_time = file_info.last_modified.ToDoubleT();
    info_->system_type = file_system_type_;
    if (file_info.is_directory)
      info_->type = PP_FILETYPE_DIRECTORY;
    else
      info_->type = PP_FILETYPE_REGULAR;
  }
  info_ = NULL;
  RunPendingCallback(PlatformFileErrorToPepperError(error_code));
}

void PPB_FileIO_Impl::ReadCallback(base::PlatformFileError error_code,
                                   const char* data, int bytes_read) {
  DCHECK(data);
  DCHECK(read_buffer_);

  int rv;
  if (error_code == base::PLATFORM_FILE_OK) {
    rv = bytes_read;
    if (file_ != base::kInvalidPlatformFileValue)
      memcpy(read_buffer_, data, bytes_read);
  } else
    rv = PlatformFileErrorToPepperError(error_code);

  read_buffer_ = NULL;
  RunPendingCallback(rv);
}

void PPB_FileIO_Impl::WriteCallback(base::PlatformFileError error_code,
                                    int bytes_written) {
  if (error_code != base::PLATFORM_FILE_OK)
    RunPendingCallback(PlatformFileErrorToPepperError(error_code));
  else
    RunPendingCallback(bytes_written);
}

}  // namespace ppapi
}  // namespace webkit
