// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gfx/image/image.h"

#include <algorithm>

#include "base/logging.h"
#include "base/stl_util.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/size.h"

#if defined(TOOLKIT_USES_GTK)
#include <gdk/gdk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>
#include "ui/gfx/canvas_skia.h"
#include "ui/gfx/gtk_util.h"
#include "ui/gfx/image/cairo_cached_surface.h"
#elif defined(OS_MACOSX)
#include "base/mac/mac_util.h"
#include "skia/ext/skia_utils_mac.h"
#endif

namespace gfx {

namespace internal {

#if defined(OS_MACOSX)
// This is a wrapper around gfx::NSImageToSkBitmap() because this cross-platform
// file cannot include the [square brackets] of ObjC.
bool NSImageToSkBitmaps(NSImage* image, std::vector<const SkBitmap*>* bitmaps);
#endif

#if defined(TOOLKIT_USES_GTK)
const SkBitmap* GdkPixbufToSkBitmap(GdkPixbuf* pixbuf) {
  CHECK(pixbuf);
  gfx::CanvasSkia canvas(gfx::Size(gdk_pixbuf_get_width(pixbuf),
                                   gdk_pixbuf_get_height(pixbuf)),
                         /*is_opaque=*/false);
  skia::ScopedPlatformPaint scoped_platform_paint(canvas.sk_canvas());
  cairo_t* cr = scoped_platform_paint.GetPlatformSurface();
  gdk_cairo_set_source_pixbuf(cr, pixbuf, 0, 0);
  cairo_paint(cr);
  return new SkBitmap(canvas.ExtractBitmap());
}
#endif

class ImageRepSkia;
class ImageRepGdk;
class ImageRepCairoCached;
class ImageRepCocoa;

// An ImageRep is the object that holds the backing memory for an Image. Each
// RepresentationType has an ImageRep subclass that is responsible for freeing
// the memory that the ImageRep holds. When an ImageRep is created, it expects
// to take ownership of the image, without having to retain it or increase its
// reference count.
class ImageRep {
 public:
  explicit ImageRep(Image::RepresentationType rep) : type_(rep) {}

  // Deletes the associated pixels of an ImageRep.
  virtual ~ImageRep() {}

  // Cast helpers ("fake RTTI").
  ImageRepSkia* AsImageRepSkia() {
    CHECK_EQ(type_, Image::kImageRepSkia);
    return reinterpret_cast<ImageRepSkia*>(this);
  }

#if defined(TOOLKIT_USES_GTK)
  ImageRepGdk* AsImageRepGdk() {
    CHECK_EQ(type_, Image::kImageRepGdk);
    return reinterpret_cast<ImageRepGdk*>(this);
  }

  ImageRepCairoCached* AsImageRepCairo() {
    CHECK_EQ(type_, Image::kImageRepCairoCache);
    return reinterpret_cast<ImageRepCairoCached*>(this);
  }
#endif

#if defined(OS_MACOSX)
  ImageRepCocoa* AsImageRepCocoa() {
    CHECK_EQ(type_, Image::kImageRepCocoa);
    return reinterpret_cast<ImageRepCocoa*>(this);
  }
#endif

  Image::RepresentationType type() const { return type_; }

 private:
  Image::RepresentationType type_;
};

class ImageRepSkia : public ImageRep {
 public:
  explicit ImageRepSkia(const SkBitmap* bitmap)
      : ImageRep(Image::kImageRepSkia) {
    CHECK(bitmap);
    bitmaps_.push_back(bitmap);
  }

  explicit ImageRepSkia(const std::vector<const SkBitmap*>& bitmaps)
      : ImageRep(Image::kImageRepSkia),
        bitmaps_(bitmaps) {
    CHECK(!bitmaps_.empty());
  }

  virtual ~ImageRepSkia() {
    STLDeleteElements(&bitmaps_);
  }

  const SkBitmap* bitmap() const { return bitmaps_[0]; }

  const std::vector<const SkBitmap*>& bitmaps() const { return bitmaps_; }

 private:
  std::vector<const SkBitmap*> bitmaps_;

  DISALLOW_COPY_AND_ASSIGN(ImageRepSkia);
};

#if defined(TOOLKIT_USES_GTK)
class ImageRepGdk : public ImageRep {
 public:
  explicit ImageRepGdk(GdkPixbuf* pixbuf)
      : ImageRep(Image::kImageRepGdk),
        pixbuf_(pixbuf) {
    CHECK(pixbuf);
  }

  virtual ~ImageRepGdk() {
    if (pixbuf_) {
      g_object_unref(pixbuf_);
      pixbuf_ = NULL;
    }
  }

  GdkPixbuf* pixbuf() const { return pixbuf_; }

 private:
  GdkPixbuf* pixbuf_;

  DISALLOW_COPY_AND_ASSIGN(ImageRepGdk);
};

// Represents data that lives on the display server instead of in the client.
class ImageRepCairoCached : public ImageRep {
 public:
  explicit ImageRepCairoCached(GdkPixbuf* pixbuf)
      : ImageRep(Image::kImageRepCairoCache),
        cairo_cache_(new CairoCachedSurface) {
    CHECK(pixbuf);
    cairo_cache_->UsePixbuf(pixbuf);
  }

  virtual ~ImageRepCairoCached() {
    delete cairo_cache_;
  }

  CairoCachedSurface* surface() const { return cairo_cache_; }

 private:
  CairoCachedSurface* cairo_cache_;

  DISALLOW_COPY_AND_ASSIGN(ImageRepCairoCached);
};
#endif  // defined(TOOLKIT_USES_GTK)

#if defined(OS_MACOSX)
class ImageRepCocoa : public ImageRep {
 public:
  explicit ImageRepCocoa(NSImage* image)
      : ImageRep(Image::kImageRepCocoa),
        image_(image) {
    CHECK(image);
  }

  virtual ~ImageRepCocoa() {
    base::mac::NSObjectRelease(image_);
    image_ = nil;
  }

  NSImage* image() const { return image_; }

 private:
  NSImage* image_;

  DISALLOW_COPY_AND_ASSIGN(ImageRepCocoa);
};
#endif  // defined(OS_MACOSX)

// The Storage class acts similarly to the pixels in a SkBitmap: the Image
// class holds a refptr instance of Storage, which in turn holds all the
// ImageReps. This way, the Image can be cheaply copied.
class ImageStorage : public base::RefCounted<ImageStorage> {
 public:
  ImageStorage(gfx::Image::RepresentationType default_type)
      : default_representation_type_(default_type) {
  }

  gfx::Image::RepresentationType default_representation_type() {
    return default_representation_type_;
  }
  gfx::Image::RepresentationMap& representations() { return representations_; }

 private:
  ~ImageStorage() {
    for (gfx::Image::RepresentationMap::iterator it = representations_.begin();
         it != representations_.end();
         ++it) {
      delete it->second;
    }
    representations_.clear();
  }

  // The type of image that was passed to the constructor. This key will always
  // exist in the |representations_| map.
  gfx::Image::RepresentationType default_representation_type_;

  // All the representations of an Image. Size will always be at least one, with
  // more for any converted representations.
  gfx::Image::RepresentationMap representations_;

  friend class base::RefCounted<ImageStorage>;
};

}  // namespace internal

Image::Image(const SkBitmap* bitmap)
    : storage_(new internal::ImageStorage(Image::kImageRepSkia)) {
  internal::ImageRepSkia* rep = new internal::ImageRepSkia(bitmap);
  AddRepresentation(rep);
}

Image::Image(const std::vector<const SkBitmap*>& bitmaps)
    : storage_(new internal::ImageStorage(Image::kImageRepSkia)) {
  internal::ImageRepSkia* rep = new internal::ImageRepSkia(bitmaps);
  AddRepresentation(rep);
}

#if defined(TOOLKIT_USES_GTK)
Image::Image(GdkPixbuf* pixbuf)
    : storage_(new internal::ImageStorage(Image::kImageRepGdk)) {
  internal::ImageRepGdk* rep = new internal::ImageRepGdk(pixbuf);
  AddRepresentation(rep);
}
#endif

#if defined(OS_MACOSX)
Image::Image(NSImage* image)
    : storage_(new internal::ImageStorage(Image::kImageRepCocoa)) {
  internal::ImageRepCocoa* rep = new internal::ImageRepCocoa(image);
  AddRepresentation(rep);
}
#endif

Image::Image(const Image& other) : storage_(other.storage_) {
}

Image& Image::operator=(const Image& other) {
  storage_ = other.storage_;
  return *this;
}

Image::~Image() {
}

const SkBitmap* Image::ToSkBitmap() const {
  internal::ImageRep* rep = GetRepresentation(Image::kImageRepSkia);
  return rep->AsImageRepSkia()->bitmap();
}

#if defined(TOOLKIT_USES_GTK)
GdkPixbuf* Image::ToGdkPixbuf() const {
  internal::ImageRep* rep = GetRepresentation(Image::kImageRepGdk);
  return rep->AsImageRepGdk()->pixbuf();
}

CairoCachedSurface* const Image::ToCairo() const {
  internal::ImageRep* rep = GetRepresentation(Image::kImageRepCairoCache);
  return rep->AsImageRepCairo()->surface();
}
#endif

#if defined(OS_MACOSX)
NSImage* Image::ToNSImage() const {
  internal::ImageRep* rep = GetRepresentation(Image::kImageRepCocoa);
  return rep->AsImageRepCocoa()->image();
}
#endif

const SkBitmap* Image::CopySkBitmap() const {
  return new SkBitmap(*ToSkBitmap());
}

#if defined(TOOLKIT_USES_GTK)
GdkPixbuf* Image::CopyGdkPixbuf() const {
  GdkPixbuf* pixbuf = ToGdkPixbuf();
  g_object_ref(pixbuf);
  return pixbuf;
}
#endif

#if defined(OS_MACOSX)
NSImage* Image::CopyNSImage() const {
  NSImage* image = ToNSImage();
  base::mac::NSObjectRetain(image);
  return image;
}
#endif

Image::operator const SkBitmap*() const {
  return ToSkBitmap();
}

Image::operator const SkBitmap&() const {
  return *ToSkBitmap();
}

#if defined(TOOLKIT_USES_GTK)
Image::operator GdkPixbuf*() const {
  return ToGdkPixbuf();
}
#endif

#if defined(OS_MACOSX)
Image::operator NSImage*() const {
  return ToNSImage();
}
#endif

bool Image::HasRepresentation(RepresentationType type) const {
  return storage_->representations().count(type) != 0;
}

size_t Image::RepresentationCount() const {
  return storage_->representations().size();
}

void Image::SwapRepresentations(gfx::Image* other) {
  storage_.swap(other->storage_);
}

internal::ImageRep* Image::DefaultRepresentation() const {
  RepresentationMap& representations = storage_->representations();
  RepresentationMap::iterator it =
      representations.find(storage_->default_representation_type());
  DCHECK(it != representations.end());
  return it->second;
}

internal::ImageRep* Image::GetRepresentation(
    RepresentationType rep_type) const {
  // If the requested rep is the default, return it.
  internal::ImageRep* default_rep = DefaultRepresentation();
  if (rep_type == storage_->default_representation_type())
    return default_rep;

  // Check to see if the representation already exists.
  RepresentationMap::iterator it = storage_->representations().find(rep_type);
  if (it != storage_->representations().end())
    return it->second;

  // At this point, the requested rep does not exist, so it must be converted
  // from the default rep.

  // Handle native-to-Skia conversion.
  if (rep_type == Image::kImageRepSkia) {
    internal::ImageRepSkia* rep = NULL;
#if defined(TOOLKIT_USES_GTK)
    if (storage_->default_representation_type() == Image::kImageRepGdk) {
      internal::ImageRepGdk* pixbuf_rep = default_rep->AsImageRepGdk();
      rep = new internal::ImageRepSkia(
          internal::GdkPixbufToSkBitmap(pixbuf_rep->pixbuf()));
    }
    // We don't do conversions from CairoCachedSurfaces to Skia because the
    // data lives on the display server and we'll always have a GdkPixbuf if we
    // have a CairoCachedSurface.
#elif defined(OS_MACOSX)
    if (storage_->default_representation_type() == Image::kImageRepCocoa) {
      internal::ImageRepCocoa* nsimage_rep = default_rep->AsImageRepCocoa();
      std::vector<const SkBitmap*> bitmaps;
      CHECK(internal::NSImageToSkBitmaps(nsimage_rep->image(), &bitmaps));
      rep = new internal::ImageRepSkia(bitmaps);
    }
#endif
    CHECK(rep);
    AddRepresentation(rep);
    return rep;
  }
#if defined(TOOLKIT_USES_GTK)
  else if (rep_type == Image::kImageRepCairoCache) {
    // Handle any-to-Cairo conversion. This may recursively create an
    // intermediate pixbuf before we send the data to the display server.
    internal::ImageRep* rep = GetRepresentation(Image::kImageRepGdk);
    internal::ImageRepCairoCached* native_rep =
        new internal::ImageRepCairoCached(rep->AsImageRepGdk()->pixbuf());

    CHECK(native_rep);
    AddRepresentation(native_rep);
    return native_rep;
  }
#endif

  // Handle Skia-to-native conversions.
  if (default_rep->type() == Image::kImageRepSkia) {
    internal::ImageRep* native_rep = NULL;
#if defined(USE_AURA)
    NOTIMPLEMENTED();
#elif defined(TOOLKIT_USES_GTK)
    if (rep_type == Image::kImageRepGdk) {
      GdkPixbuf* pixbuf = gfx::GdkPixbufFromSkBitmap(
          default_rep->AsImageRepSkia()->bitmap());
      native_rep = new internal::ImageRepGdk(pixbuf);
    }
#elif defined(OS_MACOSX)
    if (rep_type == Image::kImageRepCocoa) {
      NSImage* image = gfx::SkBitmapsToNSImage(
          default_rep->AsImageRepSkia()->bitmaps());
      base::mac::NSObjectRetain(image);
      native_rep = new internal::ImageRepCocoa(image);
    }
#endif
    CHECK(native_rep);
    AddRepresentation(native_rep);
    return native_rep;
  }

  // Something went seriously wrong...
  return NULL;
}

void Image::AddRepresentation(internal::ImageRep* rep) const {
  storage_->representations().insert(std::make_pair(rep->type(), rep));
}

size_t Image::GetNumberOfSkBitmaps() const  {
  return GetRepresentation(Image::kImageRepSkia)->AsImageRepSkia()->
      bitmaps().size();
}

const SkBitmap* Image::GetSkBitmapAtIndex(size_t index) const {
  return GetRepresentation(Image::kImageRepSkia)->AsImageRepSkia()->
      bitmaps()[index];
}

}  // namespace gfx
