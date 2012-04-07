// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/plugins/ppapi/ppb_flash_file_impl.h"

#include <string.h>

#include <string>

#include "ppapi/c/pp_file_info.h"
#include "ppapi/c/ppb_file_io.h"
#include "ppapi/c/private/ppb_flash_file.h"
#include "ppapi/shared_impl/file_type_conversion.h"
#include "ppapi/shared_impl/time_conversion.h"
#include "ppapi/thunk/enter.h"
#include "webkit/plugins/ppapi/common.h"
#include "webkit/plugins/ppapi/file_path.h"
#include "webkit/plugins/ppapi/host_globals.h"
#include "webkit/plugins/ppapi/plugin_delegate.h"
#include "webkit/plugins/ppapi/plugin_module.h"
#include "webkit/plugins/ppapi/ppapi_plugin_instance.h"
#include "webkit/plugins/ppapi/ppb_file_ref_impl.h"
#include "webkit/plugins/ppapi/resource_helper.h"

#if defined(OS_WIN)
#include "base/utf_string_conversions.h"
#endif

using ppapi::thunk::EnterResource;
using ppapi::thunk::PPB_FileRef_API;
using ppapi::TimeToPPTime;

namespace webkit {
namespace ppapi {

namespace {

void FreeDirContents(PP_Instance instance, PP_DirContents_Dev* contents) {
  DCHECK(contents);
  for (int32_t i = 0; i < contents->count; ++i) {
    delete [] contents->entries[i].name;
  }
  delete [] contents->entries;
  delete contents;
}

}  // namespace

// PPB_Flash_File_ModuleLocal_Impl ---------------------------------------------

namespace {

bool CreateThreadAdapterForInstance(PP_Instance instance) {
  return false;  // No multithreaded access allowed.
}

void ClearThreadAdapterForInstance(PP_Instance instance) {
}

int32_t OpenModuleLocalFile(PP_Instance pp_instance,
                            const char* path,
                            int32_t mode,
                            PP_FileHandle* file) {
  int flags = 0;
  if (!path ||
      !::ppapi::PepperFileOpenFlagsToPlatformFileFlags(mode, &flags) ||
      !file)
    return PP_ERROR_BADARGUMENT;

  PluginInstance* instance = HostGlobals::Get()->GetInstance(pp_instance);
  if (!instance)
    return PP_ERROR_FAILED;

  base::PlatformFile base_file;
  base::PlatformFileError result = instance->delegate()->OpenFile(
      PepperFilePath::MakeModuleLocal(instance->module(), path),
      flags,
      &base_file);
  *file = base_file;
  return ::ppapi::PlatformFileErrorToPepperError(result);
}

int32_t RenameModuleLocalFile(PP_Instance pp_instance,
                              const char* from_path,
                              const char* to_path) {
  if (!from_path || !to_path)
    return PP_ERROR_BADARGUMENT;

  PluginInstance* instance = HostGlobals::Get()->GetInstance(pp_instance);
  if (!instance)
    return PP_ERROR_FAILED;

  base::PlatformFileError result = instance->delegate()->RenameFile(
      PepperFilePath::MakeModuleLocal(instance->module(), from_path),
      PepperFilePath::MakeModuleLocal(instance->module(), to_path));
  return ::ppapi::PlatformFileErrorToPepperError(result);
}

int32_t DeleteModuleLocalFileOrDir(PP_Instance pp_instance,
                                   const char* path,
                                   PP_Bool recursive) {
  if (!path)
    return PP_ERROR_BADARGUMENT;

  PluginInstance* instance = HostGlobals::Get()->GetInstance(pp_instance);
  if (!instance)
    return PP_ERROR_FAILED;

  base::PlatformFileError result = instance->delegate()->DeleteFileOrDir(
      PepperFilePath::MakeModuleLocal(instance->module(), path),
      PPBoolToBool(recursive));
  return ::ppapi::PlatformFileErrorToPepperError(result);
}

int32_t CreateModuleLocalDir(PP_Instance pp_instance, const char* path) {
  if (!path)
    return PP_ERROR_BADARGUMENT;

  PluginInstance* instance = HostGlobals::Get()->GetInstance(pp_instance);
  if (!instance)
    return PP_ERROR_FAILED;

  base::PlatformFileError result = instance->delegate()->CreateDir(
      PepperFilePath::MakeModuleLocal(instance->module(), path));
  return ::ppapi::PlatformFileErrorToPepperError(result);
}

int32_t QueryModuleLocalFile(PP_Instance pp_instance,
                             const char* path,
                             PP_FileInfo* info) {
  if (!path || !info)
    return PP_ERROR_BADARGUMENT;

  PluginInstance* instance = HostGlobals::Get()->GetInstance(pp_instance);
  if (!instance)
    return PP_ERROR_FAILED;

  base::PlatformFileInfo file_info;
  base::PlatformFileError result = instance->delegate()->QueryFile(
      PepperFilePath::MakeModuleLocal(instance->module(), path),
      &file_info);
  if (result == base::PLATFORM_FILE_OK) {
    info->size = file_info.size;
    info->creation_time = TimeToPPTime(file_info.creation_time);
    info->last_access_time = TimeToPPTime(file_info.last_accessed);
    info->last_modified_time = TimeToPPTime(file_info.last_modified);
    info->system_type = PP_FILESYSTEMTYPE_EXTERNAL;
    if (file_info.is_directory)
      info->type = PP_FILETYPE_DIRECTORY;
    else
      info->type = PP_FILETYPE_REGULAR;
  }
  return ::ppapi::PlatformFileErrorToPepperError(result);
}

int32_t GetModuleLocalDirContents(PP_Instance pp_instance,
                                  const char* path,
                                  PP_DirContents_Dev** contents) {
  if (!path || !contents)
    return PP_ERROR_BADARGUMENT;
  PluginInstance* instance = HostGlobals::Get()->GetInstance(pp_instance);
  if (!instance)
    return PP_ERROR_FAILED;

  *contents = NULL;
  DirContents pepper_contents;
  base::PlatformFileError result = instance->delegate()->GetDirContents(
      PepperFilePath::MakeModuleLocal(instance->module(), path),
      &pepper_contents);

  if (result != base::PLATFORM_FILE_OK)
    return ::ppapi::PlatformFileErrorToPepperError(result);

  *contents = new PP_DirContents_Dev;
  size_t count = pepper_contents.size();
  (*contents)->count = count;
  (*contents)->entries = new PP_DirEntry_Dev[count];
  for (size_t i = 0; i < count; ++i) {
    PP_DirEntry_Dev& entry = (*contents)->entries[i];
#if defined(OS_WIN)
    const std::string& name = UTF16ToUTF8(pepper_contents[i].name.value());
#else
    const std::string& name = pepper_contents[i].name.value();
#endif
    size_t size = name.size() + 1;
    char* name_copy = new char[size];
    memcpy(name_copy, name.c_str(), size);
    entry.name = name_copy;
    entry.is_dir = BoolToPPBool(pepper_contents[i].is_dir);
  }
  return PP_OK;
}

const PPB_Flash_File_ModuleLocal ppb_flash_file_modulelocal = {
  &CreateThreadAdapterForInstance,
  &ClearThreadAdapterForInstance,
  &OpenModuleLocalFile,
  &RenameModuleLocalFile,
  &DeleteModuleLocalFileOrDir,
  &CreateModuleLocalDir,
  &QueryModuleLocalFile,
  &GetModuleLocalDirContents,
  &FreeDirContents,
};

}  // namespace

// static
const PPB_Flash_File_ModuleLocal*
    PPB_Flash_File_ModuleLocal_Impl::GetInterface() {
  return &ppb_flash_file_modulelocal;
}

// PPB_Flash_File_FileRef_Impl -------------------------------------------------

namespace {

int32_t OpenFileRefFile(PP_Resource file_ref_id,
                        int32_t mode,
                        PP_FileHandle* file) {
  int flags = 0;
  if (!::ppapi::PepperFileOpenFlagsToPlatformFileFlags(mode, &flags) || !file)
    return PP_ERROR_BADARGUMENT;

  EnterResource<PPB_FileRef_API> enter(file_ref_id, true);
  if (enter.failed())
    return PP_ERROR_BADRESOURCE;
  PPB_FileRef_Impl* file_ref = static_cast<PPB_FileRef_Impl*>(enter.object());

  PluginInstance* instance = ResourceHelper::GetPluginInstance(file_ref);
  if (!instance)
    return PP_ERROR_FAILED;

  base::PlatformFile base_file;
  base::PlatformFileError result = instance->delegate()->OpenFile(
      PepperFilePath::MakeAbsolute(file_ref->GetSystemPath()),
      flags,
      &base_file);
  *file = base_file;
  return ::ppapi::PlatformFileErrorToPepperError(result);
}

int32_t QueryFileRefFile(PP_Resource file_ref_id,
                         PP_FileInfo* info) {
  EnterResource<PPB_FileRef_API> enter(file_ref_id, true);
  if (enter.failed())
    return PP_ERROR_BADRESOURCE;
  PPB_FileRef_Impl* file_ref = static_cast<PPB_FileRef_Impl*>(enter.object());

  PluginInstance* instance = ResourceHelper::GetPluginInstance(file_ref);
  if (!instance)
    return PP_ERROR_FAILED;

  base::PlatformFileInfo file_info;
  base::PlatformFileError result = instance->delegate()->QueryFile(
      PepperFilePath::MakeAbsolute(file_ref->GetSystemPath()),
      &file_info);
  if (result == base::PLATFORM_FILE_OK) {
    info->size = file_info.size;
    info->creation_time = TimeToPPTime(file_info.creation_time);
    info->last_access_time = TimeToPPTime(file_info.last_accessed);
    info->last_modified_time = TimeToPPTime(file_info.last_modified);
    info->system_type = PP_FILESYSTEMTYPE_EXTERNAL;
    if (file_info.is_directory)
      info->type = PP_FILETYPE_DIRECTORY;
    else
      info->type = PP_FILETYPE_REGULAR;
  }
  return ::ppapi::PlatformFileErrorToPepperError(result);
}

const PPB_Flash_File_FileRef ppb_flash_file_fileref = {
  &OpenFileRefFile,
  &QueryFileRefFile,
};

}  // namespace

// static
const PPB_Flash_File_FileRef* PPB_Flash_File_FileRef_Impl::GetInterface() {
  return &ppb_flash_file_fileref;
}

}  // namespace ppapi
}  // namespace webkit
