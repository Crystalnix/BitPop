// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/desktop_background/desktop_background_view.h"

#include "ash/ash_export.h"
#include "ash/shell.h"
#include "ash/shell_window_ids.h"
#include "base/utf_string_conversions.h"
#include "grit/ui_resources.h"
#include "ui/aura/root_window.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/canvas.h"
#include "ui/views/widget/widget.h"

namespace ash {
namespace internal {

////////////////////////////////////////////////////////////////////////////////
// DesktopBackgroundView, public:

DesktopBackgroundView::DesktopBackgroundView() {
  wallpaper_ = *ResourceBundle::GetSharedInstance().GetBitmapNamed(
      IDR_AURA_WALLPAPER);
  wallpaper_.buildMipMap(false);
}

DesktopBackgroundView::~DesktopBackgroundView() {
}

////////////////////////////////////////////////////////////////////////////////
// DesktopBackgroundView, views::View overrides:

void DesktopBackgroundView::OnPaint(gfx::Canvas* canvas) {
  canvas->DrawBitmapInt(wallpaper_,
      0, 0, wallpaper_.width(), wallpaper_.height(),
      0, 0, width(), height(),
      true);
}

bool DesktopBackgroundView::OnMousePressed(const views::MouseEvent& event) {
  return true;
}

void DesktopBackgroundView::OnMouseReleased(const views::MouseEvent& event) {
  if (event.IsRightMouseButton())
    Shell::GetInstance()->ShowBackgroundMenu(GetWidget(), event.location());
}

views::Widget* CreateDesktopBackground() {
  views::Widget* desktop_widget = new views::Widget;
  views::Widget::InitParams params(
      views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  DesktopBackgroundView* view = new DesktopBackgroundView;
  params.delegate = view;
  desktop_widget->Init(params);
  Shell::GetInstance()->GetContainer(
      ash::internal::kShellWindowId_DesktopBackgroundContainer)->
      AddChild(desktop_widget->GetNativeView());
  desktop_widget->SetContentsView(view);
  desktop_widget->Show();
  desktop_widget->GetNativeView()->SetName("DesktopBackgroundView");
  return desktop_widget;
}

}  // namespace internal
}  // namespace ash
