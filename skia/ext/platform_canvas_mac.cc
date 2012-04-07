// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "skia/ext/platform_canvas.h"

#include "base/debug/trace_event.h"
#include "skia/ext/bitmap_platform_device.h"
#include "third_party/skia/include/core/SkTypes.h"

namespace skia {

PlatformCanvas::PlatformCanvas(int width, int height, bool is_opaque) {
  TRACE_EVENT2("skia", "PlatformCanvas::PlatformCanvas",
               "width", width, "height", height);
  initialize(width, height, is_opaque);
}

PlatformCanvas::PlatformCanvas(int width,
                               int height,
                               bool is_opaque,
                               CGContextRef context) {
  TRACE_EVENT2("skia", "PlatformCanvas::PlatformCanvas",
               "width", width, "height", height);
  initialize(context, width, height, is_opaque);
}

PlatformCanvas::PlatformCanvas(int width,
                               int height,
                               bool is_opaque,
                               uint8_t* data) {
  TRACE_EVENT2("skia", "PlatformCanvas::PlatformCanvas",
               "width", width, "height", height);
  initialize(width, height, is_opaque, data);
}

PlatformCanvas::~PlatformCanvas() {
}

bool PlatformCanvas::initialize(int width,
                                int height,
                                bool is_opaque,
                                uint8_t* data) {
  return initializeWithDevice(BitmapPlatformDevice::CreateWithData(
      data, width, height, is_opaque));
}

bool PlatformCanvas::initialize(CGContextRef context,
                                int width,
                                int height,
                                bool is_opaque) {
  return initializeWithDevice(BitmapPlatformDevice::Create(
      context, width, height, is_opaque));
}

}  // namespace skia
