// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views/window/native_frame_view.h"

#include "views/widget/native_widget_win.h"
#include "views/window/native_window.h"
#include "views/window/window.h"

namespace views {

////////////////////////////////////////////////////////////////////////////////
// NativeFrameView, public:

NativeFrameView::NativeFrameView(Window* frame)
    : NonClientFrameView(),
      frame_(frame) {
}

NativeFrameView::~NativeFrameView() {
}

////////////////////////////////////////////////////////////////////////////////
// NativeFrameView, NonClientFrameView overrides:

gfx::Rect NativeFrameView::GetBoundsForClientView() const {
  return gfx::Rect(0, 0, width(), height());
}

gfx::Rect NativeFrameView::GetWindowBoundsForClientBounds(
    const gfx::Rect& client_bounds) const {
  RECT rect = client_bounds.ToRECT();
  NativeWidgetWin* widget_win =
      static_cast<NativeWidgetWin*>(frame_->native_window()->AsNativeWidget());
  AdjustWindowRectEx(&rect, widget_win->window_style(), FALSE,
                     widget_win->window_ex_style());
  return gfx::Rect(rect);
}

int NativeFrameView::NonClientHitTest(const gfx::Point& point) {
  return frame_->client_view()->NonClientHitTest(point);
}

void NativeFrameView::GetWindowMask(const gfx::Size& size,
                                    gfx::Path* window_mask) {
  // Nothing to do, we use the default window mask.
}

void NativeFrameView::EnableClose(bool enable) {
  // Nothing to do, handled automatically by Window.
}

void NativeFrameView::ResetWindowControls() {
  // Nothing to do.
}

void NativeFrameView::UpdateWindowIcon() {
  // Nothing to do.
}

gfx::Size NativeFrameView::GetPreferredSize() {
  gfx::Size pref = frame_->client_view()->GetPreferredSize();
  gfx::Rect bounds(0, 0, pref.width(), pref.height());
  return frame_->non_client_view()->GetWindowBoundsForClientBounds(
      bounds).size();
}

}  // namespace views
