// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "base/utf_string_conversions.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test_utils.h"
#include "content/shell/shell.h"
#include "content/test/content_browser_test.h"
#include "content/test/content_browser_test_utils.h"
#include "net/test/test_server.h"

namespace content {

class WebrtcBrowserTest: public ContentBrowserTest {
 public:
  WebrtcBrowserTest() {}
  virtual ~WebrtcBrowserTest() {}

  virtual void SetUp() OVERRIDE {
    // We need fake devices in this test since we want to run on naked VMs. We
    // assume this switch is set by default in content_browsertests.
    ASSERT_TRUE(CommandLine::ForCurrentProcess()->HasSwitch(
        switches::kUseFakeDeviceForMediaStream));

    ASSERT_TRUE(test_server()->Start());
    ContentBrowserTest::SetUp();
  }
 protected:
  bool ExecuteJavascript(const std::string& javascript) {
    RenderViewHost* render_view_host =
        shell()->web_contents()->GetRenderViewHost();

    return ExecuteJavaScript(render_view_host, L"", ASCIIToWide(javascript));
  }

  void ExpectTitle(const std::string& expected_title) const {
    string16 expected_title16(ASCIIToUTF16(expected_title));
    TitleWatcher title_watcher(shell()->web_contents(), expected_title16);
    EXPECT_EQ(expected_title16, title_watcher.WaitAndGetTitle());
  }
};

// These tests will all make a getUserMedia call with different constraints and
// see that the success callback is called. If the error callback is called or
// none of the callbacks are called the tests will simply time out and fail.
IN_PROC_BROWSER_TEST_F(WebrtcBrowserTest, GetVideoStreamAndStop) {
  GURL url(test_server()->GetURL("files/media/getusermedia_and_stop.html"));
  NavigateToURL(shell(), url);

  EXPECT_TRUE(ExecuteJavascript("getUserMedia({video: true});"));

  ExpectTitle("OK");
}

IN_PROC_BROWSER_TEST_F(WebrtcBrowserTest, GetAudioAndVideoStreamAndStop) {
  GURL url(test_server()->GetURL("files/media/getusermedia_and_stop.html"));
  NavigateToURL(shell(), url);

  EXPECT_TRUE(ExecuteJavascript("getUserMedia({video: true, audio: true});"));

  ExpectTitle("OK");
}

// These tests will make a complete PeerConnection-based call and verify that
// video is playing for the call.
IN_PROC_BROWSER_TEST_F(WebrtcBrowserTest, CanSetupVideoCall) {
  GURL url(test_server()->GetURL("files/media/peerconnection-call.html"));
  NavigateToURL(shell(), url);

  EXPECT_TRUE(ExecuteJavascript("call({video: true});"));
  ExpectTitle("OK");
}

IN_PROC_BROWSER_TEST_F(WebrtcBrowserTest, CanSetupAudioAndVideoCall) {
  GURL url(test_server()->GetURL("files/media/peerconnection-call.html"));
  NavigateToURL(shell(), url);

  EXPECT_TRUE(ExecuteJavascript("call({video: true, audio: true});"));
  ExpectTitle("OK");
}

}  // namespace content

