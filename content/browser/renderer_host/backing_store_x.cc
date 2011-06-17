// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/backing_store_x.h"

#include <cairo-xlib.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#if defined(OS_OPENBSD) || defined(OS_FREEBSD)
#include <sys/endian.h>
#endif

#include <algorithm>
#include <utility>
#include <limits>

#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "base/time.h"
#include "content/browser/renderer_host/render_process_host.h"
#include "skia/ext/platform_canvas.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/x/x11_util.h"
#include "ui/base/x/x11_util_internal.h"
#include "ui/gfx/rect.h"
#include "ui/gfx/surface/transport_dib.h"

// Assume that somewhere along the line, someone will do width * height * 4
// with signed numbers. If the maximum value is 2**31, then 2**31 / 4 =
// 2**29 and floor(sqrt(2**29)) = 23170.

// Max height and width for layers
static const int kMaxVideoLayerSize = 23170;


// X Backing Stores:
//
// Unlike Windows, where the backing store is kept in heap memory, we keep our
// backing store in the X server, as a pixmap. Thus expose events just require
// instructing the X server to copy from the backing store to the window.
//
// The backing store is in the same format as the visual which our main window
// is using. Bitmaps from the renderer are uploaded to the X server, either via
// shared memory or over the wire, and XRENDER is used to convert them to the
// correct format for the backing store.

// Destroys the image and the associated shared memory structures. This is a
// helper function for code using shared memory.
static void DestroySharedImage(Display* display,
                               XImage* image,
                               XShmSegmentInfo* shminfo) {
  XShmDetach(display, shminfo);
  XDestroyImage(image);
  shmdt(shminfo->shmaddr);
}

BackingStoreX::BackingStoreX(RenderWidgetHost* widget,
                            const gfx::Size& size,
                            void* visual,
                            int depth)
    : BackingStore(widget, size),
      display_(ui::GetXDisplay()),
      shared_memory_support_(ui::QuerySharedMemorySupport(display_)),
      use_render_(ui::QueryRenderSupport(display_)),
      visual_(visual),
      visual_depth_(depth),
      root_window_(ui::GetX11RootWindow()) {
#if defined(OS_OPENBSD) || defined(OS_FREEBSD)
  COMPILE_ASSERT(_BYTE_ORDER == _LITTLE_ENDIAN, assumes_little_endian);
#else
  COMPILE_ASSERT(__BYTE_ORDER == __LITTLE_ENDIAN, assumes_little_endian);
#endif

  pixmap_ = XCreatePixmap(display_, root_window_,
                          size.width(), size.height(), depth);

  if (use_render_) {
    picture_ = XRenderCreatePicture(
        display_, pixmap_,
        ui::GetRenderVisualFormat(display_,
                                  static_cast<Visual*>(visual)),
                                  0, NULL);
    pixmap_bpp_ = 0;
  } else {
    picture_ = 0;
    pixmap_bpp_ = ui::BitsPerPixelForPixmapDepth(display_, depth);
  }

  pixmap_gc_ = XCreateGC(display_, pixmap_, 0, NULL);
}

BackingStoreX::BackingStoreX(RenderWidgetHost* widget, const gfx::Size& size)
    : BackingStore(widget, size),
      display_(NULL),
      shared_memory_support_(ui::SHARED_MEMORY_NONE),
      use_render_(false),
      pixmap_bpp_(0),
      visual_(NULL),
      visual_depth_(-1),
      root_window_(0),
      pixmap_(0),
      picture_(0),
      pixmap_gc_(NULL) {
}

BackingStoreX::~BackingStoreX() {
  // In unit tests, display_ may be NULL.
  if (!display_)
    return;

  XRenderFreePicture(display_, picture_);
  XFreePixmap(display_, pixmap_);
  XFreeGC(display_, static_cast<GC>(pixmap_gc_));
}

size_t BackingStoreX::MemorySize() {
  if (!use_render_)
    return size().GetArea() * (pixmap_bpp_ / 8);
  else
    return size().GetArea() * 4;
}

void BackingStoreX::PaintRectWithoutXrender(
    TransportDIB* bitmap,
    const gfx::Rect& bitmap_rect,
    const std::vector<gfx::Rect>& copy_rects) {
  const int width = bitmap_rect.width();
  const int height = bitmap_rect.height();
  Pixmap pixmap = XCreatePixmap(display_, root_window_, width, height,
                                visual_depth_);

  // Draw ARGB transport DIB onto our pixmap.
  ui::PutARGBImage(display_, visual_, visual_depth_, pixmap,
                   pixmap_gc_, static_cast<uint8*>(bitmap->memory()),
                   width, height);

  for (size_t i = 0; i < copy_rects.size(); i++) {
    const gfx::Rect& copy_rect = copy_rects[i];
    XCopyArea(display_,
              pixmap,                           // src
              pixmap_,                          // dest
              static_cast<GC>(pixmap_gc_),      // gc
              copy_rect.x() - bitmap_rect.x(),  // src_x
              copy_rect.y() - bitmap_rect.y(),  // src_y
              copy_rect.width(),                // width
              copy_rect.height(),               // height
              copy_rect.x(),                    // dest_x
              copy_rect.y());                   // dest_y
  }

  XFreePixmap(display_, pixmap);
}

void BackingStoreX::PaintToBackingStore(
    RenderProcessHost* process,
    TransportDIB::Id bitmap,
    const gfx::Rect& bitmap_rect,
    const std::vector<gfx::Rect>& copy_rects) {
  if (!display_)
    return;

  if (bitmap_rect.IsEmpty())
    return;

  const int width = bitmap_rect.width();
  const int height = bitmap_rect.height();

  if (width <= 0 || width > kMaxVideoLayerSize ||
      height <= 0 || height > kMaxVideoLayerSize)
    return;

  TransportDIB* dib = process->GetTransportDIB(bitmap);
  if (!dib)
    return;

  if (!use_render_)
    return PaintRectWithoutXrender(dib, bitmap_rect, copy_rects);

  Picture picture;
  Pixmap pixmap;

  if (shared_memory_support_ == ui::SHARED_MEMORY_PIXMAP) {
    XShmSegmentInfo shminfo = {0};
    shminfo.shmseg = dib->MapToX(display_);

    // The NULL in the following is the |data| pointer: this is an artifact of
    // Xlib trying to be helpful, rather than just exposing the X protocol. It
    // assumes that we have the shared memory segment mapped into our memory,
    // which we don't, and it's trying to calculate an offset by taking the
    // difference between the |data| pointer and the address of the mapping in
    // |shminfo|. Since both are NULL, the offset will be calculated to be 0,
    // which is correct for us.
    pixmap = XShmCreatePixmap(display_, root_window_, NULL, &shminfo,
                              width, height, 32);
  } else {
    // We don't have shared memory pixmaps.  Fall back to creating a pixmap
    // ourselves and putting an image on it.
    pixmap = XCreatePixmap(display_, root_window_, width, height, 32);
    GC gc = XCreateGC(display_, pixmap, 0, NULL);

    if (shared_memory_support_ == ui::SHARED_MEMORY_PUTIMAGE) {
      const XID shmseg = dib->MapToX(display_);

      XShmSegmentInfo shminfo;
      memset(&shminfo, 0, sizeof(shminfo));
      shminfo.shmseg = shmseg;
      shminfo.shmaddr = static_cast<char*>(dib->memory());

      XImage* image = XShmCreateImage(display_, static_cast<Visual*>(visual_),
                                      32, ZPixmap,
                                      shminfo.shmaddr, &shminfo,
                                      width, height);

      // This code path is important for performance and we have found that
      // different techniques work better on different platforms. See
      // http://code.google.com/p/chromium/issues/detail?id=44124.
      //
      // Checking for ARM is an approximation, but it seems to be a good one so
      // far.
#if defined(ARCH_CPU_ARM_FAMILY)
      for (size_t i = 0; i < copy_rects.size(); i++) {
        const gfx::Rect& copy_rect = copy_rects[i];
        XShmPutImage(display_, pixmap, gc, image,
                     copy_rect.x() - bitmap_rect.x(), /* source x */
                     copy_rect.y() - bitmap_rect.y(), /* source y */
                     copy_rect.x() - bitmap_rect.x(), /* dest x */
                     copy_rect.y() - bitmap_rect.y(), /* dest y */
                     copy_rect.width(), copy_rect.height(),
                     False /* send_event */);
      }
#else
      XShmPutImage(display_, pixmap, gc, image,
                   0, 0 /* source x, y */, 0, 0 /* dest x, y */,
                   width, height, False /* send_event */);
#endif
      XDestroyImage(image);
    } else {  // case SHARED_MEMORY_NONE
      // No shared memory support, we have to copy the bitmap contents
      // to the X server. Xlib wraps the underlying PutImage call
      // behind several layers of functions which try to convert the
      // image into the format which the X server expects. The
      // following values hopefully disable all conversions.
      XImage image;
      memset(&image, 0, sizeof(image));

      image.width = width;
      image.height = height;
      image.depth = 32;
      image.bits_per_pixel = 32;
      image.format = ZPixmap;
      image.byte_order = LSBFirst;
      image.bitmap_unit = 8;
      image.bitmap_bit_order = LSBFirst;
      image.bytes_per_line = width * 4;
      image.red_mask = 0xff;
      image.green_mask = 0xff00;
      image.blue_mask = 0xff0000;
      image.data = static_cast<char*>(dib->memory());

      XPutImage(display_, pixmap, gc, &image,
                0, 0 /* source x, y */, 0, 0 /* dest x, y */,
                width, height);
    }
    XFreeGC(display_, gc);
  }

  picture = ui::CreatePictureFromSkiaPixmap(display_, pixmap);

  for (size_t i = 0; i < copy_rects.size(); i++) {
    const gfx::Rect& copy_rect = copy_rects[i];
    XRenderComposite(display_,
                     PictOpSrc,                        // op
                     picture,                          // src
                     0,                                // mask
                     picture_,                         // dest
                     copy_rect.x() - bitmap_rect.x(),  // src_x
                     copy_rect.y() - bitmap_rect.y(),  // src_y
                     0,                                // mask_x
                     0,                                // mask_y
                     copy_rect.x(),                    // dest_x
                     copy_rect.y(),                    // dest_y
                     copy_rect.width(),                // width
                     copy_rect.height());              // height
  }

  // In the case of shared memory, we wait for the composite to complete so that
  // we are sure that the X server has finished reading from the shared memory
  // segment.
  if (shared_memory_support_ != ui::SHARED_MEMORY_NONE)
    XSync(display_, False);

  XRenderFreePicture(display_, picture);
  XFreePixmap(display_, pixmap);
}

bool BackingStoreX::CopyFromBackingStore(const gfx::Rect& rect,
                                         skia::PlatformCanvas* output) {
  base::TimeTicks begin_time = base::TimeTicks::Now();

  if (visual_depth_ < 24) {
    // CopyFromBackingStore() copies pixels out of the XImage
    // in a way that assumes that each component (red, green,
    // blue) is a byte.  This doesn't work on visuals which
    // encode a pixel color with less than a byte per color.
    return false;
  }

  const int width = std::min(size().width(), rect.width());
  const int height = std::min(size().height(), rect.height());

  XImage* image;
  XShmSegmentInfo shminfo;  // Used only when shared memory is enabled.
  if (shared_memory_support_ != ui::SHARED_MEMORY_NONE) {
    // Use shared memory for faster copies when it's available.
    Visual* visual = static_cast<Visual*>(visual_);
    memset(&shminfo, 0, sizeof(shminfo));
    image = XShmCreateImage(display_, visual, 32,
                            ZPixmap, NULL, &shminfo, width, height);
    if (!image) {
      return false;
    }
    // Create the shared memory segment for the image and map it.
    if (image->bytes_per_line == 0 || image->height == 0 ||
        static_cast<size_t>(image->height) >
        (std::numeric_limits<size_t>::max() / image->bytes_per_line)) {
      XDestroyImage(image);
      return false;
    }
    shminfo.shmid = shmget(IPC_PRIVATE, image->bytes_per_line * image->height,
                           IPC_CREAT|0666);
    if (shminfo.shmid == -1) {
      XDestroyImage(image);
      return false;
    }

    void* mapped_memory = shmat(shminfo.shmid, NULL, SHM_RDONLY);
    shmctl(shminfo.shmid, IPC_RMID, 0);
    if (mapped_memory == (void*)-1) {
      XDestroyImage(image);
      return false;
    }
    shminfo.shmaddr = image->data = static_cast<char*>(mapped_memory);

    if (!XShmAttach(display_, &shminfo) ||
        !XShmGetImage(display_, pixmap_, image, rect.x(), rect.y(),
                      AllPlanes)) {
      DestroySharedImage(display_, image, &shminfo);
      return false;
    }
  } else {
    // Non-shared memory case just copy the image from the server.
    image = XGetImage(display_, pixmap_,
                      rect.x(), rect.y(), width, height,
                      AllPlanes, ZPixmap);
  }

  // TODO(jhawkins): Need to convert the image data if the image bits per pixel
  // is not 32.
  // Note that this also initializes the output bitmap as opaque.
  if (!output->initialize(width, height, true) ||
      image->bits_per_pixel != 32) {
    if (shared_memory_support_ != ui::SHARED_MEMORY_NONE)
      DestroySharedImage(display_, image, &shminfo);
    else
      XDestroyImage(image);
    return false;
  }

  // The X image might have a different row stride, so iterate through
  // it and copy each row out, only up to the pixels we're actually
  // using.  This code assumes a visual mode where a pixel is
  // represented using a 32-bit unsigned int, with a byte per component.
  SkBitmap bitmap = output->getTopPlatformDevice().accessBitmap(true);
  for (int y = 0; y < height; y++) {
    const uint32* src_row = reinterpret_cast<uint32*>(
        &image->data[image->bytes_per_line * y]);
    uint32* dest_row = bitmap.getAddr32(0, y);
    for (int x = 0; x < width; ++x, ++dest_row) {
      // Force alpha to be 0xff, because otherwise it causes rendering problems.
      *dest_row = src_row[x] | 0xff000000;
    }
  }

  if (shared_memory_support_ != ui::SHARED_MEMORY_NONE)
    DestroySharedImage(display_, image, &shminfo);
  else
    XDestroyImage(image);

  HISTOGRAM_TIMES("BackingStore.RetrievalFromX",
                  base::TimeTicks::Now() - begin_time);
  return true;
}

void BackingStoreX::ScrollBackingStore(int dx, int dy,
                                       const gfx::Rect& clip_rect,
                                       const gfx::Size& view_size) {
  if (!display_)
    return;

  // We only support scrolling in one direction at a time.
  DCHECK(dx == 0 || dy == 0);

  if (dy) {
    // Positive values of |dy| scroll up
    if (abs(dy) < clip_rect.height()) {
      XCopyArea(display_, pixmap_, pixmap_, static_cast<GC>(pixmap_gc_),
                clip_rect.x() /* source x */,
                std::max(clip_rect.y(), clip_rect.y() - dy),
                clip_rect.width(),
                clip_rect.height() - abs(dy),
                clip_rect.x() /* dest x */,
                std::max(clip_rect.y(), clip_rect.y() + dy) /* dest y */);
    }
  } else if (dx) {
    // Positive values of |dx| scroll right
    if (abs(dx) < clip_rect.width()) {
      XCopyArea(display_, pixmap_, pixmap_, static_cast<GC>(pixmap_gc_),
                std::max(clip_rect.x(), clip_rect.x() - dx),
                clip_rect.y() /* source y */,
                clip_rect.width() - abs(dx),
                clip_rect.height(),
                std::max(clip_rect.x(), clip_rect.x() + dx) /* dest x */,
                clip_rect.y() /* dest x */);
    }
  }
}

void BackingStoreX::XShowRect(const gfx::Point &origin,
                              const gfx::Rect& rect, XID target) {
  XCopyArea(display_, pixmap_, target, static_cast<GC>(pixmap_gc_),
            rect.x(), rect.y(), rect.width(), rect.height(),
            rect.x() + origin.x(), rect.y() + origin.y());
}

void BackingStoreX::CairoShowRect(const gfx::Rect& rect,
                                  GdkDrawable* drawable) {
  cairo_surface_t* surface = cairo_xlib_surface_create(
      display_, pixmap_, static_cast<Visual*>(visual_),
      size().width(), size().height());
  cairo_t* cr = gdk_cairo_create(drawable);
  cairo_set_source_surface(cr, surface, 0, 0);

  cairo_rectangle(cr, rect.x(), rect.y(), rect.width(), rect.height());
  cairo_fill(cr);
  cairo_destroy(cr);
  cairo_surface_destroy(surface);
}

#if defined(TOOLKIT_GTK)
void BackingStoreX::PaintToRect(const gfx::Rect& rect, GdkDrawable* target) {
  cairo_surface_t* surface = cairo_xlib_surface_create(
      display_, pixmap_, static_cast<Visual*>(visual_),
      size().width(), size().height());
  cairo_t* cr = gdk_cairo_create(target);

  cairo_translate(cr, rect.x(), rect.y());
  double x_scale = static_cast<double>(rect.width()) / size().width();
  double y_scale = static_cast<double>(rect.height()) / size().height();
  cairo_scale(cr, x_scale, y_scale);

  cairo_pattern_t* pattern = cairo_pattern_create_for_surface(surface);
  cairo_pattern_set_filter(pattern, CAIRO_FILTER_BEST);
  cairo_set_source(cr, pattern);
  cairo_pattern_destroy(pattern);

  cairo_identity_matrix(cr);

  cairo_rectangle(cr, rect.x(), rect.y(), rect.width(), rect.height());
  cairo_fill(cr);
  cairo_destroy(cr);
}
#endif
