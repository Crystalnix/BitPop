// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/webdriver/commands/create_session.h"

#include <sstream>
#include <string>

#include "base/file_path.h"
#include "base/stringprintf.h"
#include "base/values.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/test/webdriver/commands/response.h"
#include "chrome/test/webdriver/session.h"
#include "chrome/test/webdriver/session_manager.h"

namespace webdriver {

// The minimum supported version of Chrome for this version of ChromeDriver.
const int kMinSupportedChromeVersion = 12;

CreateSession::CreateSession(const std::vector<std::string>& path_segments,
                             const DictionaryValue* const parameters)
    : Command(path_segments, parameters) {}

CreateSession::~CreateSession() {}

bool CreateSession::DoesPost() { return true; }

void CreateSession::ExecutePost(Response* const response) {
  SessionManager* session_manager = SessionManager::GetInstance();

  // Session manages its own liftime, so do not call delete.
  Session* session = new Session();
  ErrorCode code = session->Init(session_manager->chrome_dir());

  if (code == kBrowserCouldNotBeFound) {
    SET_WEBDRIVER_ERROR(response,
                        "Chrome could not be found.",
                        kUnknownError);
    return;
  } else if (code == kBrowserFailedToStart) {
    std::string error_msg = base::StringPrintf(
        "Chrome could not be started successfully. "
            "Please update ChromeDriver and ensure you are using Chrome %d+.",
        kMinSupportedChromeVersion);
    SET_WEBDRIVER_ERROR(response, error_msg, kUnknownError);
    return;
  } else if (code == kIncompatibleBrowserVersion) {
    std::string error_msg = base::StringPrintf(
        "Version of Chrome is incompatible with version of ChromeDriver. "
            "Please update ChromeDriver and ensure you are using Chrome %d+.",
        kMinSupportedChromeVersion);
    SET_WEBDRIVER_ERROR(response, error_msg, kUnknownError);
    return;
  } else if (code != kSuccess) {
    std::string error_msg = base::StringPrintf(
        "Unknown error while initializing session. "
            "Ensure ChromeDriver is up-to-date and Chrome is version %d+.",
        kMinSupportedChromeVersion);
    SET_WEBDRIVER_ERROR(response, error_msg, kUnknownError);
    return;
  }

  DictionaryValue* capabilities = NULL;
  bool native_events_required = false;
  if (GetDictionaryParameter("desiredCapabilities", &capabilities)) {
   capabilities->GetBoolean("chrome.nativeEvents", &native_events_required);
   session->set_use_native_events(native_events_required);
  }

  bool screenshot_on_error = false;
  if (GetDictionaryParameter("desiredCapabilities", &capabilities)) {
    capabilities->GetBoolean("takeScreenshotOnError", &screenshot_on_error);
    session->set_screenshot_on_error(screenshot_on_error);
  }

  VLOG(1) << "Created session " << session->id();
  std::ostringstream stream;
  stream << "http://" << session_manager->GetAddress() << "/session/"
         << session->id();
  response->SetStatus(kSeeOther);
  response->SetValue(Value::CreateStringValue(stream.str()));
}

}  // namespace webdriver
