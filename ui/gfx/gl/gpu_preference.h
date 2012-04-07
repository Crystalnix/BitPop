// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_GFX_GL_GPU_PREFERENCE_H_
#define UI_GFX_GL_GPU_PREFERENCE_H_
#pragma once

namespace gfx {

// On dual-GPU systems, expresses a preference for using the integrated
// or discrete GPU. On systems that have dual-GPU support (see
// GLContext::SupportsDualGpus), resource sharing only works between
// contexts that are created with the same GPU preference.
//
// This API will likely need to be adjusted as the functionality is
// implemented on more operating systems.
enum GpuPreference {
  PreferIntegratedGpu,
  PreferDiscreteGpu
};

}  // namespace gfx

#endif  // UI_GFX_GL_GPU_PREFERENCE_H_
