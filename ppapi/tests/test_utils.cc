// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/tests/test_utils.h"

#include <stdio.h>
#include <stdlib.h>
#if defined(_MSC_VER)
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "ppapi/c/pp_errors.h"
#include "ppapi/cpp/module.h"
#include "ppapi/cpp/var.h"

const int kActionTimeoutMs = 10000;

const PPB_Testing_Dev* GetTestingInterface() {
  static const PPB_Testing_Dev* g_testing_interface =
      static_cast<const PPB_Testing_Dev*>(
          pp::Module::Get()->GetBrowserInterface(PPB_TESTING_DEV_INTERFACE));
  return g_testing_interface;
}

std::string ReportError(const char* method, int32_t error) {
  char error_as_string[12];
  sprintf(error_as_string, "%d", static_cast<int>(error));
  std::string result = method + std::string(" failed with error: ") +
      error_as_string;
  return result;
}

void PlatformSleep(int duration_ms) {
#if defined(_MSC_VER)
  ::Sleep(duration_ms);
#else
  usleep(duration_ms * 1000);
#endif
}

bool GetLocalHostPort(PP_Instance instance, std::string* host, uint16_t* port) {
  if (!host || !port)
    return false;

  const PPB_Testing_Dev* testing = GetTestingInterface();
  if (!testing)
    return false;

  PP_URLComponents_Dev components;
  pp::Var pp_url(pp::Var::PassRef(),
                 testing->GetDocumentURL(instance, &components));
  if (!pp_url.is_string())
    return false;
  std::string url = pp_url.AsString();

  if (components.host.len < 0)
    return false;
  host->assign(url.substr(components.host.begin, components.host.len));

  if (components.port.len <= 0)
    return false;

  int i = atoi(url.substr(components.port.begin, components.port.len).c_str());
  if (i < 0 || i > 65535)
    return false;
  *port = static_cast<uint16_t>(i);

  return true;
}

TestCompletionCallback::TestCompletionCallback(PP_Instance instance)
    : have_result_(false),
      result_(PP_OK_COMPLETIONPENDING),
      force_async_(false),
      post_quit_task_(false),
      run_count_(0),
      instance_(instance) {
}

TestCompletionCallback::TestCompletionCallback(PP_Instance instance,
                                               bool force_async)
    : have_result_(false),
      result_(PP_OK_COMPLETIONPENDING),
      force_async_(force_async),
      post_quit_task_(false),
      run_count_(0),
      instance_(instance) {
}

int32_t TestCompletionCallback::WaitForResult() {
  if (!have_result_) {
    result_ = PP_OK_COMPLETIONPENDING;  // Reset
    post_quit_task_ = true;
    GetTestingInterface()->RunMessageLoop(instance_);
  }
  have_result_ = false;
  return result_;
}

TestCompletionCallback::operator pp::CompletionCallback() const {
  int32_t flags = (force_async_ ? 0 : PP_COMPLETIONCALLBACK_FLAG_OPTIONAL);
  return pp::CompletionCallback(&TestCompletionCallback::Handler,
                                const_cast<TestCompletionCallback*>(this),
                                flags);
}

// static
void TestCompletionCallback::Handler(void* user_data, int32_t result) {
  TestCompletionCallback* callback =
      static_cast<TestCompletionCallback*>(user_data);
  callback->result_ = result;
  callback->have_result_ = true;
  callback->run_count_++;
  if (callback->post_quit_task_) {
    callback->post_quit_task_ = false;
    GetTestingInterface()->QuitMessageLoop(callback->instance_);
  }
}
