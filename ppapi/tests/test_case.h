// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PPAPI_TESTS_TEST_CASE_H_
#define PPAPI_TESTS_TEST_CASE_H_

#include <cmath>
#include <limits>
#include <string>

#include "ppapi/c/pp_resource.h"
#include "ppapi/c/dev/ppb_testing_dev.h"
#include "ppapi/cpp/dev/scrollbar_dev.h"
#include "ppapi/cpp/view.h"

#if (defined __native_client__)
#include "ppapi/cpp/var.h"
#else
#include "ppapi/cpp/private/var_private.h"
#endif

class TestingInstance;

namespace pp {
namespace deprecated {
class ScriptableObject;
}
}

// Individual classes of tests derive from this generic test case.
class TestCase {
 public:
  explicit TestCase(TestingInstance* instance);
  virtual ~TestCase();

  // Optionally override to do testcase specific initialization.
  // Default implementation just returns true.
  virtual bool Init();

  // Override to implement the test case. It will be called after the plugin is
  // first displayed, passing a string. If the string is empty, the
  // should run all tests for this test case. Otherwise, it should run the test
  // whose name matches test_filter exactly (if there is one). This should
  // generally be implemented using the RUN_TEST* macros.
  virtual void RunTests(const std::string& test_filter) = 0;

  static std::string MakeFailureMessage(const char* file, int line,
                                        const char* cmd);

#if !(defined __native_client__)
  // Returns the scriptable test object for the current test, if any.
  // Internally, this uses CreateTestObject which each test overrides.
  pp::VarPrivate GetTestObject();
#endif

  // A function that is invoked whenever HandleMessage is called on the
  // associated TestingInstance. Default implementation does nothing.  TestCases
  // that want to handle incoming postMessage events should override this
  // method.
  virtual void HandleMessage(const pp::Var& message_data);

  // A function that is invoked whenever DidChangeView is called on the
  // associated TestingInstance. Default implementation does nothing. TestCases
  // that want to handle view changes should override this method.
  virtual void DidChangeView(const pp::View& view);

  // A function that is invoked whenever HandleInputEvent is called on the
  // associated TestingInstance. Default implementation returns false. TestCases
  // that want to handle view changes should override this method.
  virtual bool HandleInputEvent(const pp::InputEvent& event);

 protected:
#if !(defined __native_client__)
  // Overridden by each test to supply a ScriptableObject corresponding to the
  // test. There can only be one object created for all test in a given class
  // so be sure your object is designed to be re-used.
  //
  // This object should be created on the heap. Ownership will be passed to the
  // caller. Return NULL if there is no supported test object (the default).
  virtual pp::deprecated::ScriptableObject* CreateTestObject();
#endif

  // Checks whether the testing interface is available. Returns true if it is,
  // false otherwise. If it is not available, adds a descriptive error. This is
  // for use by tests that require the testing interface.
  bool CheckTestingInterface();

  // Makes sure the test is run over HTTP.
  bool EnsureRunningOverHTTP();

  // Return true if the given test name matches the filter. This is true if
  // (a) filter is empty or (b) test_name and filter match exactly.
  bool MatchesFilter(const std::string& test_name, const std::string& filter);

  // Check for leaked resources and vars at the end of the test. If any exist,
  // return a string with some information about the error. Otherwise, return
  // an empty string.
  std::string CheckResourcesAndVars();

  // Pointer to the instance that owns us.
  TestingInstance* instance_;

  // NULL unless InitTestingInterface is called.
  const PPB_Testing_Dev* testing_interface_;

  // Force asynchronous completion of any operation taking a callback.
  bool force_async_;

 private:
#if !(defined __native_client__)
  // Holds the test object, if any was retrieved from CreateTestObject.
  pp::VarPrivate test_object_;
#endif
};

// This class is an implementation detail.
class TestCaseFactory {
 public:
  typedef TestCase* (*Method)(TestingInstance* instance);

  TestCaseFactory(const char* name, Method method)
      : next_(head_),
        name_(name),
        method_(method) {
    head_ = this;
  }

 private:
  friend class TestingInstance;

  TestCaseFactory* next_;
  const char* name_;
  Method method_;

  static TestCaseFactory* head_;
};

// Use the REGISTER_TEST_CASE macro in your TestCase implementation file to
// register your TestCase.  If your test is named TestFoo, then add the
// following to test_foo.cc:
//
//   REGISTER_TEST_CASE(Foo);
//
// This will cause your test to be included in the set of known tests.
//
#define REGISTER_TEST_CASE(name)                                            \
  static TestCase* Test##name##_FactoryMethod(TestingInstance* instance) {  \
    return new Test##name(instance);                                        \
  }                                                                         \
  static TestCaseFactory g_Test##name_factory(                              \
    #name, &Test##name##_FactoryMethod                                      \
  )

// Helper macro for calling functions implementing specific tests in the
// RunTest function. This assumes the function name is TestFoo where Foo is the
// test |name|.
#define RUN_TEST(name, test_filter) \
  if (MatchesFilter(#name, test_filter)) { \
    force_async_ = false; \
    std::string error_message = Test##name(); \
    if (error_message.empty()) \
      error_message = CheckResourcesAndVars(); \
    instance_->LogTest(#name, error_message); \
  }

#define RUN_TEST_WITH_REFERENCE_CHECK(name, test_filter) \
  if (MatchesFilter(#name, test_filter)) { \
    force_async_ = false; \
    uint32_t objects = testing_interface_->GetLiveObjectsForInstance( \
        instance_->pp_instance()); \
    std::string error_message = Test##name(); \
    if (error_message.empty() && \
        testing_interface_->GetLiveObjectsForInstance( \
            instance_->pp_instance()) != objects) \
      error_message = MakeFailureMessage(__FILE__, __LINE__, \
          "reference leak check"); \
    instance_->LogTest(#name, error_message); \
  }

// Like RUN_TEST above but forces functions taking callbacks to complete
// asynchronously on success or error.
#define RUN_TEST_FORCEASYNC(name, test_filter) \
  if (MatchesFilter(#name"ForceAsync", test_filter)) { \
    force_async_ = true; \
    instance_->LogTest(#name"ForceAsync", Test##name()); \
  }

#define RUN_TEST_FORCEASYNC_AND_NOT(name, test_filter) \
  do { \
    RUN_TEST_FORCEASYNC(name, test_filter); \
    RUN_TEST(name, test_filter); \
  } while (false)


// Helper macros for checking values in tests, and returning a location
// description of the test fails.
#define ASSERT_TRUE(cmd) \
  if (!(cmd)) { \
    return MakeFailureMessage(__FILE__, __LINE__, #cmd); \
  }
#define ASSERT_FALSE(cmd) ASSERT_TRUE(!(cmd))
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))
#define ASSERT_NE(a, b) ASSERT_TRUE((a) != (b))

#define ASSERT_DOUBLE_EQ(a, b) ASSERT_TRUE( \
    std::fabs((a)-(b)) <= std::numeric_limits<double>::epsilon())

#define PASS() return std::string()

#endif  // PPAPI_TESTS_TEST_CASE_H_
