// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_PLUGINS_PPAPI_PPB_FILE_SYSTEM_IMPL_H_
#define WEBKIT_PLUGINS_PPAPI_PPB_FILE_SYSTEM_IMPL_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "googleurl/src/gurl.h"
#include "ppapi/c/dev/pp_file_info_dev.h"
#include "ppapi/thunk/ppb_file_system_api.h"
#include "webkit/plugins/ppapi/resource.h"

struct PPB_FileSystem_Dev;

namespace webkit {
namespace ppapi {

class PluginInstance;

class PPB_FileSystem_Impl : public Resource,
                            public ::ppapi::thunk::PPB_FileSystem_API {
 public:
  PPB_FileSystem_Impl(PluginInstance* instance, PP_FileSystemType_Dev type);
  virtual ~PPB_FileSystem_Impl();

  static PP_Resource Create(PP_Instance instance, PP_FileSystemType_Dev type);

  // ResourceObjectBase overrides.
  virtual ::ppapi::thunk::PPB_FileSystem_API* AsPPB_FileSystem_API() OVERRIDE;

  PluginInstance* instance() { return instance_; }
  PP_FileSystemType_Dev type() { return type_; }
  const GURL& root_url() const { return root_url_; }
  void set_root_url(const GURL& root_url) { root_url_ = root_url; }
  bool opened() const { return opened_; }
  void set_opened(bool opened) { opened_ = opened; }

  // PPB_FileSystem_API implementation.
  virtual int32_t Open(int64_t expected_size,
                       PP_CompletionCallback callback) OVERRIDE;
  virtual PP_FileSystemType_Dev GetType() OVERRIDE;

 private:
  PluginInstance* instance_;
  PP_FileSystemType_Dev type_;
  GURL root_url_;
  bool opened_;
  bool called_open_;

  DISALLOW_COPY_AND_ASSIGN(PPB_FileSystem_Impl);
};

}  // namespace ppapi
}  // namespace webkit

#endif  // WEBKIT_PLUGINS_PPAPI_PPB_FILE_SYSTEM_IMPL_H_
