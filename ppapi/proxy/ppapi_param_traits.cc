// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/proxy/ppapi_param_traits.h"

#include <string.h>  // For memcpy

#include "ppapi/c/pp_file_info.h"
#include "ppapi/c/pp_resource.h"
#include "ppapi/c/private/ppb_flash_tcp_socket.h"
#include "ppapi/proxy/ppapi_messages.h"
#include "ppapi/proxy/serialized_var.h"
#include "ppapi/proxy/serialized_flash_menu.h"
#include "ppapi/shared_impl/host_resource.h"

namespace IPC {

namespace {

// Deserializes a vector from IPC. This special version must be used instead
// of the default IPC version when the vector contains a SerializedVar, either
// directly or indirectly (i.e. a vector of objects that have a SerializedVar
// inside them).
//
// The default vector deserializer does resize and then we deserialize into
// those allocated slots. However, the implementation of vector (at least in
// GCC's implementation), creates a new empty object using the default
// constructor, and then sets the rest of the items to that empty one using the
// copy constructor.
//
// Since we allocate the inner class when you call the default constructor and
// transfer the inner class when you do operator=, the entire vector will end
// up referring to the same inner class. Deserializing into this will just end
// up overwriting the same item over and over, since all the SerializedVars
// will refer to the same thing.
//
// The solution is to make a new object for each deserialized item, and then
// add it to the vector one at a time.
template<typename T>
bool ReadVectorWithoutCopy(const Message* m,
                           void** iter,
                           std::vector<T>* output) {
  // This part is just a copy of the the default ParamTraits vector Read().
  int size;
  // ReadLength() checks for < 0 itself.
  if (!m->ReadLength(iter, &size))
    return false;
  // Resizing beforehand is not safe, see BUG 1006367 for details.
  if (INT_MAX / sizeof(T) <= static_cast<size_t>(size))
    return false;

  output->reserve(size);
  for (int i = 0; i < size; i++) {
    T cur;
    if (!ReadParam(m, iter, &cur))
      return false;
    output->push_back(cur);
  }
  return true;
}

// This serializes the vector of items to the IPC message in exactly the same
// way as the "regular" IPC vector serializer does. But having the code here
// saves us from having to copy this code into all ParamTraits that use the
// ReadVectorWithoutCopy function for deserializing.
template<typename T>
void WriteVectorWithoutCopy(Message* m, const std::vector<T>& p) {
  WriteParam(m, static_cast<int>(p.size()));
  for (size_t i = 0; i < p.size(); i++)
    WriteParam(m, p[i]);
}

}  // namespace

// PP_Bool ---------------------------------------------------------------------

// static
void ParamTraits<PP_Bool>::Write(Message* m, const param_type& p) {
  ParamTraits<bool>::Write(m, PP_ToBool(p));
}

// static
bool ParamTraits<PP_Bool>::Read(const Message* m, void** iter, param_type* r) {
  // We specifically want to be strict here about what types of input we accept,
  // which ParamTraits<bool> does for us. We don't want to deserialize "2" into
  // a PP_Bool, for example.
  bool result = false;
  if (!ParamTraits<bool>::Read(m, iter, &result))
    return false;
  *r = PP_FromBool(result);
  return true;
}

// static
void ParamTraits<PP_Bool>::Log(const param_type& p, std::string* l) {
}

// PP_FileInfo -------------------------------------------------------------

// static
void ParamTraits<PP_FileInfo>::Write(Message* m, const param_type& p) {
  ParamTraits<int64_t>::Write(m, p.size);
  ParamTraits<int>::Write(m, static_cast<int>(p.type));
  ParamTraits<int>::Write(m, static_cast<int>(p.system_type));
  ParamTraits<double>::Write(m, p.creation_time);
  ParamTraits<double>::Write(m, p.last_access_time);
  ParamTraits<double>::Write(m, p.last_modified_time);
}

// static
bool ParamTraits<PP_FileInfo>::Read(const Message* m, void** iter,
                                        param_type* r) {
  int type, system_type;
  if (!ParamTraits<int64_t>::Read(m, iter, &r->size) ||
      !ParamTraits<int>::Read(m, iter, &type) ||
      !ParamTraits<int>::Read(m, iter, &system_type) ||
      !ParamTraits<double>::Read(m, iter, &r->creation_time) ||
      !ParamTraits<double>::Read(m, iter, &r->last_access_time) ||
      !ParamTraits<double>::Read(m, iter, &r->last_modified_time))
    return false;
  if (type != PP_FILETYPE_REGULAR &&
      type != PP_FILETYPE_DIRECTORY &&
      type != PP_FILETYPE_OTHER)
    return false;
  r->type = static_cast<PP_FileType>(type);
  if (system_type != PP_FILESYSTEMTYPE_INVALID &&
      system_type != PP_FILESYSTEMTYPE_EXTERNAL &&
      system_type != PP_FILESYSTEMTYPE_LOCALPERSISTENT &&
      system_type != PP_FILESYSTEMTYPE_LOCALTEMPORARY)
    return false;
  r->system_type = static_cast<PP_FileSystemType>(system_type);
  return true;
}

// static
void ParamTraits<PP_FileInfo>::Log(const param_type& p, std::string* l) {
}

// PP_NetAddress_Private -------------------------------------------------------

// static
void ParamTraits<PP_NetAddress_Private>::Write(Message* m,
                                               const param_type& p) {
  WriteParam(m, p.size);
  m->WriteBytes(p.data, static_cast<int>(p.size));
}

// static
bool ParamTraits<PP_NetAddress_Private>::Read(const Message* m,
                                              void** iter,
                                              param_type* p) {
  uint16 size;
  if (!ReadParam(m, iter, &size))
    return false;
  if (size > sizeof(p->data))
    return false;
  p->size = size;

  const char* data;
  if (!m->ReadBytes(iter, &data, size))
    return false;
  memcpy(p->data, data, size);
  return true;
}

// static
void ParamTraits<PP_NetAddress_Private>::Log(const param_type& p,
                                             std::string* l) {
  l->append("<PP_NetAddress_Private (");
  LogParam(p.size, l);
  l->append(" bytes)>");
}

// PP_ObjectProperty -----------------------------------------------------------

// static
void ParamTraits<PP_ObjectProperty>::Write(Message* m, const param_type& p) {
  // FIXME(brettw);
}

// static
bool ParamTraits<PP_ObjectProperty>::Read(const Message* m,
                                          void** iter,
                                          param_type* r) {
  // FIXME(brettw);
  return true;
}

// static
void ParamTraits<PP_ObjectProperty>::Log(const param_type& p, std::string* l) {
}

// PPBFlash_DrawGlyphs_Params --------------------------------------------------

// static
void ParamTraits<ppapi::proxy::PPBFlash_DrawGlyphs_Params>::Write(
    Message* m,
    const param_type& p) {
  ParamTraits<PP_Instance>::Write(m, p.instance);
  ParamTraits<ppapi::HostResource>::Write(m, p.image_data);
  ParamTraits<ppapi::proxy::SerializedFontDescription>::Write(m, p.font_desc);
  ParamTraits<uint32_t>::Write(m, p.color);
  ParamTraits<PP_Point>::Write(m, p.position);
  ParamTraits<PP_Rect>::Write(m, p.clip);
  ParamTraits<float>::Write(m, p.transformation[0][0]);
  ParamTraits<float>::Write(m, p.transformation[0][1]);
  ParamTraits<float>::Write(m, p.transformation[0][2]);
  ParamTraits<float>::Write(m, p.transformation[1][0]);
  ParamTraits<float>::Write(m, p.transformation[1][1]);
  ParamTraits<float>::Write(m, p.transformation[1][2]);
  ParamTraits<float>::Write(m, p.transformation[2][0]);
  ParamTraits<float>::Write(m, p.transformation[2][1]);
  ParamTraits<float>::Write(m, p.transformation[2][2]);
  ParamTraits<PP_Bool>::Write(m, p.allow_subpixel_aa);
  ParamTraits<std::vector<uint16_t> >::Write(m, p.glyph_indices);
  ParamTraits<std::vector<PP_Point> >::Write(m, p.glyph_advances);
}

// static
bool ParamTraits<ppapi::proxy::PPBFlash_DrawGlyphs_Params>::Read(
    const Message* m,
    void** iter,
    param_type* r) {
  return
      ParamTraits<PP_Instance>::Read(m, iter, &r->instance) &&
      ParamTraits<ppapi::HostResource>::Read(m, iter, &r->image_data) &&
      ParamTraits<ppapi::proxy::SerializedFontDescription>::Read(m, iter,
                                                              &r->font_desc) &&
      ParamTraits<uint32_t>::Read(m, iter, &r->color) &&
      ParamTraits<PP_Point>::Read(m, iter, &r->position) &&
      ParamTraits<PP_Rect>::Read(m, iter, &r->clip) &&
      ParamTraits<float>::Read(m, iter, &r->transformation[0][0]) &&
      ParamTraits<float>::Read(m, iter, &r->transformation[0][1]) &&
      ParamTraits<float>::Read(m, iter, &r->transformation[0][2]) &&
      ParamTraits<float>::Read(m, iter, &r->transformation[1][0]) &&
      ParamTraits<float>::Read(m, iter, &r->transformation[1][1]) &&
      ParamTraits<float>::Read(m, iter, &r->transformation[1][2]) &&
      ParamTraits<float>::Read(m, iter, &r->transformation[2][0]) &&
      ParamTraits<float>::Read(m, iter, &r->transformation[2][1]) &&
      ParamTraits<float>::Read(m, iter, &r->transformation[2][2]) &&
      ParamTraits<PP_Bool>::Read(m, iter, &r->allow_subpixel_aa) &&
      ParamTraits<std::vector<uint16_t> >::Read(m, iter, &r->glyph_indices) &&
      ParamTraits<std::vector<PP_Point> >::Read(m, iter, &r->glyph_advances) &&
      r->glyph_indices.size() == r->glyph_advances.size();
}

// static
void ParamTraits<ppapi::proxy::PPBFlash_DrawGlyphs_Params>::Log(
    const param_type& p,
    std::string* l) {
}

// PPB_FileRef_CreateInfo ------------------------------------------------------

// static
void ParamTraits<ppapi::PPB_FileRef_CreateInfo>::Write(Message* m,
                                                       const param_type& p) {
  ParamTraits<ppapi::HostResource>::Write(m, p.resource);
  ParamTraits<int>::Write(m, p.file_system_type);
  ParamTraits<std::string>::Write(m, p.path);
  ParamTraits<std::string>::Write(m, p.name);
}

// static
bool ParamTraits<ppapi::PPB_FileRef_CreateInfo>::Read(const Message* m,
                                                      void** iter,
                                                      param_type* r) {
  return
      ParamTraits<ppapi::HostResource>::Read(m, iter, &r->resource) &&
      ParamTraits<int>::Read(m, iter, &r->file_system_type) &&
      ParamTraits<std::string>::Read(m, iter, &r->path) &&
      ParamTraits<std::string>::Read(m, iter, &r->name);
}

// static
void ParamTraits<ppapi::PPB_FileRef_CreateInfo>::Log(const param_type& p,
                                                     std::string* l) {
}

// PPBURLLoader_UpdateProgress_Params ------------------------------------------

// static
void ParamTraits<ppapi::proxy::PPBURLLoader_UpdateProgress_Params>::Write(
    Message* m,
    const param_type& p) {
  ParamTraits<PP_Instance>::Write(m, p.instance);
  ParamTraits<ppapi::HostResource>::Write(m, p.resource);
  ParamTraits<int64_t>::Write(m, p.bytes_sent);
  ParamTraits<int64_t>::Write(m, p.total_bytes_to_be_sent);
  ParamTraits<int64_t>::Write(m, p.bytes_received);
  ParamTraits<int64_t>::Write(m, p.total_bytes_to_be_received);
}

// static
bool ParamTraits<ppapi::proxy::PPBURLLoader_UpdateProgress_Params>::Read(
    const Message* m,
    void** iter,
    param_type* r) {
  return
      ParamTraits<PP_Instance>::Read(m, iter, &r->instance) &&
      ParamTraits<ppapi::HostResource>::Read(m, iter, &r->resource) &&
      ParamTraits<int64_t>::Read(m, iter, &r->bytes_sent) &&
      ParamTraits<int64_t>::Read(m, iter, &r->total_bytes_to_be_sent) &&
      ParamTraits<int64_t>::Read(m, iter, &r->bytes_received) &&
      ParamTraits<int64_t>::Read(m, iter, &r->total_bytes_to_be_received);
}

// static
void ParamTraits<ppapi::proxy::PPBURLLoader_UpdateProgress_Params>::Log(
    const param_type& p,
    std::string* l) {
}

// SerializedDirEntry ----------------------------------------------------------

// static
void ParamTraits<ppapi::proxy::SerializedDirEntry>::Write(Message* m,
                                                          const param_type& p) {
  ParamTraits<std::string>::Write(m, p.name);
  ParamTraits<bool>::Write(m, p.is_dir);
}

// static
bool ParamTraits<ppapi::proxy::SerializedDirEntry>::Read(const Message* m,
                                                         void** iter,
                                                         param_type* r) {
  return ParamTraits<std::string>::Read(m, iter, &r->name) &&
         ParamTraits<bool>::Read(m, iter, &r->is_dir);
}

// static
void ParamTraits<ppapi::proxy::SerializedDirEntry>::Log(const param_type& p,
                                                        std::string* l) {
}

// ppapi::proxy::SerializedFontDescription -------------------------------------

// static
void ParamTraits<ppapi::proxy::SerializedFontDescription>::Write(
    Message* m,
    const param_type& p) {
  ParamTraits<ppapi::proxy::SerializedVar>::Write(m, p.face);
  ParamTraits<int32_t>::Write(m, p.family);
  ParamTraits<uint32_t>::Write(m, p.size);
  ParamTraits<int32_t>::Write(m, p.weight);
  ParamTraits<PP_Bool>::Write(m, p.italic);
  ParamTraits<PP_Bool>::Write(m, p.small_caps);
  ParamTraits<int32_t>::Write(m, p.letter_spacing);
  ParamTraits<int32_t>::Write(m, p.word_spacing);
}

// static
bool ParamTraits<ppapi::proxy::SerializedFontDescription>::Read(
    const Message* m,
    void** iter,
    param_type* r) {
  return
      ParamTraits<ppapi::proxy::SerializedVar>::Read(m, iter, &r->face) &&
      ParamTraits<int32_t>::Read(m, iter, &r->family) &&
      ParamTraits<uint32_t>::Read(m, iter, &r->size) &&
      ParamTraits<int32_t>::Read(m, iter, &r->weight) &&
      ParamTraits<PP_Bool>::Read(m, iter, &r->italic) &&
      ParamTraits<PP_Bool>::Read(m, iter, &r->small_caps) &&
      ParamTraits<int32_t>::Read(m, iter, &r->letter_spacing) &&
      ParamTraits<int32_t>::Read(m, iter, &r->word_spacing);
}

// static
void ParamTraits<ppapi::proxy::SerializedFontDescription>::Log(
    const param_type& p,
    std::string* l) {
}

// HostResource ----------------------------------------------------------------

// static
void ParamTraits<ppapi::HostResource>::Write(Message* m,
                                             const param_type& p) {
  ParamTraits<PP_Instance>::Write(m, p.instance());
  ParamTraits<PP_Resource>::Write(m, p.host_resource());
}

// static
bool ParamTraits<ppapi::HostResource>::Read(const Message* m,
                                            void** iter,
                                            param_type* r) {
  PP_Instance instance;
  PP_Resource resource;
  if (!ParamTraits<PP_Instance>::Read(m, iter, &instance) ||
      !ParamTraits<PP_Resource>::Read(m, iter, &resource))
    return false;
  r->SetHostResource(instance, resource);
  return true;
}

// static
void ParamTraits<ppapi::HostResource>::Log(const param_type& p,
                                           std::string* l) {
}

// SerializedVar ---------------------------------------------------------------

// static
void ParamTraits<ppapi::proxy::SerializedVar>::Write(Message* m,
                                                     const param_type& p) {
  p.WriteToMessage(m);
}

// static
bool ParamTraits<ppapi::proxy::SerializedVar>::Read(const Message* m,
                                                    void** iter,
                                                    param_type* r) {
  return r->ReadFromMessage(m, iter);
}

// static
void ParamTraits<ppapi::proxy::SerializedVar>::Log(const param_type& p,
                                                   std::string* l) {
}

// std::vector<SerializedVar> --------------------------------------------------

void ParamTraits< std::vector<ppapi::proxy::SerializedVar> >::Write(
    Message* m,
    const param_type& p) {
  WriteVectorWithoutCopy(m, p);
}

// static
bool ParamTraits< std::vector<ppapi::proxy::SerializedVar> >::Read(
    const Message* m,
    void** iter,
    param_type* r) {
  return ReadVectorWithoutCopy(m, iter, r);
}

// static
void ParamTraits< std::vector<ppapi::proxy::SerializedVar> >::Log(
    const param_type& p,
    std::string* l) {
}

// std::vector<PPB_FileRef_CreateInfo> -----------------------------------------

void ParamTraits< std::vector<ppapi::PPB_FileRef_CreateInfo> >::Write(
    Message* m,
    const param_type& p) {
  WriteVectorWithoutCopy(m, p);
}

// static
bool ParamTraits< std::vector<ppapi::PPB_FileRef_CreateInfo> >::Read(
    const Message* m,
    void** iter,
    param_type* r) {
  return ReadVectorWithoutCopy(m, iter, r);
}

// static
void ParamTraits< std::vector<ppapi::PPB_FileRef_CreateInfo> >::Log(
    const param_type& p,
    std::string* l) {
}

// SerializedFlashMenu ---------------------------------------------------------

// static
void ParamTraits<ppapi::proxy::SerializedFlashMenu>::Write(
    Message* m,
    const param_type& p) {
  p.WriteToMessage(m);
}

// static
bool ParamTraits<ppapi::proxy::SerializedFlashMenu>::Read(const Message* m,
                                                          void** iter,
                                                          param_type* r) {
  return r->ReadFromMessage(m, iter);
}

// static
void ParamTraits<ppapi::proxy::SerializedFlashMenu>::Log(const param_type& p,
                                                         std::string* l) {
}

}  // namespace IPC
