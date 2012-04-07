// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PPAPI_TESTS_TESTING_INSTANCE_H_
#define PPAPI_TESTS_TESTING_INSTANCE_H_

#include <string>

#include "ppapi/utility/completion_callback_factory.h"

#if defined(__native_client__)
#include "ppapi/cpp/instance.h"
#else
#include "ppapi/cpp/private/instance_private.h"
#endif

class TestCase;

// How signaling works:
//
// We want to signal to the Chrome UI test harness
// (chrome/test/ui/ppapi_uitest.cc) that we're making progress and when we're
// done. The easiest thing in the UI test infrastructure is to wait for a
// cookie to become nonempty. We don't want to have a big wait for all tests in
// a TestCase since they can take a while and it might timeout.  So we set a
// series of cookies with an incrementing number in the name.
//
// If the value of the cookie is "..." then that tells the test runner that
// the test is progressing. It then waits for the next numbered cookie until
// it either times out or the value is something other than "...". In this
// case, the value will be either "PASS" or "FAIL [optional message]"
// corresponding to the outcome of the entire test case. Timeout will be
// treated just like a failure of the entire test case and the test will be
// terminated.
//
// In trusted builds, we use InstancePrivate and allow tests that use
// synchronous scripting. NaCl does not support synchronous scripting.
class TestingInstance : public
#if defined(__native_client__)
pp::Instance {
#else
pp::InstancePrivate {
#endif
 public:
  explicit TestingInstance(PP_Instance instance);
  virtual ~TestingInstance();

  // pp::Instance override.
  virtual bool Init(uint32_t argc, const char* argn[], const char* argv[]);
  virtual void DidChangeView(const pp::View& view);
  virtual bool HandleInputEvent(const pp::InputEvent& event);

#if !(defined __native_client__)
  virtual pp::Var GetInstanceObject();
#endif

  // Outputs the information from one test run, using the format
  //   <test_name> [PASS|FAIL <error_message>]
  // If error_message is empty, we say the test passed and emit PASS. If
  // error_message is nonempty, the test failed with that message as the error
  // string.
  //
  // Intended usage:
  //   LogTest("Foo", FooTest());
  //
  // Where FooTest is defined as:
  //   std::string FooTest() {
  //     if (something_horrible_happened)
  //       return "Something horrible happened";
  //     return "";
  //   }
  void LogTest(const std::string& test_name, const std::string& error_message);

  // Appends an error message to the log.
  void AppendError(const std::string& message);

  // Passes the message_data through to the HandleMessage method on the
  // TestClass object that's associated with this instance.
  virtual void HandleMessage(const pp::Var& message_data);

  const std::string& protocol() {
    return protocol_;
  }

  // Posts a message to the test page to eval() the script.
  void EvalScript(const std::string& script);

  // Sets the given cookie in the current document.
  void SetCookie(const std::string& name, const std::string& value);

 private:
  void ExecuteTests(int32_t unused);

  // Creates a new TestCase for the give test name, or NULL if there is no such
  // test. Ownership is passed to the caller. The given string is split by '_'.
  // The test case name is the first part.
  TestCase* CaseForTestName(const std::string& name);
  // Returns the filter (second part) of the given string. If there is no '_',
  // returns the empty string, which means 'run all tests for this test case'.
  // E.g.:
  //  http://testserver/test_case.html?testcase=PostMessage
  // Otherwise, the part of the testcase after '_' is returned, and the test
  // whose name matches that string (if any) will be run:
  //  http://testserver/test_case.html?testcase=PostMessage_SendingData
  // Runs 'PostMessage_SendingData.
  std::string FilterForTestName(const std::string& name);

  // Appends a list of available tests to the console in the document.
  void LogAvailableTests();

  // Appends the given error test to the console in the document.
  void LogError(const std::string& text);

  // Appends the given HTML string to the console in the document.
  void LogHTML(const std::string& html);

  void ReportProgress(const std::string& progress_value);

  pp::CompletionCallbackFactory<TestingInstance> callback_factory_;

  // Owning pointer to the current test case. Valid after Init has been called.
  TestCase* current_case_;

  // A filter to use when running tests. This is passed to 'RunTests', which
  // runs only tests whose name contains test_filter_ as a substring.
  std::string test_filter_;

  // The current step we're on starting at 0. This is incremented every time we
  // report progress via a cookie. See comment above the class.
  int progress_cookie_number_;

  // Set once the tests are run so we know not to re-run when the view is sized.
  bool executed_tests_;

  // Collects all errors to send the the browser. Empty indicates no error yet.
  std::string errors_;

  // True if running in Native Client.
  bool nacl_mode_;

  // String representing the protocol.  Used for detecting whether we're running
  // with http.
  std::string protocol_;
};

#endif  // PPAPI_TESTS_TESTING_INSTANCE_H_
