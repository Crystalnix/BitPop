// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/compact_browser_frame_view.h"

#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "grit/theme_resources_standard.h"
#include "ui/base/hit_test.h"
#include "ui/base/theme_provider.h"

namespace {
// Width of area to the left of first tab for which mouse events should be
// forwarded to the first tab.
const int kLeftPad = 15;
// Additional pixels of pad above the tabs.
const int kTopPad = 4;
// To align theme bitmaps correctly we return this offset.
const int kThemeOffset = -5;
}

// CompactBrowserFrameView adds a few pixels of pad to the top of the
// tabstrip and clicks left of first tab should be forwarded to the first tab.
// To enable this we have to grab mouse events in that area and forward them on
// to the NonClientView. We do this by overriding HitTest(), NonClientHitTest()
// and GetEventHandlerForPoint().
CompactBrowserFrameView::CompactBrowserFrameView(
    BrowserFrame* frame, BrowserView* browser_view)
    : OpaqueBrowserFrameView(frame, browser_view) {
}

CompactBrowserFrameView::~CompactBrowserFrameView() {
}

int CompactBrowserFrameView::NonClientHitTest(const gfx::Point& point) {
  if (point.x() < kLeftPad || point.y() < kTopPad)
    return HTNOWHERE;
  return OpaqueBrowserFrameView::NonClientHitTest(point);
}

bool CompactBrowserFrameView::HitTest(const gfx::Point& l) const {
  if (l.x() < kLeftPad || l.y() < kTopPad)
    return true;
  return OpaqueBrowserFrameView::HitTest(l);
}

views::View* CompactBrowserFrameView::GetEventHandlerForPoint(
    const gfx::Point& point) {
  if (point.x() < kLeftPad || point.y() < kTopPad) {
    gfx::Point nc_point(std::max(kLeftPad, point.x()),
                        std::max(kTopPad, point.y()));
    views::NonClientView* nc_view = frame()->non_client_view();
    View::ConvertPointToView(this, nc_view, &nc_point);
    return nc_view->GetEventHandlerForPoint(nc_point);
  }
  return OpaqueBrowserFrameView::GetEventHandlerForPoint(point);
}

int CompactBrowserFrameView::GetHorizontalTabStripVerticalOffset(
    bool restored) const {
  return NonClientTopBorderHeight(restored) + kTopPad;
}

void CompactBrowserFrameView::ModifyMaximizedFramePainting(
    int* top_offset,
    SkBitmap** theme_frame,
    SkBitmap** left_corner,
    SkBitmap** right_corner) {
  *top_offset = kThemeOffset;
  ui::ThemeProvider* tp = GetThemeProvider();
  if (!ThemeServiceFactory::GetForProfile(
      browser_view()->browser()->profile())->UsingDefaultTheme())
    return;
  if (browser_view()->IsOffTheRecord()) {
#if defined(USE_AURA)
    *theme_frame = tp->GetBitmapNamed(IDR_THEME_FRAME_INCOGNITO_COMPACT);
#endif
    *left_corner = tp->GetBitmapNamed(IDR_THEME_FRAME_INCOGNITO_LEFT);
    *right_corner = tp->GetBitmapNamed(IDR_THEME_FRAME_INCOGNITO_RIGHT);
  } else {
#if defined(USE_AURA)
    *theme_frame = tp->GetBitmapNamed(IDR_THEME_FRAME_COMPACT);
#endif
    *left_corner = tp->GetBitmapNamed(IDR_THEME_FRAME_LEFT);
    *right_corner = tp->GetBitmapNamed(IDR_THEME_FRAME_RIGHT);
  }
}

