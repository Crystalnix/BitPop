// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PPAPI_PROXY_SERIALIZED_STRUCTS_H_
#define PPAPI_PROXY_SERIALIZED_STRUCTS_H_

#include <string>
#include <vector>

#include "base/shared_memory.h"
#include "build/build_config.h"
#include "ipc/ipc_platform_file.h"
#include "ppapi/c/pp_bool.h"
#include "ppapi/c/pp_instance.h"
#include "ppapi/c/pp_point.h"
#include "ppapi/c/pp_rect.h"
#include "ppapi/proxy/serialized_var.h"
#include "ppapi/shared_impl/host_resource.h"

struct PP_FontDescription_Dev;

namespace ppapi {
namespace proxy {

class Dispatcher;

// PP_FontDescript_Dev has to be redefined with a SerializedVar in place of
// the PP_Var used for the face name.
struct PPAPI_PROXY_EXPORT SerializedFontDescription {
  SerializedFontDescription();
  ~SerializedFontDescription();

  // Converts a PP_FontDescription_Dev to a SerializedFontDescription.
  //
  // If source_owns_ref is true, the reference owned by the
  // PP_FontDescription_Dev will be unchanged and the caller is responsible for
  // freeing it. When false, the SerializedFontDescription will take ownership
  // of the ref. This is the difference between serializing as an input value
  // (owns_ref = true) and an output value (owns_ref = true).
  void SetFromPPFontDescription(Dispatcher* dispatcher,
                                const PP_FontDescription_Dev& desc,
                                bool source_owns_ref);

  // Converts to a PP_FontDescription_Dev. The face name will have one ref
  // assigned to it on behalf of the caller.
  //
  // If dest_owns_ref is set, the resulting PP_FontDescription_Dev will keep a
  // reference to any strings we made on its behalf even when the
  // SerializedFontDescription goes away. When false, ownership of the ref will
  // stay with the SerializedFontDescription and the PP_FontDescription_Dev
  // will just refer to that one. This is the difference between deserializing
  // as an input value (owns_ref = false) and an output value (owns_ref = true).
  void SetToPPFontDescription(Dispatcher* dispatcher,
                              PP_FontDescription_Dev* desc,
                              bool dest_owns_ref) const;

  SerializedVar face;
  int32_t family;
  uint32_t size;
  int32_t weight;
  PP_Bool italic;
  PP_Bool small_caps;
  int32_t letter_spacing;
  int32_t word_spacing;
};

struct SerializedDirEntry {
  std::string name;
  bool is_dir;
};

struct PPBFlash_DrawGlyphs_Params {
  PPBFlash_DrawGlyphs_Params();
  ~PPBFlash_DrawGlyphs_Params();

  PP_Instance instance;
  ppapi::HostResource image_data;
  SerializedFontDescription font_desc;
  uint32_t color;
  PP_Point position;
  PP_Rect clip;
  float transformation[3][3];
  PP_Bool allow_subpixel_aa;
  std::vector<uint16_t> glyph_indices;
  std::vector<PP_Point> glyph_advances;
};

struct PPBURLLoader_UpdateProgress_Params {
  PP_Instance instance;
  ppapi::HostResource resource;
  int64_t bytes_sent;
  int64_t total_bytes_to_be_sent;
  int64_t bytes_received;
  int64_t total_bytes_to_be_received;
};

struct PPPVideoCapture_Buffer {
  ppapi::HostResource resource;
  uint32_t size;
  base::SharedMemoryHandle handle;
};

#if defined(OS_WIN)
typedef HANDLE ImageHandle;
#elif defined(OS_MACOSX) || defined(OS_ANDROID)
typedef base::SharedMemoryHandle ImageHandle;
#else
// On X Windows this is a SysV shared memory key.
typedef int ImageHandle;
#endif

}  // namespace proxy
}  // namespace ppapi

#endif  // PPAPI_PROXY_SERIALIZED_STRUCTS_H_
