// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "base/file_path.h"
#include "base/path_service.h"
#include "base/string_number_conversions.h"
#include "base/utf_string_conversions.h"
#include "net/http/http_cache.h"
#include "net/url_request/url_request_context.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "webkit/glue/webkit_glue.h"
#include "webkit/tools/test_shell/simple_resource_loader_bridge.h"
#include "webkit/tools/test_shell/test_shell.h"
#include "webkit/tools/test_shell/test_shell_request_context.h"
#include "webkit/tools/test_shell/test_shell_switches.h"
#include "webkit/tools/test_shell/test_shell_test.h"

namespace {

const char kTestUrlSwitch[] = "test-url";

// A test to help determine if any nodes have been leaked as a result of
// visiting a given URL.  If enabled in WebCore, the number of leaked nodes
// can be printed upon termination.  This is only enabled in debug builds, so
// it only makes sense to run this using a debug build.
//
// It will load a URL, visit about:blank, and then perform garbage collection.
// The number of remaining (potentially leaked) nodes will be printed on exit.
class NodeLeakTest : public TestShellTest {
 public:
  virtual void SetUp() {
    const CommandLine& parsed_command_line = *CommandLine::ForCurrentProcess();

    FilePath cache_path =
        parsed_command_line.GetSwitchValuePath(test_shell::kCacheDir);
    if (cache_path.empty()) {
      PathService::Get(base::DIR_EXE, &cache_path);
      cache_path = cache_path.AppendASCII("cache");
    }

    if (parsed_command_line.HasSwitch(test_shell::kTestShellTimeOut)) {
      const std::string timeout_str = parsed_command_line.GetSwitchValueASCII(
          test_shell::kTestShellTimeOut);
      int timeout_ms;
      if (base::StringToInt(timeout_str, &timeout_ms) && timeout_ms > 0)
        TestShell::SetFileTestTimeout(timeout_ms);
    }

    // Optionally use playback mode (for instance if running automated tests).
    net::HttpCache::Mode mode =
        parsed_command_line.HasSwitch(test_shell::kPlaybackMode) ?
        net::HttpCache::PLAYBACK : net::HttpCache::NORMAL;
    SimpleResourceLoaderBridge::Init(cache_path, mode, false);

    TestShellTest::SetUp();
  }

  virtual void TearDown() {
    TestShellTest::TearDown();

    SimpleResourceLoaderBridge::Shutdown();
  }

  void NavigateToURL(const std::string& test_url) {
    test_shell_->LoadURL(GURL(test_url));
    test_shell_->WaitTestFinished();

    // Depends on TestShellTests::TearDown to load blank page and
    // the TestShell destructor to call garbage collection.
  }
};

TEST_F(NodeLeakTest, TestURL) {
  const CommandLine& parsed_command_line = *CommandLine::ForCurrentProcess();
  if (parsed_command_line.HasSwitch(kTestUrlSwitch))
    NavigateToURL(parsed_command_line.GetSwitchValueASCII(kTestUrlSwitch));
}

}  // namespace
