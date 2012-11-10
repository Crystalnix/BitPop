// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/cloud_print_private/cloud_print_private_api.h"

#include <string>
#include "base/values.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/printing/cloud_print/cloud_print_proxy_service.h"
#include "chrome/browser/printing/cloud_print/cloud_print_proxy_service_factory.h"
#include "chrome/browser/profiles/profile.h"

namespace {

bool test_mode = false;

}  // namespace

namespace extensions {

CloudPrintSetCredentialsFunction::CloudPrintSetCredentialsFunction() {
}

CloudPrintSetCredentialsFunction::~CloudPrintSetCredentialsFunction() {
}

bool CloudPrintSetCredentialsFunction::RunImpl() {
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
    SetResult(Value::CreateStringValue(test_response));
  } else {
    CloudPrintProxyServiceFactory::GetForProfile(profile_)->
        EnableForUserWithRobot(credentials, robot_email, user_email);
  }
  SendResponse(true);
  return true;
}

// static
void CloudPrintSetCredentialsFunction::SetTestMode(bool test_mode_enabled) {
  test_mode = test_mode_enabled;
}

}  // namespace extensions
