// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <windows.h>
#include <psapi.h>

#include "skia/ext/bitmap_platform_device_win.h"

#include "skia/ext/bitmap_platform_device_data.h"
#include "third_party/skia/include/core/SkMatrix.h"
#include "third_party/skia/include/core/SkRefCnt.h"
#include "third_party/skia/include/core/SkRegion.h"
#include "third_party/skia/include/core/SkUtils.h"

namespace skia {

// Disable optimizations during crash analysis.
#pragma optimize("", off)

// Crash On Failure. |address| should be a number less than 4000.
#define COF(address, condition) if (!(condition)) *((int*) address) = 0

// This is called when a bitmap allocation fails, and this function tries to
// determine why it might have failed, and crash on different
// lines. This allows us to see in crash dumps the most likely reason for the
// failure. It takes the size of the bitmap we were trying to allocate as its
// arguments so we can check that as well.
//
// Note that in a sandboxed renderer this function crashes when trying to
// call GetProcessMemoryInfo() because it tries to load psapi.dll, which
// is fine but gives you a very hard to read crash dump.
void CrashForBitmapAllocationFailure(int w, int h, unsigned int error) {
  // Store the extended error info in a place easy to find at debug time.
  unsigned int diag_error = 0;
  // If the bitmap is ginormous, then we probably can't allocate it.
  // We use 32M pixels = 128MB @ 4 bytes per pixel.
  const LONG_PTR kGinormousBitmapPxl = 32000000;
  COF(1, LONG_PTR(w) * LONG_PTR(h) < kGinormousBitmapPxl);

  // The maximum number of GDI objects per process is 10K. If we're very close
  // to that, it's probably the problem.
  const unsigned int kLotsOfGDIObjects = 9990;
  unsigned int num_gdi_objects = GetGuiResources(GetCurrentProcess(),
                                                 GR_GDIOBJECTS);
  if (num_gdi_objects == 0) {
    diag_error = GetLastError();
    COF(2, false);
  }
  COF(3, num_gdi_objects < kLotsOfGDIObjects);

  // If we're using a crazy amount of virtual address space, then maybe there
  // isn't enough for our bitmap.
  const SIZE_T kLotsOfMem = 1500000000;  // 1.5GB.
  PROCESS_MEMORY_COUNTERS_EX pmc;
  pmc.cb = sizeof(pmc);
  if (!GetProcessMemoryInfo(GetCurrentProcess(),
                            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                            sizeof(pmc))) {
    diag_error = GetLastError();
    COF(4, false);
  }
  COF(5, pmc.PagefileUsage < kLotsOfMem);
  COF(6, pmc.PrivateUsage < kLotsOfMem);
  // Ok but we are somehow out of memory?
  COF(7, error != ERROR_NOT_ENOUGH_MEMORY);
}

// Crashes the process. This is called when a bitmap allocation fails but
// unlike its cousin CrashForBitmapAllocationFailure() it tries to detect if
// the issue was a non-valid shared bitmap handle.
void CrashIfInvalidSection(HANDLE shared_section) {
  DWORD handle_info = 0;
  COF(8, ::GetHandleInformation(shared_section, &handle_info) == TRUE);
}

BitmapPlatformDevice::BitmapPlatformDeviceData::BitmapPlatformDeviceData(
    HBITMAP hbitmap)
    : bitmap_context_(hbitmap),
      hdc_(NULL),
      config_dirty_(true),  // Want to load the config next time.
      transform_(SkMatrix::I()) {
  // Initialize the clip region to the entire bitmap.
  BITMAP bitmap_data;
  if (GetObject(bitmap_context_, sizeof(BITMAP), &bitmap_data)) {
    SkIRect rect;
    rect.set(0, 0, bitmap_data.bmWidth, bitmap_data.bmHeight);
    clip_region_ = SkRegion(rect);
  }
}

BitmapPlatformDevice::BitmapPlatformDeviceData::~BitmapPlatformDeviceData() {
  if (hdc_)
    ReleaseBitmapDC();

  // this will free the bitmap data as well as the bitmap handle
  DeleteObject(bitmap_context_);
}

HDC BitmapPlatformDevice::BitmapPlatformDeviceData::GetBitmapDC() {
  if (!hdc_) {
    hdc_ = CreateCompatibleDC(NULL);
    InitializeDC(hdc_);
    HGDIOBJ old_bitmap = SelectObject(hdc_, bitmap_context_);
    // When the memory DC is created, its display surface is exactly one
    // monochrome pixel wide and one monochrome pixel high. Since we select our
    // own bitmap, we must delete the previous one.
    DeleteObject(old_bitmap);
  }

  LoadConfig();
  return hdc_;
}

void BitmapPlatformDevice::BitmapPlatformDeviceData::ReleaseBitmapDC() {
  SkASSERT(hdc_);
  DeleteDC(hdc_);
  hdc_ = NULL;
}

bool BitmapPlatformDevice::BitmapPlatformDeviceData::IsBitmapDCCreated()
    const {
  return hdc_ != NULL;
}


void BitmapPlatformDevice::BitmapPlatformDeviceData::SetMatrixClip(
    const SkMatrix& transform,
    const SkRegion& region) {
  transform_ = transform;
  clip_region_ = region;
  config_dirty_ = true;
}

void BitmapPlatformDevice::BitmapPlatformDeviceData::LoadConfig() {
  if (!config_dirty_ || !hdc_)
    return;  // Nothing to do.
  config_dirty_ = false;

  // Transform.
  LoadTransformToDC(hdc_, transform_);
  LoadClippingRegionToDC(hdc_, clip_region_, transform_);
}

// We use this static factory function instead of the regular constructor so
// that we can create the pixel data before calling the constructor. This is
// required so that we can call the base class' constructor with the pixel
// data.
BitmapPlatformDevice* BitmapPlatformDevice::Create(
    int width,
    int height,
    bool is_opaque,
    HANDLE shared_section) {
  SkBitmap bitmap;

  // CreateDIBSection appears to get unhappy if we create an empty bitmap, so
  // just create a minimal bitmap
  if ((width == 0) || (height == 0)) {
    width = 1;
    height = 1;
  }

  BITMAPINFOHEADER hdr = {0};
  hdr.biSize = sizeof(BITMAPINFOHEADER);
  hdr.biWidth = width;
  hdr.biHeight = -height;  // minus means top-down bitmap
  hdr.biPlanes = 1;
  hdr.biBitCount = 32;
  hdr.biCompression = BI_RGB;  // no compression
  hdr.biSizeImage = 0;
  hdr.biXPelsPerMeter = 1;
  hdr.biYPelsPerMeter = 1;
  hdr.biClrUsed = 0;
  hdr.biClrImportant = 0;

  void* data = NULL;
  HBITMAP hbitmap = CreateDIBSection(NULL,
                                     reinterpret_cast<BITMAPINFO*>(&hdr), 0,
                                     &data,
                                     shared_section, 0);
  if (!hbitmap) {
    // Investigate we failed. If we know the reason, crash in a specific place.
    unsigned int error = GetLastError();
    if (shared_section)
      CrashIfInvalidSection(shared_section);
    CrashForBitmapAllocationFailure(width, height, error);
    return NULL;
  }

  bitmap.setConfig(SkBitmap::kARGB_8888_Config, width, height);
  bitmap.setPixels(data);
  bitmap.setIsOpaque(is_opaque);

#ifndef NDEBUG
  // If we were given data, then don't clobber it!
  if (!shared_section && is_opaque)
    // To aid in finding bugs, we set the background color to something
    // obviously wrong so it will be noticable when it is not cleared
    bitmap.eraseARGB(255, 0, 255, 128);  // bright bluish green
#endif

  // The device object will take ownership of the HBITMAP. The initial refcount
  // of the data object will be 1, which is what the constructor expects.
  return new BitmapPlatformDevice(new BitmapPlatformDeviceData(hbitmap),
                                  bitmap);
}

// static
BitmapPlatformDevice* BitmapPlatformDevice::Create(int width, int height,
                                                   bool is_opaque) {
  return Create(width, height, is_opaque, NULL);
}

// static
BitmapPlatformDevice* BitmapPlatformDevice::CreateAndClear(int width,
                                                           int height,
                                                           bool is_opaque) {
  BitmapPlatformDevice* device = BitmapPlatformDevice::Create(width, height,
                                                              is_opaque);
  if (!is_opaque)
    device->accessBitmap(true).eraseARGB(0, 0, 0, 0);
  return device;
}

// The device will own the HBITMAP, which corresponds to also owning the pixel
// data. Therefore, we do not transfer ownership to the SkDevice's bitmap.
BitmapPlatformDevice::BitmapPlatformDevice(
    BitmapPlatformDeviceData* data,
    const SkBitmap& bitmap)
    : SkDevice(bitmap),
      data_(data) {
  // The data object is already ref'ed for us by create().
  SkDEBUGCODE(begin_paint_count_ = 0);
  SetPlatformDevice(this, this);
}

BitmapPlatformDevice::~BitmapPlatformDevice() {
  SkASSERT(begin_paint_count_ == 0);
  data_->unref();
}

HDC BitmapPlatformDevice::BeginPlatformPaint() {
  SkDEBUGCODE(begin_paint_count_++);
  return data_->GetBitmapDC();
}

void BitmapPlatformDevice::EndPlatformPaint() {
  SkASSERT(begin_paint_count_--);
  PlatformDevice::EndPlatformPaint();
}

void BitmapPlatformDevice::setMatrixClip(const SkMatrix& transform,
                                         const SkRegion& region,
                                         const SkClipStack&) {
  data_->SetMatrixClip(transform, region);
}

void BitmapPlatformDevice::DrawToNativeContext(HDC dc, int x, int y,
                                               const RECT* src_rect) {
  bool created_dc = !data_->IsBitmapDCCreated();
  HDC source_dc = BeginPlatformPaint();

  RECT temp_rect;
  if (!src_rect) {
    temp_rect.left = 0;
    temp_rect.right = width();
    temp_rect.top = 0;
    temp_rect.bottom = height();
    src_rect = &temp_rect;
  }

  int copy_width = src_rect->right - src_rect->left;
  int copy_height = src_rect->bottom - src_rect->top;

  // We need to reset the translation for our bitmap or (0,0) won't be in the
  // upper left anymore
  SkMatrix identity;
  identity.reset();

  LoadTransformToDC(source_dc, identity);
  if (isOpaque()) {
    BitBlt(dc,
           x,
           y,
           copy_width,
           copy_height,
           source_dc,
           src_rect->left,
           src_rect->top,
           SRCCOPY);
  } else {
    SkASSERT(copy_width != 0 && copy_height != 0);
    BLENDFUNCTION blend_function = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    GdiAlphaBlend(dc,
                  x,
                  y,
                  copy_width,
                  copy_height,
                  source_dc,
                  src_rect->left,
                  src_rect->top,
                  copy_width,
                  copy_height,
                  blend_function);
  }
  LoadTransformToDC(source_dc, data_->transform());

  EndPlatformPaint();
  if (created_dc)
    data_->ReleaseBitmapDC();
}

const SkBitmap& BitmapPlatformDevice::onAccessBitmap(SkBitmap* bitmap) {
  // FIXME(brettw) OPTIMIZATION: We should only flush if we know a GDI
  // operation has occurred on our DC.
  if (data_->IsBitmapDCCreated())
    GdiFlush();
  return *bitmap;
}

SkDevice* BitmapPlatformDevice::onCreateCompatibleDevice(
    SkBitmap::Config config, int width, int height, bool isOpaque,
    Usage /*usage*/) {
  SkASSERT(config == SkBitmap::kARGB_8888_Config);
  SkDevice* bitmap_device = BitmapPlatformDevice::CreateAndClear(width, height,
                                                                 isOpaque);
  return bitmap_device;
}

}  // namespace skia
