// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/frame/bubble_window.h"

#include <gtk/gtk.h>

#include "chrome/browser/chromeos/frame/bubble_frame_view.h"
#include "chrome/browser/chromeos/wm_ipc.h"
#include "third_party/cros/chromeos_wm_ipc_enums.h"
#include "ui/gfx/skia_utils_gtk.h"
#include "views/window/non_client_view.h"

namespace {

bool IsInsideCircle(int x0, int y0, int x1, int y1, int r) {
  return (x0 - x1) * (x0 - x1) + (y0 - y1) * (y0 - y1) <= r * r;
}

void SetRegionUnionWithPoint(int i, int j, GdkRegion* region) {
  GdkRectangle rect = {i, j, 1, 1};
  gdk_region_union_with_rect(region, &rect);
}

}  // namespace

namespace chromeos {

// static
const SkColor BubbleWindow::kBackgroundColor = SK_ColorWHITE;

BubbleWindow::BubbleWindow(views::Window* window)
    : views::NativeWindowGtk(window) {
  MakeTransparent();
}

void BubbleWindow::InitNativeWidget(const views::Widget::InitParams& params) {
  views::NativeWindowGtk::InitNativeWidget(params);

  // Turn on double buffering so that the hosted GtkWidgets does not
  // flash as in http://crosbug.com/9065.
  EnableDoubleBuffer(true);

  GdkColor background_color = gfx::SkColorToGdkColor(kBackgroundColor);
  gtk_widget_modify_bg(GetNativeView(), GTK_STATE_NORMAL, &background_color);

  // A work-around for http://crosbug.com/8538. All GdkWindow of top-level
  // GtkWindow should participate _NET_WM_SYNC_REQUEST protocol and window
  // manager should only show the window after getting notified. And we
  // should only notify window manager after at least one paint is done.
  // TODO(xiyuan): Figure out the right fix.
  gtk_widget_realize(GetNativeView());
  gdk_window_set_back_pixmap(GetNativeView()->window, NULL, FALSE);
  gtk_widget_realize(window_contents());
  gdk_window_set_back_pixmap(window_contents()->window, NULL, FALSE);
}

void BubbleWindow::TrimMargins(int margin_left, int margin_right,
                               int margin_top, int margin_bottom,
                               int border_radius) {
  gfx::Size size = GetWindow()->non_client_view()->GetPreferredSize();
  const int w = size.width() - margin_left - margin_right;
  const int h = size.height() - margin_top - margin_bottom;
  GdkRectangle rect0 = {0, border_radius, w, h - 2 * border_radius};
  GdkRectangle rect1 = {border_radius, 0, w - 2 * border_radius, h};
  GdkRegion* region = gdk_region_rectangle(&rect0);
  gdk_region_union_with_rect(region, &rect1);

  // Top Left
  for (int i = 0; i < border_radius; ++i) {
    for (int j = 0; j < border_radius; ++j) {
      if (IsInsideCircle(i + 0.5, j + 0.5, border_radius, border_radius,
                         border_radius)) {
        SetRegionUnionWithPoint(i, j, region);
      }
    }
  }
  // Top Right
  for (int i = w - border_radius - 1; i < w; ++i) {
    for (int j = 0; j < border_radius; ++j) {
      if (IsInsideCircle(i + 0.5, j + 0.5, w - border_radius - 1,
                         border_radius, border_radius)) {
        SetRegionUnionWithPoint(i, j, region);
      }
    }
  }
  // Bottom Left
  for (int i = 0; i < border_radius; ++i) {
    for (int j = h - border_radius - 1; j < h; ++j) {
      if (IsInsideCircle(i + 0.5, j + 0.5, border_radius, h - border_radius - 1,
                         border_radius)) {
        SetRegionUnionWithPoint(i, j, region);
      }
    }
  }
  // Bottom Right
  for (int i = w - border_radius - 1; i < w; ++i) {
    for (int j = h - border_radius - 1; j < h; ++j) {
      if (IsInsideCircle(i + 0.5, j + 0.5, w - border_radius - 1,
                         h - border_radius - 1, border_radius)) {
        SetRegionUnionWithPoint(i, j, region);
      }
    }
  }

  gdk_window_shape_combine_region(window_contents()->window, region,
                                  margin_left, margin_top);
  gdk_region_destroy(region);
}

views::Window* BubbleWindow::Create(
    gfx::NativeWindow parent,
    const gfx::Rect& bounds,
    Style style,
    views::WindowDelegate* window_delegate) {
  views::Window* window = new views::Window;
  BubbleWindow* bubble_window = new BubbleWindow(window);
  window->non_client_view()->SetFrameView(
      new BubbleFrameView(window, window_delegate, style));
  views::Window::InitParams params(window_delegate);
  params.parent_window = parent;
  params.native_window = bubble_window;
  params.widget_init_params.parent = GTK_WIDGET(parent);
  params.widget_init_params.bounds = bounds;
  params.widget_init_params.parent = GTK_WIDGET(parent);
  params.widget_init_params.native_widget = bubble_window;
  window->InitWindow(params);

  if (style == STYLE_XSHAPE) {
    const int kMarginLeft = 14;
    const int kMarginRight = 14;
    const int kMarginTop = 12;
    const int kMarginBottom = 16;
    const int kBorderRadius = 8;
    static_cast<BubbleWindow*>(window->native_window())->
        TrimMargins(kMarginLeft, kMarginRight, kMarginTop, kMarginBottom,
                    kBorderRadius);
  }

  return window;
}

}  // namespace chromeos
