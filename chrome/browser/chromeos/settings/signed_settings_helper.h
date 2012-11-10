// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_SETTINGS_SIGNED_SETTINGS_HELPER_H_
#define CHROME_BROWSER_CHROMEOS_SETTINGS_SIGNED_SETTINGS_HELPER_H_

#include "chrome/browser/chromeos/settings/signed_settings.h"

namespace enterprise_management {
class PolicyFetchResponse;
}  // namespace enterprise_management

namespace chromeos {

// Helper to serialize signed settings ops, provide unified callback interface,
// and handle callbacks destruction before ops completion.
class SignedSettingsHelper {
 public:
  typedef base::Callback<void(SignedSettings::ReturnCode)> StorePolicyCallback;
  typedef
      base::Callback<void(SignedSettings::ReturnCode,
                          const enterprise_management::PolicyFetchResponse&)>
      RetrievePolicyCallback;

  // Class factory
  static SignedSettingsHelper* Get();

  // Functions to start signed settings ops.
  virtual void StartStorePolicyOp(
      const enterprise_management::PolicyFetchResponse& policy,
      StorePolicyCallback callback) = 0;
  virtual void StartRetrievePolicyOp(
      RetrievePolicyCallback callback) = 0;

  class TestDelegate {
   public:
    virtual void OnOpCreated(SignedSettings* op) = 0;
    virtual void OnOpStarted(SignedSettings* op) = 0;
    virtual void OnOpCompleted(SignedSettings* op) = 0;
  };

#if defined(UNIT_TEST)
  void set_test_delegate(TestDelegate* test_delegate) {
    test_delegate_ = test_delegate;
  }
#endif  // defined(UNIT_TEST)

 protected:
  SignedSettingsHelper() : test_delegate_(NULL) {
  }

  TestDelegate* test_delegate_;
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_SETTINGS_SIGNED_SETTINGS_HELPER_H_
