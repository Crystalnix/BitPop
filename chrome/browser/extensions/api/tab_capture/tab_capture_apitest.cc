// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/common/chrome_version_info.h"
#include "chrome/common/extensions/feature_switch.h"
#include "chrome/common/extensions/features/feature.h"
#include "content/public/common/content_switches.h"

namespace chrome {

namespace {

class TabCaptureApiTest : public ExtensionApiTest {
 public:
  TabCaptureApiTest() : current_channel_(VersionInfo::CHANNEL_UNKNOWN) {}

 private:
  extensions::Feature::ScopedCurrentChannel current_channel_;
};

}  // namespace

IN_PROC_BROWSER_TEST_F(TabCaptureApiTest, TabCapture) {
  extensions::FeatureSwitch::ScopedOverride tab_capture(
      extensions::FeatureSwitch::tab_capture(), true);
  ASSERT_TRUE(RunExtensionTest("tab_capture/experimental")) << message_;
}

IN_PROC_BROWSER_TEST_F(TabCaptureApiTest, TabCapturePermissionsTestFlagOn) {
  extensions::FeatureSwitch::ScopedOverride tab_capture(
      extensions::FeatureSwitch::tab_capture(), true);
  ASSERT_TRUE(RunExtensionTest("tab_capture/permissions")) << message_;
}

IN_PROC_BROWSER_TEST_F(TabCaptureApiTest, TabCapturePermissionsTestFlagOff) {
  extensions::FeatureSwitch::ScopedOverride tab_capture(
      extensions::FeatureSwitch::tab_capture(), false);
  ASSERT_TRUE(RunExtensionTest("tab_capture/permissions")) << message_;
}

}  // namespace chrome
