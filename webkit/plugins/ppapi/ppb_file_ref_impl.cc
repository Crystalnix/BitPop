// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/plugins/ppapi/ppb_file_ref_impl.h"

#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "googleurl/src/gurl.h"
#include "ppapi/c/pp_errors.h"
#include "ppapi/thunk/enter.h"
#include "ppapi/thunk/ppb_file_system_api.h"
#include "webkit/plugins/ppapi/common.h"
#include "webkit/plugins/ppapi/file_callbacks.h"
#include "webkit/plugins/ppapi/plugin_delegate.h"
#include "webkit/plugins/ppapi/plugin_module.h"
#include "webkit/plugins/ppapi/ppapi_plugin_instance.h"
#include "webkit/plugins/ppapi/ppb_directory_reader_impl.h"
#include "webkit/plugins/ppapi/ppb_file_system_impl.h"
#include "webkit/plugins/ppapi/var.h"

using ppapi::thunk::EnterResourceNoLock;
using ppapi::thunk::PPB_FileRef_API;
using ppapi::thunk::PPB_FileSystem_API;

namespace webkit {
namespace ppapi {

namespace {

bool IsValidLocalPath(const std::string& path) {
  // The path must start with '/'
  if (path.empty() || path[0] != '/')
    return false;

  // The path must contain valid UTF-8 characters.
  if (!IsStringUTF8(path))
    return false;

  return true;
}

void TrimTrailingSlash(std::string* path) {
  // If this path ends with a slash, then normalize it away unless path is the
  // root path.
  if (path->size() > 1 && path->at(path->size() - 1) == '/')
    path->erase(path->size() - 1, 1);
}

}  // namespace

PPB_FileRef_Impl::PPB_FileRef_Impl()
    : Resource(NULL),
      file_system_(NULL) {
}

PPB_FileRef_Impl::PPB_FileRef_Impl(
    PluginInstance* instance,
    scoped_refptr<PPB_FileSystem_Impl> file_system,
    const std::string& validated_path)
    : Resource(instance),
      file_system_(file_system),
      virtual_path_(validated_path) {
}

PPB_FileRef_Impl::PPB_FileRef_Impl(PluginInstance* instance,
                                   const FilePath& external_file_path)
    : Resource(instance),
      file_system_(NULL),
      system_path_(external_file_path) {
}

PPB_FileRef_Impl::~PPB_FileRef_Impl() {
}

// static
PP_Resource PPB_FileRef_Impl::Create(PP_Resource pp_file_system,
                                     const char* path) {
  EnterResourceNoLock<PPB_FileSystem_API> enter(pp_file_system, true);
  if (enter.failed())
    return 0;

  PPB_FileSystem_Impl* file_system =
      static_cast<PPB_FileSystem_Impl*>(enter.object());
  if (!file_system->instance())
    return 0;

  std::string validated_path(path);
  if (!IsValidLocalPath(validated_path))
    return 0;
  TrimTrailingSlash(&validated_path);

  PPB_FileRef_Impl* file_ref =
      new PPB_FileRef_Impl(file_system->instance(),
                           file_system, validated_path);
  return file_ref->GetReference();
}

PPB_FileRef_API* PPB_FileRef_Impl::AsPPB_FileRef_API() {
  return this;
}

PPB_FileRef_Impl* PPB_FileRef_Impl::AsPPB_FileRef_Impl() {
  return this;
}

PP_FileSystemType_Dev PPB_FileRef_Impl::GetFileSystemType() const {
  // When the file ref exists but there's no explicit filesystem object
  // associated with it, that means it's an "external" filesystem.
  if (!file_system_)
    return PP_FILESYSTEMTYPE_EXTERNAL;
  return file_system_->type();
}

PP_Var PPB_FileRef_Impl::GetName() const {
  std::string result;
  if (GetFileSystemType() == PP_FILESYSTEMTYPE_EXTERNAL) {
    FilePath::StringType path = system_path_.value();
    size_t pos = path.rfind(FilePath::kSeparators[0]);
    DCHECK(pos != FilePath::StringType::npos);
#if defined(OS_WIN)
    result = WideToUTF8(path.substr(pos + 1));
#elif defined(OS_POSIX)
    result = path.substr(pos + 1);
#else
#error "Unsupported platform."
#endif
  } else if (virtual_path_.size() == 1 && virtual_path_[0] == '/') {
    result = virtual_path_;
  } else {
    // There should always be a leading slash at least!
    size_t pos = virtual_path_.rfind('/');
    DCHECK(pos != std::string::npos);
    result = virtual_path_.substr(pos + 1);
  }

  return StringVar::StringToPPVar(instance()->module(), result);
}

PP_Var PPB_FileRef_Impl::GetPath() const {
  if (GetFileSystemType() == PP_FILESYSTEMTYPE_EXTERNAL)
    return PP_MakeUndefined();
  return StringVar::StringToPPVar(instance()->module(), virtual_path_);
}

PP_Resource PPB_FileRef_Impl::GetParent() {
  if (GetFileSystemType() == PP_FILESYSTEMTYPE_EXTERNAL)
    return 0;

  // There should always be a leading slash at least!
  size_t pos = virtual_path_.rfind('/');
  DCHECK(pos != std::string::npos);

  // If the path is "/foo", then we want to include the slash.
  if (pos == 0)
    pos++;
  std::string parent_path = virtual_path_.substr(0, pos);

  scoped_refptr<PPB_FileRef_Impl> parent_ref(
      new PPB_FileRef_Impl(instance(), file_system_, parent_path));
  return parent_ref->GetReference();
}

int32_t PPB_FileRef_Impl::MakeDirectory(PP_Bool make_ancestors,
                                        PP_CompletionCallback callback) {
  if (!IsValidNonExternalFileSystem())
    return PP_ERROR_NOACCESS;
  if (!instance()->delegate()->MakeDirectory(
          GetFileSystemURL(), PP_ToBool(make_ancestors),
          new FileCallbacks(instance()->module()->AsWeakPtr(),
                            GetReferenceNoAddRef(), callback,
                            NULL, NULL, NULL)))
    return PP_ERROR_FAILED;
  return PP_OK_COMPLETIONPENDING;
}

int32_t PPB_FileRef_Impl::Touch(PP_Time last_access_time,
                                PP_Time last_modified_time,
                                PP_CompletionCallback callback) {
  if (!IsValidNonExternalFileSystem())
    return PP_ERROR_NOACCESS;
  if (!instance()->delegate()->Touch(
          GetFileSystemURL(),
          base::Time::FromDoubleT(last_access_time),
          base::Time::FromDoubleT(last_modified_time),
          new FileCallbacks(instance()->module()->AsWeakPtr(),
                            GetReferenceNoAddRef(), callback,
                            NULL, NULL, NULL)))
    return PP_ERROR_FAILED;
  return PP_OK_COMPLETIONPENDING;
}

int32_t PPB_FileRef_Impl::Delete(PP_CompletionCallback callback) {
  if (!IsValidNonExternalFileSystem())
    return PP_ERROR_NOACCESS;
  if (!instance()->delegate()->Delete(
          GetFileSystemURL(),
          new FileCallbacks(instance()->module()->AsWeakPtr(),
                            GetReferenceNoAddRef(), callback,
                            NULL, NULL, NULL)))
    return PP_ERROR_FAILED;
  return PP_OK_COMPLETIONPENDING;
}

int32_t PPB_FileRef_Impl::Rename(PP_Resource new_pp_file_ref,
                                 PP_CompletionCallback callback) {
  EnterResourceNoLock<PPB_FileRef_API> enter(new_pp_file_ref, true);
  if (enter.failed())
    return PP_ERROR_BADRESOURCE;
  PPB_FileRef_Impl* new_file_ref =
      static_cast<PPB_FileRef_Impl*>(enter.object());

  if (!IsValidNonExternalFileSystem() ||
      file_system_.get() != new_file_ref->file_system_.get())
    return PP_ERROR_NOACCESS;

  // TODO(viettrungluu): Also cancel when the new file ref is destroyed?
  // http://crbug.com/67624
  if (!instance()->delegate()->Rename(
          GetFileSystemURL(), new_file_ref->GetFileSystemURL(),
          new FileCallbacks(instance()->module()->AsWeakPtr(),
                            GetReferenceNoAddRef(), callback,
                            NULL, NULL, NULL)))
    return PP_ERROR_FAILED;
  return PP_OK_COMPLETIONPENDING;
}

FilePath PPB_FileRef_Impl::GetSystemPath() const {
  if (GetFileSystemType() != PP_FILESYSTEMTYPE_EXTERNAL) {
    NOTREACHED();
    return FilePath();
  }
  return system_path_;
}

GURL PPB_FileRef_Impl::GetFileSystemURL() const {
  if (GetFileSystemType() != PP_FILESYSTEMTYPE_LOCALPERSISTENT &&
      GetFileSystemType() != PP_FILESYSTEMTYPE_LOCALTEMPORARY) {
    NOTREACHED();
    return GURL();
  }
  if (!virtual_path_.size())
    return file_system_->root_url();
  // Since |virtual_path_| starts with a '/', it looks like an absolute path.
  // We need to trim off the '/' before calling Resolve, as FileSystem URLs
  // start with a storage type identifier that looks like a path segment.
  // TODO(ericu): Switch this to use Resolve after fixing GURL to understand
  // FileSystem URLs.
  return GURL(file_system_->root_url().spec() + virtual_path_.substr(1));
}

bool PPB_FileRef_Impl::IsValidNonExternalFileSystem() const {
  return file_system_ && file_system_->opened() &&
      file_system_->type() != PP_FILESYSTEMTYPE_EXTERNAL;
}

}  // namespace ppapi
}  // namespace webkit
