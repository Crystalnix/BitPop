// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/tray/system_tray_item.h"

#include "ash/shell.h"
#include "ash/system/tray/system_tray.h"
#include "ash/system/tray/tray_constants.h"
#include "ash/system/tray/tray_item_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/view.h"

namespace ash {

SystemTrayItem::SystemTrayItem() {
}

SystemTrayItem::~SystemTrayItem() {
}

views::View* SystemTrayItem::CreateTrayView(user::LoginStatus status) {
  return NULL;
}

views::View* SystemTrayItem::CreateDefaultView(user::LoginStatus status) {
  return NULL;
}

views::View* SystemTrayItem::CreateDetailedView(user::LoginStatus status) {
  return NULL;
}

views::View* SystemTrayItem::CreateNotificationView(user::LoginStatus status) {
  return NULL;
}

void SystemTrayItem::DestroyTrayView() {
}

void SystemTrayItem::DestroyDefaultView() {
}

void SystemTrayItem::DestroyDetailedView() {
}

void SystemTrayItem::DestroyNotificationView() {
}

void SystemTrayItem::TransitionDetailedView() {
  Shell::GetInstance()->system_tray()->ShowDetailedView(this, 0, true,
      BUBBLE_USE_EXISTING);
}

void SystemTrayItem::UpdateAfterLoginStatusChange(user::LoginStatus status) {
}

void SystemTrayItem::UpdateAfterShelfAlignmentChange(ShelfAlignment alignment) {
}

void SystemTrayItem::PopupDetailedView(int for_seconds, bool activate) {
  Shell::GetInstance()->system_tray()->ShowDetailedView(
      this, for_seconds, activate, BUBBLE_CREATE_NEW);
}

void SystemTrayItem::SetDetailedViewCloseDelay(int for_seconds) {
  Shell::GetInstance()->system_tray()->SetDetailedViewCloseDelay(for_seconds);
}

void SystemTrayItem::HideDetailedView() {
  Shell::GetInstance()->system_tray()->HideDetailedView(this);
}

void SystemTrayItem::ShowNotificationView() {
  Shell::GetInstance()->system_tray()->ShowNotificationView(this);
}

void SystemTrayItem::HideNotificationView() {
  Shell::GetInstance()->system_tray()->HideNotificationView(this);
}

}  // namespace ash
