// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views/controls/menu/menu_host.h"

#include "views/controls/menu/menu_controller.h"
#include "views/controls/menu/menu_host_root_view.h"
#include "views/controls/menu/menu_item_view.h"
#include "views/controls/menu/native_menu_host.h"
#include "views/controls/menu/submenu_view.h"
#include "views/widget/native_widget.h"
#include "views/widget/widget.h"

namespace views {

////////////////////////////////////////////////////////////////////////////////
// MenuHost, public:

MenuHost::MenuHost(SubmenuView* submenu)
    : ALLOW_THIS_IN_INITIALIZER_LIST(native_menu_host_(
          NativeMenuHost::CreateNativeMenuHost(this))),
      submenu_(submenu),
      destroying_(false) {
  Widget::CreateParams params;
  params.type = Widget::CreateParams::TYPE_MENU;
  params.has_dropshadow = true;
  GetWidget()->SetCreateParams(params);
}

MenuHost::~MenuHost() {
}

void MenuHost::InitMenuHost(gfx::NativeWindow parent,
                            const gfx::Rect& bounds,
                            View* contents_view,
                            bool do_capture) {
  native_menu_host_->InitMenuHost(parent, bounds);
  GetWidget()->SetContentsView(contents_view);
  ShowMenuHost(do_capture);
}

bool MenuHost::IsMenuHostVisible() {
  return GetWidget()->IsVisible();
}

void MenuHost::ShowMenuHost(bool do_capture) {
  GetWidget()->Show();
  if (do_capture)
    native_menu_host_->StartCapturing();
}

void MenuHost::HideMenuHost() {
  ReleaseMenuHostCapture();
  GetWidget()->Hide();
}

void MenuHost::DestroyMenuHost() {
  HideMenuHost();
  destroying_ = true;
  static_cast<MenuHostRootView*>(GetWidget()->GetRootView())->ClearSubmenu();
  GetWidget()->Close();
}

void MenuHost::SetMenuHostBounds(const gfx::Rect& bounds) {
  GetWidget()->SetBounds(bounds);
}

void MenuHost::ReleaseMenuHostCapture() {
  if (GetWidget()->native_widget()->HasMouseCapture())
    GetWidget()->native_widget()->ReleaseMouseCapture();
}

Widget* MenuHost::GetWidget() {
  return native_menu_host_->AsNativeWidget()->GetWidget();
}

NativeWidget* MenuHost::GetNativeWidget() {
  return native_menu_host_->AsNativeWidget();
}

////////////////////////////////////////////////////////////////////////////////
// MenuHost, internal::NativeMenuHostDelegate implementation:

void MenuHost::OnNativeMenuHostDestroy() {
  if (!destroying_) {
    // We weren't explicitly told to destroy ourselves, which means the menu was
    // deleted out from under us (the window we're parented to was closed). Tell
    // the SubmenuView to drop references to us.
    submenu_->MenuHostDestroyed();
  }
}

void MenuHost::OnNativeMenuHostCancelCapture() {
  if (destroying_)
    return;
  MenuController* menu_controller =
      submenu_->GetMenuItem()->GetMenuController();
  if (menu_controller && !menu_controller->drag_in_progress())
    menu_controller->CancelAll();
}

RootView* MenuHost::CreateRootView() {
  return new MenuHostRootView(GetWidget(), submenu_);
}

bool MenuHost::ShouldReleaseCaptureOnMouseRelease() const {
  return false;
}

}  // namespace views
