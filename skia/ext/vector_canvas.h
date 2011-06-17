// Copyright (c) 2006-2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SKIA_EXT_VECTOR_CANVAS_H_
#define SKIA_EXT_VECTOR_CANVAS_H_
#pragma once

#include "skia/ext/platform_canvas.h"

namespace skia {

class PlatformDevice;

// This class is a specialization of the regular PlatformCanvas. It is designed
// to work with a VectorDevice to manage platform-specific drawing. It allows
// using both Skia operations and platform-specific operations. It *doesn't*
// support reading back from the bitmap backstore since it is not used.
class SK_API VectorCanvas : public PlatformCanvas {
 public:
  // Ownership of |device| is transfered to VectorCanvas.
  explicit VectorCanvas(PlatformDevice* device);
  virtual ~VectorCanvas();

  virtual SkBounder* setBounder(SkBounder* bounder);
  virtual SkDrawFilter* setDrawFilter(SkDrawFilter* filter);

 private:
  // Returns true if the top device is vector based and not bitmap based.
  bool IsTopDeviceVectorial() const;

  // Copy & assign are not supported.
  VectorCanvas(const VectorCanvas&);
  const VectorCanvas& operator=(const VectorCanvas&);
};

}  // namespace skia

#endif  // SKIA_EXT_VECTOR_CANVAS_H_

