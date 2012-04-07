// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gfx/image/image_util.h"

#include "base/memory/scoped_ptr.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/codec/jpeg_codec.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/image/image.h"

namespace gfx {

Image* ImageFromPNGEncodedData(const unsigned char* input, size_t input_size) {
  scoped_ptr<SkBitmap> favicon_bitmap(new SkBitmap());
  if (gfx::PNGCodec::Decode(input, input_size, favicon_bitmap.get()))
    return new Image(favicon_bitmap.release());

  return NULL;
}

bool PNGEncodedDataFromImage(const Image& image,
                             std::vector<unsigned char>* dst) {
  const SkBitmap& bitmap = image;
  return gfx::PNGCodec::EncodeBGRASkBitmap(bitmap, false, dst);
}

bool JPEGEncodedDataFromImage(const Image& image, int quality,
                              std::vector<unsigned char>* dst) {
  const SkBitmap& bitmap = image;
  SkAutoLockPixels bitmap_lock(bitmap);

  if (!bitmap.readyToDraw())
    return false;

  return gfx::JPEGCodec::Encode(
          reinterpret_cast<unsigned char*>(bitmap.getAddr32(0, 0)),
          gfx::JPEGCodec::FORMAT_BGRA, bitmap.width(),
          bitmap.height(),
          static_cast<int>(bitmap.rowBytes()), quality,
          dst);
}

}
