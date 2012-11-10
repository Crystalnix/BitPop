// Copyright (c) 2012 The Chromium Authors. All rights reserved.
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
//
// Images can be empty, in which case they have no backing representation.
// Attempting to use an empty Image will result in a crash.

#ifndef UI_GFX_IMAGE_IMAGE_H_
#define UI_GFX_IMAGE_IMAGE_H_

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
class ImageSkia;
class ImageSkiaRep;

#if defined(TOOLKIT_GTK)
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
    kImageRepCairo,
    kImageRepSkia,
    kImageRepPNG,
  };

  typedef std::map<RepresentationType, internal::ImageRep*> RepresentationMap;

  // Creates an empty image with no representations.
  Image();

  // Creates a new image by copying the PNG-encoded input for use as the default
  // representation. For example (from an std::vector):
  // std::vector<unsigned char> png = ...;
  // gfx::Image image(&png.front(), png.size());
  Image(const unsigned char* png, size_t input_size);

  // Creates a new image by copying the ImageSkia for use as the default
  // representation.
  explicit Image(const ImageSkia& image);

  // Creates a new image by copying the image rep for use as the default
  // representation.
  explicit Image(const ImageSkiaRep& image_rep);

  // Creates a new image by copying the bitmap for use as the default
  // representation.
  // TODO(pkotwicz): Get rid of this constructor.
  explicit Image(const SkBitmap& bitmap);

#if defined(TOOLKIT_GTK)
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
  // the Image. Must only be called if IsEmpty() is false.
  const std::vector<unsigned char>* ToImagePNG() const;
  const SkBitmap* ToSkBitmap() const;
  const ImageSkia* ToImageSkia() const;
#if defined(TOOLKIT_GTK)
  GdkPixbuf* ToGdkPixbuf() const;
  CairoCachedSurface* const ToCairo() const;
#elif defined(OS_MACOSX)
  NSImage* ToNSImage() const;
#endif

  // Same as ToSkBitmap(), but returns a null SkBitmap if this image is empty.
  SkBitmap AsBitmap() const;

  // Same as ToImageSkia(), but returns an empty ImageSkia if this
  // image is empty.
  ImageSkia AsImageSkia() const;

#if defined(OS_MACOSX)
  // Same as ToSkBitmap(), but returns nil if this image is empty.
  NSImage* AsNSImage() const;
#endif

  // Performs a conversion, like above, but returns a copy of the result rather
  // than a weak pointer. The caller is responsible for deleting the result.
  // Note that the result is only a copy in terms of memory management; the
  // backing pixels are shared amongst all copies (a fact of each of the
  // converted representations, rather than a limitation imposed by Image) and
  // so the result should be considered immutable.
  std::vector<unsigned char>* CopyImagePNG() const;
  ImageSkia* CopyImageSkia() const;
  SkBitmap* CopySkBitmap() const;
#if defined(TOOLKIT_GTK)
  GdkPixbuf* CopyGdkPixbuf() const;
#elif defined(OS_MACOSX)
  NSImage* CopyNSImage() const;
#endif

  // DEPRECATED ----------------------------------------------------------------
  // Conversion handlers. These wrap the ToType() variants.
#if defined(OS_MACOSX)
  operator NSImage*() const;
#endif
  // ---------------------------------------------------------------------------

  // Inspects the representations map to see if the given type exists.
  bool HasRepresentation(RepresentationType type) const;

  // Returns the number of representations.
  size_t RepresentationCount() const;

  // Returns true if this Image has no representations.
  bool IsEmpty() const;

  // Swaps this image's internal representations with |other|.
  void SwapRepresentations(gfx::Image* other);

 private:
  // Returns the type of the default representation.
  RepresentationType DefaultRepresentationType() const;

  // Returns the ImageRep of the appropriate type or NULL if there is no
  // representation of that type (and must_exist is false).
  internal::ImageRep* GetRepresentation(
      RepresentationType rep_type, bool must_exist) const;

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
