// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <Cocoa/Cocoa.h>

#include "base/memory/ref_counted.h"
#include "chrome/browser/profiles/profile.h"
#import "chrome/browser/ui/cocoa/content_settings/collected_cookies_mac.h"
#include "content/browser/renderer_host/test_render_view_host.h"
#include "content/browser/site_instance.h"
#include "content/browser/tab_contents/test_tab_contents.h"
#include "chrome/test/testing_profile.h"

namespace {

class CollectedCookiesWindowControllerTest : public RenderViewHostTestHarness {
};

TEST_F(CollectedCookiesWindowControllerTest, Construction) {
  BrowserThread ui_thread(BrowserThread::UI, MessageLoop::current());
  // Create a test tab.  SiteInstance will be deleted when tabContents is
  // deleted.
  SiteInstance* instance =
      SiteInstance::CreateSiteInstance(profile_.get());
  TestTabContents* tabContents = new TestTabContents(profile_.get(),
                                                      instance);
  CollectedCookiesWindowController* controller =
      [[CollectedCookiesWindowController alloc]
          initWithTabContents:tabContents];

  [controller release];

  delete tabContents;
}

}  // namespace

