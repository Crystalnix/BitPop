// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/tests/test_flash.h"

#include "ppapi/c/pp_macros.h"
#include "ppapi/c/private/ppb_flash.h"
#include "ppapi/cpp/instance.h"
#include "ppapi/cpp/module.h"
#include "ppapi/cpp/var.h"
#include "ppapi/tests/testing_instance.h"

REGISTER_TEST_CASE(Flash);

using pp::Var;

TestFlash::TestFlash(TestingInstance* instance)
    : TestCase(instance),
      PP_ALLOW_THIS_IN_INITIALIZER_LIST(callback_factory_(this)) {
}

bool TestFlash::Init() {
  flash_interface_ = static_cast<const PPB_Flash*>(
      pp::Module::Get()->GetBrowserInterface(PPB_FLASH_INTERFACE));
  return !!flash_interface_;
}

void TestFlash::RunTests(const std::string& filter) {
  RUN_TEST(SetInstanceAlwaysOnTop, filter);
  RUN_TEST(GetProxyForURL, filter);
  RUN_TEST(MessageLoop, filter);
  RUN_TEST(GetLocalTimeZoneOffset, filter);
  RUN_TEST(GetCommandLineArgs, filter);
  RUN_TEST(GetDeviceID, filter);
  RUN_TEST(GetSettingInt, filter);
  RUN_TEST(GetSetting, filter);
  RUN_TEST(SetCrashData, filter);
}

std::string TestFlash::TestSetInstanceAlwaysOnTop() {
  flash_interface_->SetInstanceAlwaysOnTop(instance_->pp_instance(), PP_TRUE);
  flash_interface_->SetInstanceAlwaysOnTop(instance_->pp_instance(), PP_FALSE);
  PASS();
}

std::string TestFlash::TestGetProxyForURL() {
  Var result(pp::PASS_REF,
             flash_interface_->GetProxyForURL(instance_->pp_instance(),
                                              "http://127.0.0.1/foobar/"));
  ASSERT_TRUE(result.is_string());
  // Assume no one configures a proxy for localhost.
  ASSERT_EQ("DIRECT", result.AsString());

  result = Var(pp::PASS_REF,
               flash_interface_->GetProxyForURL(instance_->pp_instance(),
                                                "http://www.google.com"));
  // Don't know what the proxy might be, but it should be a valid result.
  ASSERT_TRUE(result.is_string());

  result = Var(pp::PASS_REF,
               flash_interface_->GetProxyForURL(instance_->pp_instance(),
                                                "file:///tmp"));
  ASSERT_TRUE(result.is_string());
  // Should get "DIRECT" for file:// URLs.
  ASSERT_EQ("DIRECT", result.AsString());

  result = Var(pp::PASS_REF,
               flash_interface_->GetProxyForURL(instance_->pp_instance(),
                                                "this_isnt_an_url"));
  // Should be an error.
  ASSERT_TRUE(result.is_undefined());

  PASS();
}

std::string TestFlash::TestMessageLoop() {
  pp::CompletionCallback callback =
      callback_factory_.NewCallback(&TestFlash::QuitMessageLoopTask);
  pp::Module::Get()->core()->CallOnMainThread(0, callback);
  flash_interface_->RunMessageLoop(instance_->pp_instance());

  PASS();
}

std::string TestFlash::TestGetLocalTimeZoneOffset() {
  double result = flash_interface_->GetLocalTimeZoneOffset(
      instance_->pp_instance(), 1321491298.0);
  // The result depends on the local time zone, but +/- 14h from UTC should
  // cover the possibilities.
  ASSERT_TRUE(result >= -14 * 60 * 60);
  ASSERT_TRUE(result <= 14 * 60 * 60);

  PASS();
}

std::string TestFlash::TestGetCommandLineArgs() {
  Var result(pp::PASS_REF,
             flash_interface_->GetCommandLineArgs(
                 pp::Module::Get()->pp_module()));
  ASSERT_TRUE(result.is_string());

  PASS();
}

std::string TestFlash::TestGetDeviceID() {
  Var result(pp::PASS_REF,
             flash_interface_->GetDeviceID(instance_->pp_instance()));
  // TODO(wad) figure out how to mock the input and test the full flow.
  ASSERT_TRUE(result.is_string());
  PASS();
}

std::string TestFlash::TestGetSettingInt() {
  // This only works out of process.
  if (testing_interface_->IsOutOfProcess()) {
    int32_t is_3denabled = flash_interface_->GetSettingInt(
        instance_->pp_instance(), PP_FLASHSETTING_3DENABLED);
    ASSERT_TRUE(is_3denabled == 0 || is_3denabled == 1);

    int32_t is_incognito = flash_interface_->GetSettingInt(
        instance_->pp_instance(), PP_FLASHSETTING_INCOGNITO);
    ASSERT_TRUE(is_incognito == 0 || is_incognito == 1);

    int32_t is_stage3denabled = flash_interface_->GetSettingInt(
        instance_->pp_instance(), PP_FLASHSETTING_STAGE3DENABLED);
    // This may "fail" if 3d isn't enabled.
    ASSERT_TRUE((is_stage3denabled == 0 || is_stage3denabled == 1) ||
                (is_stage3denabled == -1 && is_3denabled == 0));
  }

  // Invalid instance cases.
  int32_t result = flash_interface_->GetSettingInt(
      0, PP_FLASHSETTING_3DENABLED);
  ASSERT_EQ(-1, result);
  result = flash_interface_->GetSettingInt(0, PP_FLASHSETTING_INCOGNITO);
  ASSERT_EQ(-1, result);
  result = flash_interface_->GetSettingInt(0, PP_FLASHSETTING_STAGE3DENABLED);
  ASSERT_EQ(-1, result);

  PASS();
}

std::string TestFlash::TestGetSetting() {
  // This only works out of process.
  if (testing_interface_->IsOutOfProcess()) {
    Var is_3denabled(pp::PASS_REF, flash_interface_->GetSetting(
        instance_->pp_instance(), PP_FLASHSETTING_3DENABLED));
    ASSERT_TRUE(is_3denabled.is_bool());

    Var is_incognito(pp::PASS_REF, flash_interface_->GetSetting(
        instance_->pp_instance(), PP_FLASHSETTING_INCOGNITO));
    ASSERT_TRUE(is_incognito.is_bool());

    Var is_stage3denabled(pp::PASS_REF, flash_interface_->GetSetting(
        instance_->pp_instance(), PP_FLASHSETTING_STAGE3DENABLED));
    // This may "fail" if 3d isn't enabled.
    ASSERT_TRUE(is_stage3denabled.is_bool() ||
                (is_stage3denabled.is_undefined() && !is_3denabled.AsBool()));

    Var num_cores(pp::PASS_REF, flash_interface_->GetSetting(
        instance_->pp_instance(), PP_FLASHSETTING_NUMCORES));
    ASSERT_TRUE(num_cores.is_int() && num_cores.AsInt() > 0);
  }

  // Invalid instance cases.
  Var result(pp::PASS_REF,
             flash_interface_->GetSetting(0, PP_FLASHSETTING_3DENABLED));
  ASSERT_TRUE(result.is_undefined());
  result = Var(pp::PASS_REF,
               flash_interface_->GetSetting(0, PP_FLASHSETTING_INCOGNITO));
  ASSERT_TRUE(result.is_undefined());
  result = Var(pp::PASS_REF,
               flash_interface_->GetSetting(0, PP_FLASHSETTING_STAGE3DENABLED));
  ASSERT_TRUE(result.is_undefined());

  PASS();
}

std::string TestFlash::TestSetCrashData() {
  pp::Var url("http://...");
  ASSERT_TRUE(flash_interface_->SetCrashData(instance_->pp_instance(),
                                             PP_FLASHCRASHKEY_URL,
                                             url.pp_var()));

  PASS();
}

void TestFlash::QuitMessageLoopTask(int32_t) {
  flash_interface_->QuitMessageLoop(instance_->pp_instance());
}
