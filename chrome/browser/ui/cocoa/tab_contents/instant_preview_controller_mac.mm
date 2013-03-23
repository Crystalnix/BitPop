// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/cocoa/tab_contents/instant_preview_controller_mac.h"

#include "chrome/browser/instant/instant_model.h"
#include "chrome/browser/ui/browser.h"
#import "chrome/browser/ui/cocoa/browser_window_controller.h"
#import "chrome/browser/ui/cocoa/tab_contents/previewable_contents_controller.h"

InstantPreviewControllerMac::InstantPreviewControllerMac(
    Browser* browser,
    BrowserWindowController* window,
    PreviewableContentsController* preview)
    : InstantPreviewController(browser),
      window_(window),
      preview_(preview) {
}

InstantPreviewControllerMac::~InstantPreviewControllerMac() {
}

void InstantPreviewControllerMac::PreviewStateChanged(
    const InstantModel& model) {
  if (model.mode().is_ntp() || model.mode().is_search_suggestions()) {
    [preview_ showPreview:model.GetPreviewContents()
                   height:model.height()
              heightUnits:model.height_units()];
  } else {
    [preview_ hidePreview];
  }
  browser_->MaybeUpdateBookmarkBarStateForInstantPreview(model.mode());
}
