// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// An Image wraps an image any flavor, be it platform-native GdkBitmap/NSImage,
// or a SkBitmap. This also provides easy conversion to other image types
// through operator overloading. It will cache the converted representations
// internally to prevent double-conversion.
//
// The lifetime of both the initial representation and any converted ones are
// tied to the lifetime of the Image's internal storage. To allow Images to be
// cheaply passed around by value, the actual image data is stored in a ref-
// counted member. When all Images referencing this storage are deleted, the
// actual representations are deleted, too.

#ifndef UI_GFX_IMAGE_IMAGE_H_
#define UI_GFX_IMAGE_IMAGE_H_
#pragma once

#include <map>
#include <vector>

#include "base/basictypes.h"
#include "base/gtest_prod_util.h"
#include "base/memory/ref_counted.h"
#include "ui/base/ui_export.h"
#include "ui/gfx/native_widget_types.h"

class SkBitmap;

namespace {
class ImageTest;
class ImageMacTest;
}

namespace gfx {

#if defined(TOOLKIT_USES_GTK)
class CairoCachedSurface;
#endif

namespace internal {
class ImageRep;
class ImageStorage;
}

class UI_EXPORT Image {
 public:
  enum RepresentationType {
    kImageRepGdk,
    kImageRepCocoa,
    kImageRepCairoCache,
    kImageRepSkia,
  };

  typedef std::map<RepresentationType, internal::ImageRep*> RepresentationMap;

  // Creates a new image with the default representation. The object will take
  // ownership of the image.
  explicit Image(const SkBitmap* bitmap);

  // To create an Image that supports multiple resolutions pass a vector
  // of bitmaps, one for each resolution.
  explicit Image(const std::vector<const SkBitmap*>& bitmaps);

#if defined(TOOLKIT_USES_GTK)
  // Does not increase |pixbuf|'s reference count; expects to take ownership.
  explicit Image(GdkPixbuf* pixbuf);
#elif defined(OS_MACOSX)
  // Does not retain |image|; expects to take ownership.
  // A single NSImage object can contain multiple bitmaps so there's no reason
  // to pass a vector of these.
  explicit Image(NSImage* image);
#endif

  // Initializes a new Image by AddRef()ing |other|'s internal storage.
  Image(const Image& other);

  // Copies a reference to |other|'s storage.
  Image& operator=(const Image& other);

  // Deletes the image and, if the only owner of the storage, all of its cached
  // representations.
  ~Image();

  // Converts the Image to the desired representation and stores it internally.
  // The returned result is a weak pointer owned by and scoped to the life of
  // the Image.
  const SkBitmap* ToSkBitmap() const;
#if defined(TOOLKIT_USES_GTK)
  GdkPixbuf* ToGdkPixbuf() const;
  CairoCachedSurface* const ToCairo() const;
#elif defined(OS_MACOSX)
  NSImage* ToNSImage() const;
#endif

  // Performs a conversion, like above, but returns a copy of the result rather
  // than a weak pointer. The caller is responsible for deleting the result.
  // Note that the result is only a copy in terms of memory management; the
  // backing pixels are shared amongst all copies (a fact of each of the
  // converted representations, rather than a limitation imposed by Image) and
  // so the result should be considered immutable.
  const SkBitmap* CopySkBitmap() const;
#if defined(TOOLKIT_USES_GTK)
  GdkPixbuf* CopyGdkPixbuf() const;
#elif defined(OS_MACOSX)
  NSImage* CopyNSImage() const;
#endif

  // DEPRECATED ----------------------------------------------------------------
  // Conversion handlers. These wrap the ToType() variants.
  operator const SkBitmap*() const;
  operator const SkBitmap&() const;
#if defined(TOOLKIT_USES_GTK)
  operator GdkPixbuf*() const;
#elif defined(OS_MACOSX)
  operator NSImage*() const;
#endif
  // ---------------------------------------------------------------------------

  // Gets the number of bitmaps in this image. This may cause a conversion
  // to a bitmap representation. Note, this function and GetSkBitmapAtIndex()
  // are primarily meant to be used by the theme provider.
  size_t GetNumberOfSkBitmaps() const;

  // Gets the bitmap at the given index. This may cause a conversion
  // to a bitmap representation. Note, the internal ordering of bitmaps is not
  // guaranteed.
  const SkBitmap* GetSkBitmapAtIndex(size_t index) const;

  // Inspects the representations map to see if the given type exists.
  bool HasRepresentation(RepresentationType type) const;

  // Returns the number of representations.
  size_t RepresentationCount() const;

  // Swaps this image's internal representations with |other|.
  void SwapRepresentations(gfx::Image* other);

 private:
  // Returns the ImageRep for the default representation.
  internal::ImageRep* DefaultRepresentation() const;

  // Returns a ImageRep for the given representation type, converting and
  // caching if necessary.
  internal::ImageRep* GetRepresentation(RepresentationType rep) const;

  // Stores a representation into the map.
  void AddRepresentation(internal::ImageRep* rep) const;

  // Internal class that holds all the representations. This allows the Image to
  // be cheaply copied.
  scoped_refptr<internal::ImageStorage> storage_;

  friend class ::ImageTest;
  friend class ::ImageMacTest;
};

}  // namespace gfx

#endif  // UI_GFX_IMAGE_IMAGE_H_
