// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_chrome_auth_private_api.h"

#include <string>
#include "base/values.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/printing/cloud_print/cloud_print_proxy_service.h"
#include "chrome/browser/printing/cloud_print/cloud_print_proxy_service_factory.h"
#include "chrome/browser/profiles/profile.h"

namespace {

bool test_mode = false;

}  // namespace

SetCloudPrintCredentialsFunction::SetCloudPrintCredentialsFunction() {
}

SetCloudPrintCredentialsFunction::~SetCloudPrintCredentialsFunction() {
}

bool SetCloudPrintCredentialsFunction::RunImpl() {
  std::string user_email;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(0, &user_email));
  std::string robot_email;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(1, &robot_email));
  std::string credentials;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(2, &credentials));
  if (test_mode) {
    std::string test_response = user_email;
    test_response.append(robot_email);
    test_response.append(credentials);
    result_.reset(Value::CreateStringValue(test_response));
  } else {
    CloudPrintProxyServiceFactory::GetForProfile(profile_)->
        EnableForUserWithRobot(credentials, robot_email, user_email);
  }
  SendResponse(true);
  return true;
}

// static
void SetCloudPrintCredentialsFunction::SetTestMode(bool test_mode_enabled) {
  test_mode = test_mode_enabled;
}
