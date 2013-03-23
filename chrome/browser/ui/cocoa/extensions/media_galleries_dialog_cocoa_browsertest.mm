// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/cocoa/extensions/media_galleries_dialog_cocoa.h"

#include "chrome/browser/media_gallery/media_galleries_dialog_controller_mock.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/cocoa/constrained_window/constrained_window_alert.h"
#include "chrome/browser/ui/constrained_window_tab_helper.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/test/test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;

namespace chrome {

class MediaGalleriesDialogBrowserTest : public InProcessBrowserTest {
};

// Verify that programatically closing the constrained window correctly closes
// the sheet.
IN_PROC_BROWSER_TEST_F(MediaGalleriesDialogBrowserTest, Close) {
  NiceMock<MediaGalleriesDialogControllerMock> controller;

  content::WebContents* web_contents = chrome::GetActiveWebContents(browser());
  EXPECT_CALL(controller, web_contents()).
      WillRepeatedly(Return(web_contents));

  MediaGalleriesDialogController::KnownGalleryPermissions permissions;
  EXPECT_CALL(controller, permissions()).
      WillRepeatedly(ReturnRef(permissions));

  scoped_ptr<MediaGalleriesDialogCocoa> dialog(
      static_cast<MediaGalleriesDialogCocoa*>(
          MediaGalleriesDialog::Create(&controller)));
  scoped_nsobject<NSWindow> window([[dialog->alert_ window] retain]);
  EXPECT_TRUE([window isVisible]);

  ConstrainedWindowTabHelper* constrained_window_tab_helper =
      ConstrainedWindowTabHelper::FromWebContents(web_contents);
  constrained_window_tab_helper->CloseConstrainedWindows();
  EXPECT_FALSE([window isVisible]);
}

}  // namespace chrome
