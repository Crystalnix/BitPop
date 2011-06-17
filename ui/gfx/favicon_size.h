// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_GFX_FAVICON_SIZE_H_
#define UI_GFX_FAVICON_SIZE_H_
#pragma once

#include "base/compiler_specific.h"

// Size (along each axis) of the favicon.
#if defined(TOUCH_UI)
const int kFaviconSize = 32;
#else
const int kFaviconSize = 16;
#endif

// If the width or height is bigger than the favicon size, a new width/height
// is calculated and returned in width/height that maintains the aspect
// ratio of the supplied values.
static void calc_favicon_target_size(int* width, int* height) ALLOW_UNUSED;

// static
void calc_favicon_target_size(int* width, int* height) {
  if (*width > kFaviconSize || *height > kFaviconSize) {
    // Too big, resize it maintaining the aspect ratio.
    float aspect_ratio = static_cast<float>(*width) /
                         static_cast<float>(*height);
    *height = kFaviconSize;
    *width = static_cast<int>(aspect_ratio * *height);
    if (*width > kFaviconSize) {
      *width = kFaviconSize;
      *height = static_cast<int>(*width / aspect_ratio);
    }
  }
}

#endif  // UI_GFX_FAVICON_SIZE_H_
