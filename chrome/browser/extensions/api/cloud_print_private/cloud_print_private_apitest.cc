// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/stringprintf.h"
#include "chrome/browser/extensions/api/cloud_print_private/cloud_print_private_api.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/base/ui_test_utils.h"
#include "net/base/mock_host_resolver.h"

// A base class for tests below.
class ExtensionCloudPrintPrivateApiTest : public ExtensionApiTest {
 public:
  void SetUpCommandLine(CommandLine* command_line) OVERRIDE {
    ExtensionApiTest::SetUpCommandLine(command_line);
    command_line->AppendSwitchASCII(switches::kCloudPrintServiceURL,
        "http://www.cloudprintapp.com/files/extensions/api_test/"
        "cloud_print_private");
  }

  void SetUpInProcessBrowserTestFixture() OVERRIDE {
    // Start up the test server and get us ready for calling the install
    // API functions.
    host_resolver()->AddRule("www.cloudprintapp.com", "127.0.0.1");
    ASSERT_TRUE(test_server()->Start());
  }

 protected:
  // Returns a test server URL, but with host 'www.cloudprintapp.com' so it
   // matches the cloud print app's extent that we set up via command line
   // flags.
  GURL GetTestServerURL(const std::string& path) {
    GURL url = test_server()->GetURL(
        "files/extensions/api_test/cloud_print_private/" + path);

    // Replace the host with 'www.cloudprintapp.com' so it matches the cloud
    // print app's extent.
    GURL::Replacements replace_host;
    std::string host_str("www.cloudprintapp.com");
    replace_host.SetHostStr(host_str);
    return url.ReplaceComponents(replace_host);
  }
};

#if !defined(OS_CHROMEOS)
IN_PROC_BROWSER_TEST_F(ExtensionCloudPrintPrivateApiTest,
                       CloudPrintSetCredentialsSuccessHosted) {
  // Run this as a hosted app. Since we have overridden the cloud print service
  // URL in the command line, this URL should match the web extent for our
  // cloud print component app and it should work.
  extensions::CloudPrintSetCredentialsFunction::SetTestMode(true);
  GURL page_url = GetTestServerURL(
      "enable_chrome_connector/cloud_print_success_tests.html");
  ASSERT_TRUE(RunPageTest(page_url.spec()));
  extensions::CloudPrintSetCredentialsFunction::SetTestMode(false);
}
#endif  // !defined(OS_CHROMEOS)
