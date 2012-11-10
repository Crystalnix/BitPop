// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/base_paths.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/path_service.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/time.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/common/automation_messages.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/automation/automation_proxy.h"
#include "chrome/test/automation/tab_proxy.h"
#include "chrome/test/pyautolib/pyautolib.h"
#include "googleurl/src/gurl.h"

static int64 StringToId(const std::wstring& str) {
  int64 id;
  base::StringToInt64(WideToUTF8(str), &id);
  return id;
}

// PyUITestSuiteBase
PyUITestSuiteBase::PyUITestSuiteBase(int argc, char** argv)
    : UITestSuite(argc, argv) {
}

PyUITestSuiteBase::~PyUITestSuiteBase() {
#if defined(OS_MACOSX)
  pool_.Recycle();
#endif
  Shutdown();
}

void PyUITestSuiteBase::InitializeWithPath(const FilePath& browser_dir) {
  SetBrowserDirectory(browser_dir);
  UITestSuite::Initialize();
}

void PyUITestSuiteBase::SetCrSourceRoot(const FilePath& path) {
  PathService::Override(base::DIR_SOURCE_ROOT, path);
}

// PyUITestBase
PyUITestBase::PyUITestBase(bool clear_profile, std::wstring homepage)
    : UITestBase() {
  set_clear_profile(clear_profile);
  set_homepage(WideToASCII(homepage));
  // We add this so that pyauto can execute javascript in the renderer and
  // read values back.
  dom_automation_enabled_ = true;
  message_loop_ = GetSharedMessageLoop(MessageLoop::TYPE_DEFAULT);
}

PyUITestBase::~PyUITestBase() {
}

// static, refer .h for why it needs to be static
MessageLoop* PyUITestBase::message_loop_ = NULL;

// static
MessageLoop* PyUITestBase::GetSharedMessageLoop(
  MessageLoop::Type msg_loop_type) {
  if (!message_loop_)   // Create a shared instance of MessageLoop
    message_loop_ = new MessageLoop(msg_loop_type);
  return message_loop_;
}

void PyUITestBase::Initialize(const FilePath& browser_dir) {
  UITestBase::SetBrowserDirectory(browser_dir);
}

ProxyLauncher* PyUITestBase::CreateProxyLauncher() {
  if (named_channel_id_.empty())
    return new AnonymousProxyLauncher(false);
  return new NamedProxyLauncher(named_channel_id_, false, false);
}

void PyUITestBase::SetUp() {
  UITestBase::SetUp();
}

void PyUITestBase::TearDown() {
  UITestBase::TearDown();
}

void PyUITestBase::SetLaunchSwitches() {
  // Clear the homepage because some of the pyauto tests don't work correctly
  // if a URL argument is passed.
  std::string homepage_original;
  std::swap(homepage_original, homepage_);

  UITestBase::SetLaunchSwitches();

  // However, we *do* want the --homepage switch.
  std::swap(homepage_original, homepage_);
  launch_arguments_.AppendSwitchASCII(switches::kHomePage, homepage_);
}

bool PyUITestBase::GetBookmarkBarState(bool* visible,
                                       bool* detached,
                                       int window_index) {
  scoped_refptr<BrowserProxy> browser_proxy = GetBrowserWindow(window_index);
  EXPECT_TRUE(browser_proxy.get());
  if (!browser_proxy.get())
    return false;

  // We have no use for animating in this context.
  bool animating;
  EXPECT_TRUE(browser_proxy->GetBookmarkBarVisibility(visible,
                                                      &animating,
                                                      detached));
  return true;
}

bool PyUITestBase::GetBookmarkBarVisibility() {
  // We have no use for detached in this context.
  bool visible, detached;
  if (!GetBookmarkBarState(&visible, &detached))
    return false;
  return visible;
}

bool PyUITestBase::IsBookmarkBarDetached() {
  // We have no use for visible in this context.
  bool visible, detached;
  if (!GetBookmarkBarState(&visible, &detached))
    return false;
  return detached;
}

bool PyUITestBase::WaitForBookmarkBarVisibilityChange(bool wait_for_open,
                                                      int window_index) {
  scoped_refptr<BrowserProxy> browser_proxy = GetBrowserWindow(window_index);
  EXPECT_TRUE(browser_proxy.get());
  if (!browser_proxy.get())
    return false;

  // This has a 20sec timeout.  If that's not enough we have serious problems.
  bool completed = UITestBase::WaitForBookmarkBarVisibilityChange(
      browser_proxy.get(),
      wait_for_open);
  EXPECT_TRUE(completed);
  return completed;
}

std::string PyUITestBase::_GetBookmarksAsJSON(int window_index) {
  scoped_refptr<BrowserProxy> browser_proxy = GetBrowserWindow(window_index);
  EXPECT_TRUE(browser_proxy.get());
  if (!browser_proxy.get())
    return std::string();

  std::string s;
  EXPECT_TRUE(browser_proxy->GetBookmarksAsJSON(&s));
  return s;
}

bool PyUITestBase::AddBookmarkGroup(std::wstring& parent_id,
                                    int index,
                                    std::wstring& title,
                                    int window_index) {
  scoped_refptr<BrowserProxy> browser_proxy = GetBrowserWindow(window_index);
  EXPECT_TRUE(browser_proxy.get());
  if (!browser_proxy.get())
    return false;

  return browser_proxy->AddBookmarkGroup(StringToId(parent_id), index, title);
}

bool PyUITestBase::AddBookmarkURL(std::wstring& parent_id,
                                  int index,
                                  std::wstring& title,
                                  std::wstring& url,
                                  int window_index) {
  scoped_refptr<BrowserProxy> browser_proxy = GetBrowserWindow(window_index);
  EXPECT_TRUE(browser_proxy.get());
  if (!browser_proxy.get())
    return false;

  return browser_proxy->AddBookmarkURL(StringToId(parent_id),
                                       index, title,
                                       GURL(WideToUTF8(url)));
}

bool PyUITestBase::ReparentBookmark(std::wstring& id,
                                    std::wstring& new_parent_id,
                                    int index,
                                    int window_index) {
  scoped_refptr<BrowserProxy> browser_proxy = GetBrowserWindow(window_index);
  EXPECT_TRUE(browser_proxy.get());
  if (!browser_proxy.get())
    return false;

  return browser_proxy->ReparentBookmark(StringToId(id),
                                         StringToId(new_parent_id),
                                         index);
}

bool PyUITestBase::SetBookmarkTitle(std::wstring& id,
                                    std::wstring& title,
                                    int window_index) {
  scoped_refptr<BrowserProxy> browser_proxy = GetBrowserWindow(window_index);
  EXPECT_TRUE(browser_proxy.get());
  if (!browser_proxy.get())
    return false;

  return browser_proxy->SetBookmarkTitle(StringToId(id), title);
}

bool PyUITestBase::SetBookmarkURL(std::wstring& id,
                                  std::wstring& url,
                                  int window_index) {
  scoped_refptr<BrowserProxy> browser_proxy = GetBrowserWindow(window_index);
  EXPECT_TRUE(browser_proxy.get());
  if (!browser_proxy.get())
    return false;

  return browser_proxy->SetBookmarkURL(StringToId(id), GURL(WideToUTF8(url)));
}

bool PyUITestBase::RemoveBookmark(std::wstring& id, int window_index) {
  scoped_refptr<BrowserProxy> browser_proxy = GetBrowserWindow(window_index);
  EXPECT_TRUE(browser_proxy.get());
  if (!browser_proxy.get())
    return false;

  return browser_proxy->RemoveBookmark(StringToId(id));
}

AutomationProxy* PyUITestBase::automation() const {
  AutomationProxy* automation_proxy = UITestBase::automation();
  if (!automation_proxy) {
    LOG(FATAL) << "The automation proxy is NULL.";
  }
  return automation_proxy;
}


scoped_refptr<BrowserProxy> PyUITestBase::GetBrowserWindow(int window_index) {
  return automation()->GetBrowserWindow(window_index);
}

std::string PyUITestBase::_SendJSONRequest(int window_index,
                                           const std::string& request,
                                           int timeout) {
  std::string response;
  bool success;
  AutomationMessageSender* automation_sender = automation();
  base::TimeTicks time = base::TimeTicks::Now();

  if (!automation_sender) {
    ErrorResponse("The automation proxy does not exist", request, &response);
  } else if (!automation_sender->Send(
      new AutomationMsg_SendJSONRequest(window_index, request, &response,
                                        &success),
      timeout)) {
    RequestFailureResponse(request, base::TimeTicks::Now() - time,
                           base::TimeDelta::FromMilliseconds(timeout),
                           &response);
  }
  return response;
}

void PyUITestBase::ErrorResponse(
    const std::string& error_string,
    const std::string& request,
    std::string* response) {
  base::DictionaryValue error_dict;
  std::string error_msg = StringPrintf("%s for %s", error_string.c_str(),
                                       request.c_str());
  LOG(ERROR) << "Error during automation: " << error_msg;
  error_dict.SetString("error", error_msg);
  base::JSONWriter::Write(&error_dict, response);
}

void PyUITestBase::RequestFailureResponse(
    const std::string& request,
    const base::TimeDelta& duration,
    const base::TimeDelta& timeout,
    std::string* response) {
  // TODO(craigdh): Determine timeout directly from IPC's Send().
  if (duration >= timeout) {
    ErrorResponse(
        StringPrintf("Request timed out after %d seconds",
                     static_cast<int>(duration.InSeconds())),
        request, response);
  } else {
    // TODO(craigdh): Determine specific cause.
    ErrorResponse("Chrome failed to respond", request, response);
  }
}
