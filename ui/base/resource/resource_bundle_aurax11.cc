// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/base/resource/resource_bundle.h"

#include "base/command_line.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "ui/base/layout.h"
#include "ui/base/resource/resource_handle.h"
#include "ui/base/ui_base_paths.h"
#include "ui/base/ui_base_switches.h"
#include "ui/gfx/display.h"
#include "ui/gfx/image/image.h"

namespace {

FilePath GetResourcesPakFilePath(const std::string& pak_name) {
  FilePath path;
  if (PathService::Get(base::DIR_MODULE, &path))
    return path.AppendASCII(pak_name.c_str());

  // Return just the name of the pack file.
  return FilePath(pak_name.c_str());
}

bool ShouldLoad2xResources() {
  return gfx::Display::GetForcedDeviceScaleFactor() > 1.0f ||
      CommandLine::ForCurrentProcess()->HasSwitch(switches::kLoad2xResources);
}

}  // namespace

namespace ui {

void ResourceBundle::LoadCommonResources() {
  // Always load the 1x data pack first as the 2x data pack contains both 1x and
  // 2x images. The 1x data pack only has 1x images, thus passes in an accurate
  // scale factor to gfx::ImageSkia::AddRepresentation.

  AddDataPackFromPath(GetResourcesPakFilePath("chrome.pak"),
                      SCALE_FACTOR_100P);

  if (ui::GetDisplayLayout() == ui::LAYOUT_TOUCH) {
    // 1x touch
    AddDataPackFromPath(GetResourcesPakFilePath(
                        "theme_resources_touch_100_percent.pak"),
                        SCALE_FACTOR_100P);
    AddDataPackFromPath(GetResourcesPakFilePath(
                        "ui_resources_touch_100_percent.pak"),
                        SCALE_FACTOR_100P);
    if (ShouldLoad2xResources()) {
      // 2x touch
      AddDataPackFromPath(GetResourcesPakFilePath(
                          "theme_resources_touch_200_percent.pak"),
                          SCALE_FACTOR_200P);
      AddDataPackFromPath(GetResourcesPakFilePath(
                          "ui_resources_touch_200_percent.pak"),
                          SCALE_FACTOR_200P);
      AddDataPackFromPath(GetResourcesPakFilePath(
                          "webkit_resources_200_percent.pak"),
                          SCALE_FACTOR_200P);
    }
  } else {
    // 1x non touch
    AddDataPackFromPath(GetResourcesPakFilePath(
                        "theme_resources_100_percent.pak"),
                        SCALE_FACTOR_100P);
    AddDataPackFromPath(GetResourcesPakFilePath(
                        "ui_resources_100_percent.pak"),
                        SCALE_FACTOR_100P);
    if (ShouldLoad2xResources()) {
      // 2x non touch
      AddDataPackFromPath(GetResourcesPakFilePath(
                          "theme_resources_200_percent.pak"),
                          SCALE_FACTOR_200P);
      AddDataPackFromPath(GetResourcesPakFilePath(
                          "ui_resources_200_percent.pak"),
                          SCALE_FACTOR_200P);
      AddDataPackFromPath(GetResourcesPakFilePath(
                          "webkit_resources_200_percent.pak"),
                          SCALE_FACTOR_200P);
    }
  }
}

gfx::Image& ResourceBundle::GetNativeImageNamed(int resource_id, ImageRTL rtl) {
  // Flipped image is not used on ChromeOS.
  DCHECK_EQ(rtl, RTL_DISABLED);
  return GetImageNamed(resource_id);
}

}  // namespace ui
