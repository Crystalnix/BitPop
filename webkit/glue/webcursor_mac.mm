// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/glue/webcursor.h"

#import <AppKit/AppKit.h>
#include <Carbon/Carbon.h>

#include "app/mac/nsimage_cache.h"
#include "base/logging.h"
#include "base/mac/scoped_cftyperef.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebCursorInfo.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebImage.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebSize.h"

using WebKit::WebCursorInfo;
using WebKit::WebImage;
using WebKit::WebSize;

namespace {

// TODO: This image fetch can (and probably should) be serviced by the resource
// resource bundle instead of going through the image cache.
NSCursor* LoadCursor(const char* name, int x, int y) {
  NSString* file_name = [NSString stringWithUTF8String:name];
  DCHECK(file_name);
  NSImage* cursor_image = app::mac::GetCachedImageWithName(file_name);
  DCHECK(cursor_image);
  return [[[NSCursor alloc] initWithImage:cursor_image
                                  hotSpot:NSMakePoint(x, y)] autorelease];
}

CGImageRef CreateCGImageFromCustomData(const std::vector<char>& custom_data,
                                       const gfx::Size& custom_size) {
  base::mac::ScopedCFTypeRef<CGColorSpaceRef> cg_color(
      CGColorSpaceCreateDeviceRGB());
  // This is safe since we're not going to draw into the context we're creating.
  void* data = const_cast<char*>(&custom_data[0]);
  // The settings here match SetCustomData() below; keep in sync.
  base::mac::ScopedCFTypeRef<CGContextRef> context(
      CGBitmapContextCreate(data,
                            custom_size.width(),
                            custom_size.height(),
                            8,
                            custom_size.width()*4,
                            cg_color.get(),
                            kCGImageAlphaPremultipliedLast |
                            kCGBitmapByteOrder32Big));
  return CGBitmapContextCreateImage(context.get());
}

NSCursor* CreateCustomCursor(const std::vector<char>& custom_data,
                             const gfx::Size& custom_size,
                             const gfx::Point& hotspot) {
  // CG throws a cocoa exception if we try to create an empty image, which
  // results in an infinite loop.  This CHECK ensures that we crash instead.
  CHECK(!custom_data.empty());

  base::mac::ScopedCFTypeRef<CGImageRef> cg_image(
      CreateCGImageFromCustomData(custom_data, custom_size));

  NSBitmapImageRep* ns_bitmap =
      [[NSBitmapImageRep alloc] initWithCGImage:cg_image.get()];
  NSImage* cursor_image = [[NSImage alloc] init];
  DCHECK(cursor_image);
  [cursor_image addRepresentation:ns_bitmap];
  [ns_bitmap release];

  NSCursor* cursor = [[NSCursor alloc] initWithImage:cursor_image
                                             hotSpot:NSMakePoint(hotspot.x(),
                                                                 hotspot.y())];
  [cursor_image release];

  return [cursor autorelease];
}

}  // namespace

// We're matching Safari's cursor choices; see platform/mac/CursorMac.mm
NSCursor* WebCursor::GetCursor() const {
  switch (type_) {
    case WebCursorInfo::TypePointer:
      return [NSCursor arrowCursor];
    case WebCursorInfo::TypeCross:
      return LoadCursor("crossHairCursor", 11, 11);
    case WebCursorInfo::TypeHand:
      return LoadCursor("linkCursor", 6, 1);
    case WebCursorInfo::TypeIBeam:
      return [NSCursor IBeamCursor];
    case WebCursorInfo::TypeWait:
      return LoadCursor("waitCursor", 7, 7);
    case WebCursorInfo::TypeHelp:
      return LoadCursor("helpCursor", 8, 8);
    case WebCursorInfo::TypeEastResize:
    case WebCursorInfo::TypeEastPanning:
      return LoadCursor("eastResizeCursor", 14, 7);
    case WebCursorInfo::TypeNorthResize:
    case WebCursorInfo::TypeNorthPanning:
      return LoadCursor("northResizeCursor", 7, 1);
    case WebCursorInfo::TypeNorthEastResize:
    case WebCursorInfo::TypeNorthEastPanning:
      return LoadCursor("northEastResizeCursor", 14, 1);
    case WebCursorInfo::TypeNorthWestResize:
    case WebCursorInfo::TypeNorthWestPanning:
      return LoadCursor("northWestResizeCursor", 0, 0);
    case WebCursorInfo::TypeSouthResize:
    case WebCursorInfo::TypeSouthPanning:
      return LoadCursor("southResizeCursor", 7, 14);
    case WebCursorInfo::TypeSouthEastResize:
    case WebCursorInfo::TypeSouthEastPanning:
      return LoadCursor("southEastResizeCursor", 14, 14);
    case WebCursorInfo::TypeSouthWestResize:
    case WebCursorInfo::TypeSouthWestPanning:
      return LoadCursor("southWestResizeCursor", 1, 14);
    case WebCursorInfo::TypeWestResize:
    case WebCursorInfo::TypeWestPanning:
      return LoadCursor("westResizeCursor", 1, 7);
    case WebCursorInfo::TypeNorthSouthResize:
      return LoadCursor("northSouthResizeCursor", 7, 7);
    case WebCursorInfo::TypeEastWestResize:
      return LoadCursor("eastWestResizeCursor", 7, 7);
    case WebCursorInfo::TypeNorthEastSouthWestResize:
      return LoadCursor("northEastSouthWestResizeCursor", 7, 7);
    case WebCursorInfo::TypeNorthWestSouthEastResize:
      return LoadCursor("northWestSouthEastResizeCursor", 7, 7);
    case WebCursorInfo::TypeColumnResize:
      return [NSCursor resizeLeftRightCursor];
    case WebCursorInfo::TypeRowResize:
      return [NSCursor resizeUpDownCursor];
    case WebCursorInfo::TypeMiddlePanning:
    case WebCursorInfo::TypeMove:
      return LoadCursor("moveCursor", 7, 7);
    case WebCursorInfo::TypeVerticalText:
      return LoadCursor("verticalTextCursor", 7, 7);
    case WebCursorInfo::TypeCell:
      return LoadCursor("cellCursor", 7, 7);
    case WebCursorInfo::TypeContextMenu:
      return LoadCursor("contextMenuCursor", 3, 2);
    case WebCursorInfo::TypeAlias:
      return LoadCursor("aliasCursor", 11, 3);
    case WebCursorInfo::TypeProgress:
      return LoadCursor("progressCursor", 3, 2);
    case WebCursorInfo::TypeNoDrop:
      return LoadCursor("noDropCursor", 3, 1);
    case WebCursorInfo::TypeCopy:
      return LoadCursor("copyCursor", 3, 2);
    case WebCursorInfo::TypeNone:
      return LoadCursor("noneCursor", 7, 7);
    case WebCursorInfo::TypeNotAllowed:
      return LoadCursor("notAllowedCursor", 11, 11);
    case WebCursorInfo::TypeZoomIn:
      return LoadCursor("zoomInCursor", 7, 7);
    case WebCursorInfo::TypeZoomOut:
      return LoadCursor("zoomOutCursor", 7, 7);
    case WebCursorInfo::TypeGrab:
      return [NSCursor openHandCursor];
    case WebCursorInfo::TypeGrabbing:
      return [NSCursor closedHandCursor];
    case WebCursorInfo::TypeCustom:
      return CreateCustomCursor(custom_data_, custom_size_, hotspot_);
  }
  NOTREACHED();
  return nil;
}

gfx::NativeCursor WebCursor::GetNativeCursor() {
  return GetCursor();
}

void WebCursor::InitFromThemeCursor(ThemeCursor cursor) {
  WebKit::WebCursorInfo cursor_info;

  switch (cursor) {
    case kThemeArrowCursor:
      cursor_info.type = WebCursorInfo::TypePointer;
      break;
    case kThemeCopyArrowCursor:
      cursor_info.type = WebCursorInfo::TypeCopy;
      break;
    case kThemeAliasArrowCursor:
      cursor_info.type = WebCursorInfo::TypeAlias;
      break;
    case kThemeContextualMenuArrowCursor:
      cursor_info.type = WebCursorInfo::TypeContextMenu;
      break;
    case kThemeIBeamCursor:
      cursor_info.type = WebCursorInfo::TypeIBeam;
      break;
    case kThemeCrossCursor:
    case kThemePlusCursor:
      cursor_info.type = WebCursorInfo::TypeCross;
      break;
    case kThemeWatchCursor:
    case kThemeSpinningCursor:
      cursor_info.type = WebCursorInfo::TypeWait;
      break;
    case kThemeClosedHandCursor:
      cursor_info.type = WebCursorInfo::TypeGrabbing;
      break;
    case kThemeOpenHandCursor:
      cursor_info.type = WebCursorInfo::TypeGrab;
      break;
    case kThemePointingHandCursor:
    case kThemeCountingUpHandCursor:
    case kThemeCountingDownHandCursor:
    case kThemeCountingUpAndDownHandCursor:
      cursor_info.type = WebCursorInfo::TypeHand;
      break;
    case kThemeResizeLeftCursor:
      cursor_info.type = WebCursorInfo::TypeWestResize;
      break;
    case kThemeResizeRightCursor:
      cursor_info.type = WebCursorInfo::TypeEastResize;
      break;
    case kThemeResizeLeftRightCursor:
      cursor_info.type = WebCursorInfo::TypeEastWestResize;
      break;
    case kThemeNotAllowedCursor:
      cursor_info.type = WebCursorInfo::TypeNotAllowed;
      break;
    case kThemeResizeUpCursor:
      cursor_info.type = WebCursorInfo::TypeNorthResize;
      break;
    case kThemeResizeDownCursor:
      cursor_info.type = WebCursorInfo::TypeSouthResize;
      break;
    case kThemeResizeUpDownCursor:
      cursor_info.type = WebCursorInfo::TypeNorthSouthResize;
      break;
    case kThemePoofCursor:  // *shrug*
    default:
      cursor_info.type = WebCursorInfo::TypePointer;
      break;
  }

  InitFromCursorInfo(cursor_info);
}

void WebCursor::InitFromCursor(const Cursor* cursor) {
  // This conversion isn't perfect (in particular, the inversion effect of
  // data==1, mask==0 won't work). Not planning on fixing it.

  gfx::Size custom_size(16, 16);
  std::vector<char> raw_data;
  for (int row = 0; row < 16; ++row) {
    unsigned short data = cursor->data[row];
    unsigned short mask = cursor->mask[row];

    // The Core Endian flipper callback for 'CURS' doesn't flip Bits16 as if it
    // were a short (which it is), so we flip it here.
    data = ((data << 8) & 0xFF00) | ((data >> 8) & 0x00FF);
    mask = ((mask << 8) & 0xFF00) | ((mask >> 8) & 0x00FF);

    for (int bit = 0; bit < 16; ++bit) {
      if (data & 0x8000) {
        raw_data.push_back(0x00);
        raw_data.push_back(0x00);
        raw_data.push_back(0x00);
      } else {
        raw_data.push_back(0xFF);
        raw_data.push_back(0xFF);
        raw_data.push_back(0xFF);
      }
      if (mask & 0x8000)
        raw_data.push_back(0xFF);
      else
        raw_data.push_back(0x00);
      data <<= 1;
      mask <<= 1;
    }
  }

  base::mac::ScopedCFTypeRef<CGImageRef> cg_image(
      CreateCGImageFromCustomData(raw_data, custom_size));

  WebKit::WebCursorInfo cursor_info;
  cursor_info.type = WebCursorInfo::TypeCustom;
  cursor_info.hotSpot = WebKit::WebPoint(cursor->hotSpot.h, cursor->hotSpot.v);
  cursor_info.customImage = cg_image.get();

  InitFromCursorInfo(cursor_info);
}

void WebCursor::InitFromNSCursor(NSCursor* cursor) {
  WebKit::WebCursorInfo cursor_info;

  if ([cursor isEqual:[NSCursor arrowCursor]]) {
    cursor_info.type = WebCursorInfo::TypePointer;
  } else if ([cursor isEqual:[NSCursor IBeamCursor]]) {
    cursor_info.type = WebCursorInfo::TypeIBeam;
  } else if ([cursor isEqual:[NSCursor crosshairCursor]]) {
    cursor_info.type = WebCursorInfo::TypeCross;
  } else if ([cursor isEqual:[NSCursor pointingHandCursor]]) {
    cursor_info.type = WebCursorInfo::TypeHand;
  } else if ([cursor isEqual:[NSCursor resizeLeftCursor]]) {
    cursor_info.type = WebCursorInfo::TypeWestResize;
  } else if ([cursor isEqual:[NSCursor resizeRightCursor]]) {
    cursor_info.type = WebCursorInfo::TypeEastResize;
  } else if ([cursor isEqual:[NSCursor resizeLeftRightCursor]]) {
    cursor_info.type = WebCursorInfo::TypeEastWestResize;
  } else if ([cursor isEqual:[NSCursor resizeUpCursor]]) {
    cursor_info.type = WebCursorInfo::TypeNorthResize;
  } else if ([cursor isEqual:[NSCursor resizeDownCursor]]) {
    cursor_info.type = WebCursorInfo::TypeSouthResize;
  } else if ([cursor isEqual:[NSCursor resizeUpDownCursor]]) {
    cursor_info.type = WebCursorInfo::TypeNorthSouthResize;
  } else if ([cursor isEqual:[NSCursor openHandCursor]]) {
    cursor_info.type = WebCursorInfo::TypeGrab;
  } else if ([cursor isEqual:[NSCursor closedHandCursor]]) {
    cursor_info.type = WebCursorInfo::TypeGrabbing;
  } else {
    // Also handles the [NSCursor disappearingItemCursor] case. Quick-and-dirty
    // image conversion; TODO(avi): do better.
    CGImageRef cg_image = nil;
    NSImage* image = [cursor image];
    for (id rep in [image representations]) {
      if ([rep isKindOfClass:[NSBitmapImageRep class]]) {
        cg_image = [rep CGImage];
        break;
      }
    }

    if (cg_image) {
      cursor_info.type = WebCursorInfo::TypeCustom;
      NSPoint hot_spot = [cursor hotSpot];
      cursor_info.hotSpot = WebKit::WebPoint(hot_spot.x, hot_spot.y);
      cursor_info.customImage = cg_image;
    } else {
      cursor_info.type = WebCursorInfo::TypePointer;
    }
  }

  InitFromCursorInfo(cursor_info);
}

void WebCursor::SetCustomData(const WebImage& image) {
  if (image.isNull())
    return;

  base::mac::ScopedCFTypeRef<CGColorSpaceRef> cg_color(
      CGColorSpaceCreateDeviceRGB());

  const WebSize& image_dimensions = image.size();
  int image_width = image_dimensions.width;
  int image_height = image_dimensions.height;

  size_t size = image_height * image_width * 4;
  custom_data_.resize(size);
  custom_size_.set_width(image_width);
  custom_size_.set_height(image_height);

  // These settings match up with the code in CreateCustomCursor() above; keep
  // them in sync.
  // TODO(avi): test to ensure that the flags here are correct for RGBA
  base::mac::ScopedCFTypeRef<CGContextRef> context(
      CGBitmapContextCreate(&custom_data_[0],
                            image_width,
                            image_height,
                            8,
                            image_width * 4,
                            cg_color.get(),
                            kCGImageAlphaPremultipliedLast |
                            kCGBitmapByteOrder32Big));
  CGRect rect = CGRectMake(0, 0, image_width, image_height);
  CGContextDrawImage(context.get(), rect, image.getCGImageRef());
}

void WebCursor::ImageFromCustomData(WebImage* image) const {
  if (custom_data_.empty())
    return;

  base::mac::ScopedCFTypeRef<CGImageRef> cg_image(
      CreateCGImageFromCustomData(custom_data_, custom_size_));
  *image = cg_image.get();
}

void WebCursor::InitPlatformData() {
  return;
}

bool WebCursor::SerializePlatformData(Pickle* pickle) const {
  return true;
}

bool WebCursor::DeserializePlatformData(const Pickle* pickle, void** iter) {
  return true;
}

bool WebCursor::IsPlatformDataEqual(const WebCursor& other) const {
  return true;
}

void WebCursor::CleanupPlatformData() {
  return;
}

void WebCursor::CopyPlatformData(const WebCursor& other) {
  return;
}
