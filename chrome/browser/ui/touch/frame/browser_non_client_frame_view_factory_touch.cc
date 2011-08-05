// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/browser_non_client_frame_view.h"

#include "chrome/browser/ui/touch/frame/touch_browser_frame_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/popup_non_client_frame_view.h"

namespace browser {

BrowserNonClientFrameView* CreateBrowserNonClientFrameView(
    BrowserFrame* frame, BrowserView* browser_view) {
  if (browser_view->IsBrowserTypePopup() ||
      browser_view->IsBrowserTypePanel()) {
    // TODO(anicolao): implement popups for touch
    NOTIMPLEMENTED();
    return new PopupNonClientFrameView(frame);
  } else {
    return new TouchBrowserFrameView(frame, browser_view);
  }
}

}  // browser
