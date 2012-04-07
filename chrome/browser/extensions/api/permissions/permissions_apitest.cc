// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/permissions/permissions_api.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/extension_prefs.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension_permission_set.h"
#include "net/base/mock_host_resolver.h"

namespace {

static void AddPattern(URLPatternSet* extent, const std::string& pattern) {
  int schemes = URLPattern::SCHEME_ALL;
  extent->AddPattern(URLPattern(schemes, pattern));
}

}  // namespace

class ExperimentalApiTest : public ExtensionApiTest {
public:
  void SetUpCommandLine(CommandLine* command_line) {
    ExtensionApiTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch(switches::kEnableExperimentalExtensionApis);
  }
};

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, PermissionsFail) {
  ASSERT_TRUE(RunExtensionTest("permissions/disabled")) << message_;

  // Since the experimental APIs require a flag, this will fail even though
  // it's enabled.
  // TODO(erikkay) This test is currently broken because LoadExtension in
  // ExtensionBrowserTest doesn't actually fail, it just times out.  To fix this
  // I'll need to add an EXTENSION_LOAD_ERROR notification, which is probably
  // too much for the branch.  I'll enable this on trunk later.
  //ASSERT_FALSE(RunExtensionTest("permissions/enabled"))) << message_;
}

IN_PROC_BROWSER_TEST_F(ExperimentalApiTest, PermissionsSucceed) {
  ASSERT_TRUE(RunExtensionTest("permissions/enabled")) << message_;
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, ExperimentalPermissionsFail) {
  // At the time this test is being created, there is no experimental
  // function that will not be graduating soon, and does not require a
  // tab id as an argument.  So, we need the tab permission to get
  // a tab id.
  ASSERT_TRUE(RunExtensionTest("permissions/experimental_disabled"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, FaviconPermission) {
  ASSERT_TRUE(RunExtensionTest("permissions/favicon")) << message_;
}

// Test functions and APIs that are always allowed (even if you ask for no
// permissions).
IN_PROC_BROWSER_TEST_F(ExtensionApiTest, AlwaysAllowed) {
  ASSERT_TRUE(RunExtensionTest("permissions/always_allowed")) << message_;
}

// Tests that the optional permissions API works correctly.
IN_PROC_BROWSER_TEST_F(ExtensionApiTest, OptionalPermissionsGranted) {
  // Mark all the tested APIs as granted to bypass the confirmation UI.
  ExtensionAPIPermissionSet apis;
  apis.insert(ExtensionAPIPermission::kTab);
  URLPatternSet explicit_hosts;
  AddPattern(&explicit_hosts, "http://*.c.com/*");
  scoped_refptr<ExtensionPermissionSet> granted_permissions =
      new ExtensionPermissionSet(apis, explicit_hosts, URLPatternSet());

  ExtensionPrefs* prefs =
      browser()->profile()->GetExtensionService()->extension_prefs();
  prefs->AddGrantedPermissions("kjmkgkdkpedkejedfhmfcenooemhbpbo",
                               granted_permissions);

  RequestPermissionsFunction::SetIgnoreUserGestureForTests(true);
  host_resolver()->AddRule("*.com", "127.0.0.1");
  ASSERT_TRUE(StartTestServer());
  EXPECT_TRUE(RunExtensionTest("permissions/optional")) << message_;
}

// Tests that the optional permissions API works correctly.
IN_PROC_BROWSER_TEST_F(ExtensionApiTest, OptionalPermissionsAutoConfirm) {
  // Rather than setting the granted permissions, set the UI autoconfirm flag
  // and run the same tests.
  RequestPermissionsFunction::SetAutoConfirmForTests(true);
  RequestPermissionsFunction::SetIgnoreUserGestureForTests(true);
  host_resolver()->AddRule("*.com", "127.0.0.1");
  ASSERT_TRUE(StartTestServer());
  EXPECT_TRUE(RunExtensionTest("permissions/optional")) << message_;
}

// Test that denying the optional permissions confirmation dialog works.
IN_PROC_BROWSER_TEST_F(ExtensionApiTest, OptionalPermissionsDeny) {
  RequestPermissionsFunction::SetAutoConfirmForTests(false);
  RequestPermissionsFunction::SetIgnoreUserGestureForTests(true);
  host_resolver()->AddRule("*.com", "127.0.0.1");
  ASSERT_TRUE(StartTestServer());
  EXPECT_TRUE(RunExtensionTest("permissions/optional_deny")) << message_;
}

// Tests that the permissions.request function must be called from within a
// user gesture.
IN_PROC_BROWSER_TEST_F(ExtensionApiTest, OptionalPermissionsGesture) {
  RequestPermissionsFunction::SetIgnoreUserGestureForTests(false);
  host_resolver()->AddRule("*.com", "127.0.0.1");
  ASSERT_TRUE(StartTestServer());
  EXPECT_TRUE(RunExtensionTest("permissions/optional_gesture")) << message_;
}
