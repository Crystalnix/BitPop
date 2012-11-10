// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/cpp/private/flash.h"

#include <string.h>

#include "ppapi/c/pp_bool.h"
#include "ppapi/c/pp_errors.h"
#include "ppapi/cpp/dev/font_dev.h"
#include "ppapi/cpp/image_data.h"
#include "ppapi/cpp/instance_handle.h"
#include "ppapi/cpp/module.h"
#include "ppapi/cpp/module_impl.h"
#include "ppapi/cpp/point.h"
#include "ppapi/cpp/rect.h"
#include "ppapi/cpp/url_request_info.h"
#include "ppapi/cpp/var.h"
#include "ppapi/c/private/ppb_flash.h"
#include "ppapi/c/private/ppb_flash_print.h"

namespace pp {

namespace {

template <> const char* interface_name<PPB_Flash_12_5>() {
  return PPB_FLASH_INTERFACE_12_5;
}

template <> const char* interface_name<PPB_Flash_12_4>() {
  return PPB_FLASH_INTERFACE_12_4;
}

template <> const char* interface_name<PPB_Flash_12_3>() {
  return PPB_FLASH_INTERFACE_12_3;
}

template <> const char* interface_name<PPB_Flash_Print_1_0>() {
  return PPB_FLASH_PRINT_INTERFACE_1_0;
}

// The combined Flash interface is all Flash v12.* interfaces. All v12
// interfaces just append one or more functions to the previous one, so we can
// have this meta one at the most recent version. Function pointers will be
// null if they're not supported on the current Chrome version.
bool initialized_combined_interface = false;
PPB_Flash flash_12_combined_interface;

// Makes sure that the most recent version is loaded into the combined
// interface struct above. Any unsupported functions will be NULL. If there
// is no Flash interface supported, all functions will be NULL.
void InitializeCombinedInterface() {
  if (initialized_combined_interface)
    return;
  if (has_interface<PPB_Flash_12_5>()) {
    memcpy(&flash_12_combined_interface, get_interface<PPB_Flash_12_5>(),
           sizeof(PPB_Flash_12_5));
  } else if (has_interface<PPB_Flash_12_4>()) {
    memcpy(&flash_12_combined_interface, get_interface<PPB_Flash_12_4>(),
           sizeof(PPB_Flash_12_4));
  } else if (has_interface<PPB_Flash_12_3>()) {
    memcpy(&flash_12_combined_interface, get_interface<PPB_Flash_12_3>(),
           sizeof(PPB_Flash_12_3));
  }
  initialized_combined_interface = true;
}

}  // namespace

namespace flash {

// static
bool Flash::IsAvailable() {
  return has_interface<PPB_Flash_12_5>() ||
         has_interface<PPB_Flash_12_4>() ||
         has_interface<PPB_Flash_12_3>();
}

// static
void Flash::SetInstanceAlwaysOnTop(const InstanceHandle& instance,
                                   bool on_top) {
  InitializeCombinedInterface();
  if (flash_12_combined_interface.SetInstanceAlwaysOnTop) {
    flash_12_combined_interface.SetInstanceAlwaysOnTop(
        instance.pp_instance(), PP_FromBool(on_top));
  }
}

// static
bool Flash::DrawGlyphs(const InstanceHandle& instance,
                       ImageData* image,
                       const FontDescription_Dev& font_desc,
                       uint32_t color,
                       const Point& position,
                       const Rect& clip,
                       const float transformation[3][3],
                       bool allow_subpixel_aa,
                       uint32_t glyph_count,
                       const uint16_t glyph_indices[],
                       const PP_Point glyph_advances[]) {
  InitializeCombinedInterface();
  if (flash_12_combined_interface.DrawGlyphs) {
    return PP_ToBool(flash_12_combined_interface.DrawGlyphs(
        instance.pp_instance(),
        image->pp_resource(),
        &font_desc.pp_font_description(),
        color,
        &position.pp_point(),
        &clip.pp_rect(),
        transformation,
        PP_FromBool(allow_subpixel_aa),
        glyph_count,
        glyph_indices,
        glyph_advances));
  }
  return false;
}

// static
Var Flash::GetProxyForURL(const InstanceHandle& instance,
                          const std::string& url) {
  InitializeCombinedInterface();
  if (flash_12_combined_interface.GetProxyForURL) {
    return Var(PASS_REF, flash_12_combined_interface.GetProxyForURL(
        instance.pp_instance(), url.c_str()));
  }
  return Var();
}

// static
int32_t Flash::Navigate(const URLRequestInfo& request_info,
                        const std::string& target,
                        bool from_user_action) {
  InitializeCombinedInterface();
  if (flash_12_combined_interface.Navigate) {
    return flash_12_combined_interface.Navigate(
        request_info.pp_resource(),
        target.c_str(),
        PP_FromBool(from_user_action));
  }
  return PP_ERROR_FAILED;
}

// static
void Flash::RunMessageLoop(const InstanceHandle& instance) {
  InitializeCombinedInterface();
  if (flash_12_combined_interface.RunMessageLoop)
    flash_12_combined_interface.RunMessageLoop(instance.pp_instance());
}

// static
void Flash::QuitMessageLoop(const InstanceHandle& instance) {
  InitializeCombinedInterface();
  if (flash_12_combined_interface.QuitMessageLoop)
    flash_12_combined_interface.QuitMessageLoop(instance.pp_instance());
}

// static
double Flash::GetLocalTimeZoneOffset(const InstanceHandle& instance,
                                     PP_Time t) {
  InitializeCombinedInterface();
  if (flash_12_combined_interface.GetLocalTimeZoneOffset) {
    return flash_12_combined_interface.GetLocalTimeZoneOffset(
        instance.pp_instance(), t);
  }
  return 0.0;
}

// static
Var Flash::GetCommandLineArgs(Module* module) {
  InitializeCombinedInterface();
  if (flash_12_combined_interface.GetCommandLineArgs) {
    return Var(
        PASS_REF,
        flash_12_combined_interface.GetCommandLineArgs(module->pp_module()));
  }
  return Var();
}

// static
void Flash::PreloadFontWin(const void* logfontw) {
  InitializeCombinedInterface();
  if (flash_12_combined_interface.PreloadFontWin)
    return flash_12_combined_interface.PreloadFontWin(logfontw);
}

// static
bool Flash::IsRectTopmost(const InstanceHandle& instance, const Rect& rect) {
  InitializeCombinedInterface();
  if (flash_12_combined_interface.IsRectTopmost) {
    return PP_ToBool(flash_12_combined_interface.IsRectTopmost(
        instance.pp_instance(), &rect.pp_rect()));
  }
  return false;
}

// static
void Flash::UpdateActivity(const InstanceHandle& instance) {
  InitializeCombinedInterface();
  if (flash_12_combined_interface.UpdateActivity)
    flash_12_combined_interface.UpdateActivity(instance.pp_instance());
}

// static
Var Flash::GetDeviceID(const InstanceHandle& instance) {
  InitializeCombinedInterface();
  if (flash_12_combined_interface.GetDeviceID) {
    return Var(PASS_REF,
               flash_12_combined_interface.GetDeviceID(instance.pp_instance()));
  }
  return Var();
}

// static
Var Flash::GetSetting(const InstanceHandle& instance, PP_FlashSetting setting) {
  InitializeCombinedInterface();
  if (flash_12_combined_interface.GetSetting) {
    return Var(PASS_REF,
               flash_12_combined_interface.GetSetting(instance.pp_instance(),
                                                      setting));
  }
  if (flash_12_combined_interface.GetSettingInt) {
    // All the |PP_FlashSetting|s supported by |GetSettingInt()| return
    // "booleans" (0 for false, 1 for true, -1 for undefined/unsupported/error).
    int32_t result = flash_12_combined_interface.GetSettingInt(
        instance.pp_instance(), setting);
    if (result == 0 || result == 1)
      return Var(!!result);
  }

  return Var();
}

// static
bool Flash::SetCrashData(const InstanceHandle& instance,
                         PP_FlashCrashKey key,
                         const pp::Var& value) {
  InitializeCombinedInterface();
  if (flash_12_combined_interface.SetCrashData) {
    return PP_ToBool(
        flash_12_combined_interface.SetCrashData(instance.pp_instance(),
                                                 key, value.pp_var()));
  }
  return false;
}

// static
bool Flash::InvokePrinting(const InstanceHandle& instance) {
  if (has_interface<PPB_Flash_Print_1_0>()) {
    get_interface<PPB_Flash_Print_1_0>()->InvokePrinting(
        instance.pp_instance());
    return true;
  }
  return false;
}

}  // namespace flash
}  // namespace pp
