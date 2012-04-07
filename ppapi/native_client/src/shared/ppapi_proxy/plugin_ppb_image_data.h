// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE file.

#ifndef NATIVE_CLIENT_SRC_SHARED_PPAPI_PROXY_PLUGIN_IMAGE_DATA_H_
#define NATIVE_CLIENT_SRC_SHARED_PPAPI_PROXY_PLUGIN_IMAGE_DATA_H_

#include "native_client/src/include/nacl_macros.h"
#include "native_client/src/shared/ppapi_proxy/plugin_resource.h"
#include "ppapi/c/ppb_image_data.h"

namespace ppapi_proxy {

// Implements the untrusted side of the PPB_ImageData interface.
class PluginImageData : public PluginResource {
 public:
  PluginImageData();
  virtual ~PluginImageData();

  static const PPB_ImageData* GetInterface();

  bool InitFromBrowserResource(PP_Resource resource);
  void* Map();
  void Unmap();

  const PP_ImageDataDesc& desc() const { return desc_; }
 private:
  IMPLEMENT_RESOURCE(PluginImageData);
  NACL_DISALLOW_COPY_AND_ASSIGN(PluginImageData);
  PP_ImageDataDesc desc_;
  int shm_fd_;
  int32_t shm_size_;
  void* addr_;
};

}  // namespace ppapi_proxy

#endif  // NATIVE_CLIENT_SRC_SHARED_PPAPI_PROXY_PLUGIN_IMAGE_DATA_H_
