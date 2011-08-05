// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PPAPI_SHARED_IMPL_RESOURCE_OBJECT_BASE_H_
#define PPAPI_SHARED_IMPL_RESOURCE_OBJECT_BASE_H_

#include <stddef.h>  // For NULL.

#define FOR_ALL_PPAPI_RESOURCE_APIS(F) \
  F(PPB_Audio_API) \
  F(PPB_AudioConfig_API) \
  F(PPB_AudioTrusted_API) \
  F(PPB_Broker_API) \
  F(PPB_Buffer_API) \
  F(PPB_DirectoryReader_API) \
  F(PPB_FileChooser_API) \
  F(PPB_FileIO_API) \
  F(PPB_FileRef_API) \
  F(PPB_FileSystem_API) \
  F(PPB_Find_API) \
  F(PPB_Font_API) \
  F(PPB_Graphics2D_API) \
  F(PPB_ImageData_API)

namespace ppapi {

// Forward declare all the resource APIs.
namespace thunk {
#define DECLARE_RESOURCE_CLASS(RESOURCE) class RESOURCE;
FOR_ALL_PPAPI_RESOURCE_APIS(DECLARE_RESOURCE_CLASS)
#undef DECLARE_RESOURCE_CLASS
}  // namespace thunk

class ResourceObjectBase {
 public:
  virtual ~ResourceObjectBase();

  // Dynamic casting for this object. Returns the pointer to the given type if
  // Inheritance-based dynamic casting for this object. Returns the pointer to
  // the given type if it's supported. Derived classes override the functions
  // they support to return the interface.
  #define DEFINE_TYPE_GETTER(RESOURCE) \
    virtual thunk::RESOURCE* As##RESOURCE();
  FOR_ALL_PPAPI_RESOURCE_APIS(DEFINE_TYPE_GETTER)
  #undef DEFINE_TYPE_GETTER

  // Template-based dynamic casting. See specializations below.
  template <typename T> T* GetAs() { return NULL; }
};

// Template-based dynamic casting. These specializations forward to the
// AsXXX virtual functions to return whether the given type is supported.
#define DEFINE_RESOURCE_CAST(RESOURCE) \
  template<> inline thunk::RESOURCE* ResourceObjectBase::GetAs() { \
    return As##RESOURCE(); \
  }
FOR_ALL_PPAPI_RESOURCE_APIS(DEFINE_RESOURCE_CAST)
#undef DEFINE_RESOURCE_CAST

}  // namespace ppapi

#endif  // PPAPI_SHARED_IMPL_RESOURCE_OBJECT_BASE_H_
