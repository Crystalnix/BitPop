// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/gpu/gl_string_manager.h"

#include "chrome/browser/browser_process.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/gpu_data_manager.h"

GLStringManager::GLStringManager() {
}

GLStringManager::~GLStringManager() {
}

void GLStringManager::Initialize() {
  // On MacOSX or Windows, preliminary GPUInfo is enough.
#if defined(OS_LINUX)
  // We never remove this observer from GpuDataManager.
  content::GpuDataManager::GetInstance()->AddObserver(this);

  PrefService* local_state = g_browser_process->local_state();
  if (!local_state)
    return;

  local_state->RegisterStringPref(prefs::kGLVendorString, gl_vendor_);
  local_state->RegisterStringPref(prefs::kGLRendererString, gl_renderer_);
  local_state->RegisterStringPref(prefs::kGLVersionString, gl_version_);

  gl_vendor_ = local_state->GetString(prefs::kGLVendorString);
  gl_renderer_ = local_state->GetString(prefs::kGLRendererString);
  gl_version_ = local_state->GetString(prefs::kGLVersionString);

  if (!gl_vendor_.empty() || !gl_renderer_.empty() || !gl_version_.empty()) {
    content::GpuDataManager::GetInstance()->SetGLStrings(
        gl_vendor_, gl_renderer_, gl_version_);
  }
#endif
}

void GLStringManager::OnGpuInfoUpdate() {
  std::string gl_vendor, gl_renderer, gl_version;
  content::GpuDataManager::GetInstance()->GetGLStrings(
      &gl_vendor, &gl_renderer, &gl_version);

  PrefService* local_state = g_browser_process->local_state();
  if (!local_state)
    return;

  if (!gl_vendor.empty() && gl_vendor != gl_vendor_) {
    gl_vendor_ = gl_vendor;
    local_state->SetString(prefs::kGLVendorString, gl_vendor_);
  }
  if (!gl_renderer.empty() && gl_renderer != gl_renderer_) {
    gl_renderer_ = gl_renderer;
    local_state->SetString(prefs::kGLRendererString, gl_renderer_);
  }
  if (!gl_version.empty() && gl_version != gl_version_) {
    gl_version_ = gl_version;
    local_state->SetString(prefs::kGLVersionString, gl_version_);
  }
}

