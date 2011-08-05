// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/proxy/ppb_font_proxy.h"

#include "base/bind.h"
#include "ppapi/c/dev/ppb_font_dev.h"
#include "ppapi/proxy/plugin_dispatcher.h"
#include "ppapi/proxy/ppapi_messages.h"
#include "ppapi/proxy/ppb_image_data_proxy.h"
#include "ppapi/proxy/serialized_var.h"
#include "ppapi/shared_impl/ppapi_preferences.h"
#include "ppapi/shared_impl/resource_object_base.h"
#include "ppapi/thunk/enter.h"
#include "ppapi/thunk/ppb_image_data_api.h"
#include "ppapi/thunk/thunk.h"

using ppapi::thunk::PPB_ImageData_API;
using ppapi::WebKitForwarding;

namespace pp {
namespace proxy {

namespace {

bool PPTextRunToTextRun(const PP_TextRun_Dev* run,
                        WebKitForwarding::Font::TextRun* output) {
  const std::string* str = PluginVarTracker::GetInstance()->GetExistingString(
      run->text);
  if (!str)
    return false;

  output->text = *str;
  output->rtl = PPBoolToBool(run->rtl);
  output->override_direction = PPBoolToBool(run->override_direction);
  return true;
}

InterfaceProxy* CreateFontProxy(Dispatcher* dispatcher,
                                const void* target_interface) {
  return new PPB_Font_Proxy(dispatcher, target_interface);
}

}  // namespace

PPB_Font_Proxy::PPB_Font_Proxy(Dispatcher* dispatcher,
                               const void* target_interface)
    : InterfaceProxy(dispatcher, target_interface) {
}

PPB_Font_Proxy::~PPB_Font_Proxy() {
}

// static
const InterfaceProxy::Info* PPB_Font_Proxy::GetInfo() {
  static const Info info = {
    ::ppapi::thunk::GetPPB_Font_Thunk(),
    PPB_FONT_DEV_INTERFACE,
    INTERFACE_ID_PPB_FONT,
    false,
    &CreateFontProxy,
  };
  return &info;
}

::ppapi::thunk::PPB_Font_FunctionAPI* PPB_Font_Proxy::AsPPB_Font_FunctionAPI() {
  return this;
}

PP_Var PPB_Font_Proxy::GetFontFamilies(PP_Instance instance) {
  PluginDispatcher* dispatcher = PluginDispatcher::GetForInstance(instance);
  if (!dispatcher)
    return PP_MakeUndefined();

  // Assume the font families don't change, so we can cache the result globally.
  static std::string families;
  if (families.empty()) {
    dispatcher->SendToBrowser(
        new PpapiHostMsg_PPBFont_GetFontFamilies(&families));
  }

  PP_Var result;
  result.type = PP_VARTYPE_STRING;
  result.value.as_id = PluginVarTracker::GetInstance()->MakeString(families);
  return result;
}

bool PPB_Font_Proxy::OnMessageReceived(const IPC::Message& msg) {
  // There aren't any font messages.
  NOTREACHED();
  return false;
}

Font::Font(const HostResource& resource,
           const PP_FontDescription_Dev& desc)
    : PluginResource(resource),
      webkit_event_(false, false) {
  const std::string* face = PluginVarTracker::GetInstance()->GetExistingString(
      desc.face);

  WebKitForwarding* forwarding = GetDispatcher()->GetWebKitForwarding();

  WebKitForwarding::Font* result = NULL;
  RunOnWebKitThread(base::Bind(&WebKitForwarding::CreateFontForwarding,
                               base::Unretained(forwarding),
                               &webkit_event_, desc,
                               face ? *face : std::string(),
                               GetDispatcher()->preferences(),
                               &result));
  font_forwarding_.reset(result);
}

Font::~Font() {
}

ppapi::thunk::PPB_Font_API* Font::AsPPB_Font_API() {
  return this;
}

Font* Font::AsFont() {
  return this;
}

PP_Bool Font::Describe(PP_FontDescription_Dev* description,
                       PP_FontMetrics_Dev* metrics) {
  std::string face;
  PP_Bool result = PP_FALSE;
  RunOnWebKitThread(base::Bind(&WebKitForwarding::Font::Describe,
                               base::Unretained(font_forwarding_.get()),
                               &webkit_event_, description, &face, metrics,
                               &result));

  if (result == PP_TRUE) {
    description->face.type = PP_VARTYPE_STRING;
    description->face.value.as_id =
        PluginVarTracker::GetInstance()->MakeString(face);
  } else {
    description->face.type = PP_VARTYPE_UNDEFINED;
  }
  return result;
}

PP_Bool Font::DrawTextAt(PP_Resource pp_image_data,
                         const PP_TextRun_Dev* text,
                         const PP_Point* position,
                         uint32_t color,
                         const PP_Rect* clip,
                         PP_Bool image_data_is_opaque) {
  // Convert to an ImageData object.
  ppapi::ResourceObjectBase* image_base =
      ppapi::TrackerBase::Get()->GetResourceAPI(pp_image_data);
  if (!image_base)
    return PP_FALSE;
  PPB_ImageData_API* image_api = image_base->GetAs<PPB_ImageData_API>();
  if (!image_api)
    return PP_FALSE;
  ImageData* image_data = static_cast<ImageData*>(image_api);

  skia::PlatformCanvas* canvas = image_data->mapped_canvas();
  bool needs_unmapping = false;
  if (!canvas) {
    needs_unmapping = true;
    image_data->Map();
    canvas = image_data->mapped_canvas();
    if (!canvas)
      return PP_FALSE;  // Failure mapping.
  }

  WebKitForwarding::Font::TextRun run;
  if (!PPTextRunToTextRun(text, &run)) {
    if (needs_unmapping)
      image_data->Unmap();
    return PP_FALSE;
  }
  RunOnWebKitThread(base::Bind(
      &WebKitForwarding::Font::DrawTextAt,
      base::Unretained(font_forwarding_.get()),
      &webkit_event_,
      WebKitForwarding::Font::DrawTextParams(canvas, run, position, color,
                                             clip, image_data_is_opaque)));

  if (needs_unmapping)
    image_data->Unmap();
  return PP_TRUE;
}

int32_t Font::MeasureText(const PP_TextRun_Dev* text) {
  WebKitForwarding::Font::TextRun run;
  if (!PPTextRunToTextRun(text, &run))
    return -1;
  int32_t result = -1;
  RunOnWebKitThread(base::Bind(&WebKitForwarding::Font::MeasureText,
                               base::Unretained(font_forwarding_.get()),
                               &webkit_event_, run, &result));
  return result;
}

uint32_t Font::CharacterOffsetForPixel(const PP_TextRun_Dev* text,
                                       int32_t pixel_position) {
  WebKitForwarding::Font::TextRun run;
  if (!PPTextRunToTextRun(text, &run))
    return -1;
  uint32_t result = -1;
  RunOnWebKitThread(base::Bind(&WebKitForwarding::Font::CharacterOffsetForPixel,
                               base::Unretained(font_forwarding_.get()),
                               &webkit_event_, run, pixel_position, &result));
  return result;
}

int32_t Font::PixelOffsetForCharacter(const PP_TextRun_Dev* text,
                                      uint32_t char_offset) {
  WebKitForwarding::Font::TextRun run;
  if (!PPTextRunToTextRun(text, &run))
    return -1;
  int32_t result = -1;
  RunOnWebKitThread(base::Bind(&WebKitForwarding::Font::PixelOffsetForCharacter,
                               base::Unretained(font_forwarding_.get()),
                               &webkit_event_, run, char_offset, &result));
  return result;
}

void Font::RunOnWebKitThread(const base::Closure& task) {
  GetDispatcher()->PostToWebKitThread(FROM_HERE, task);
  webkit_event_.Wait();
}

}  // namespace proxy
}  // namespace pp
