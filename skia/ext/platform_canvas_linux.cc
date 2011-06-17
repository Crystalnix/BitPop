// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "skia/ext/platform_canvas.h"

#include <cairo/cairo.h>

#include "skia/ext/platform_device_linux.h"
#include "skia/ext/bitmap_platform_device_linux.h"
#include "third_party/skia/include/core/SkTypes.h"

namespace skia {

PlatformCanvas::PlatformCanvas(int width, int height, bool is_opaque)
    : SkCanvas(SkNEW(BitmapPlatformDeviceFactory)) {
  if (!initialize(width, height, is_opaque))
    SK_CRASH();
}

PlatformCanvas::PlatformCanvas(int width, int height, bool is_opaque,
                               uint8_t* data)
    : SkCanvas(SkNEW(BitmapPlatformDeviceFactory)) {
  if (!initialize(width, height, is_opaque, data))
    SK_CRASH();
}

PlatformCanvas::~PlatformCanvas() {
}
bool PlatformCanvas::initialize(int width, int height, bool is_opaque,
                                uint8_t* data) {
  return initializeWithDevice(BitmapPlatformDevice::Create(
      width, height, is_opaque, data));
}

cairo_t* PlatformCanvas::beginPlatformPaint() const {
  return getTopPlatformDevice().BeginPlatformPaint();
}

void PlatformCanvas::endPlatformPaint() const {
  getTopPlatformDevice().EndPlatformPaint();
}

}  // namespace skia
