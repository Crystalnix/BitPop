// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/tab_contents/simple_alert_infobar_delegate.h"

#include "third_party/skia/include/core/SkBitmap.h"

SimpleAlertInfoBarDelegate::SimpleAlertInfoBarDelegate(
    InfoBarTabHelper* infobar_helper,
    gfx::Image* icon,
    const string16& message,
    bool auto_expire)
    : ConfirmInfoBarDelegate(infobar_helper),
      icon_(icon),
      message_(message),
      auto_expire_(auto_expire) {
}

SimpleAlertInfoBarDelegate::~SimpleAlertInfoBarDelegate() {
}

gfx::Image* SimpleAlertInfoBarDelegate::GetIcon() const {
  return icon_;
}

string16 SimpleAlertInfoBarDelegate::GetMessageText() const {
  return message_;
}

int SimpleAlertInfoBarDelegate::GetButtons() const {
  return BUTTON_NONE;
}

bool SimpleAlertInfoBarDelegate::ShouldExpireInternal(
      const content::LoadCommittedDetails& details) const {
  return auto_expire_ && ConfirmInfoBarDelegate::ShouldExpireInternal(details);
}
