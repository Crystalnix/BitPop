// Copyright (c) 2013 House of Life Property ltd. All rights reserved.
// Copyright (c) 2013 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/bitpop/bitpop_api.h"

#include "base/values.h"
#include "chrome/browser/signin/signin_manager.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "chrome/browser/sync/profile_sync_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/webui/signin/login_ui_service.h"
#include "chrome/browser/ui/webui/signin/login_ui_service_factory.h"

namespace {
// Errors.
const char kInvalidArguments[] =
  "Invalid arguments passed to function.";
const char kNoCurrentWindowError[] = "No current browser window was found";

bool GetAuthData(const DictionaryValue* result,
                 std::string* username,
                 std::string* access_token) {
  if (!result->GetString("user", username) ||
      !result->GetString("accessToken", access_token)) {
      return false;
  }
  return true;
}

} // namespace

// List is considered empty if it is actually empty or contains just one value,
// either 'null' or 'undefined'.
static bool IsArgumentListEmpty(const ListValue* arguments) {
  if (arguments->empty())
    return true;
  if (arguments->GetSize() == 1) {
    const Value* first_value = 0;
    if (!arguments->Get(0, &first_value))
      return true;
    if (first_value->GetType() == Value::TYPE_NULL)
      return true;
  }
  return false;
}

bool SyncLoginResultReadyFunction::RunImpl() {
  if (!args_.get())
    return false;

  DictionaryValue* value = NULL;
  if (IsArgumentListEmpty(args_.get())) {
    error_ = kInvalidArguments;
    return false;
  } else {
    EXTENSION_FUNCTION_VALIDATE(args_->GetDictionary(0, &value));
  }

  std::string username, access_token;
  GetAuthData(value, &username, &access_token);

	SigninManager* signin = SigninManagerFactory::GetForProfile(profile_;;
  signin->PrepareForSignin(SIGNIN_TYPE_CLIENT_LOGIN, username, "");

  UserInfoMap info_map;
  info_map['email'] = username;
  signin->OnGetUserInfoSuccess(info_map);

  ProfileSyncServiceFactory::GetForProfile(profile_)->OnIssueAuthTokenSuccess(
      GaiaConstants::kSyncService,
      access_token);

  Browser *browser = browser::FindLastActiveWithProfile(profile_);
  if (!browser) {
		browser = new Browser(Browser::CreateParams(profile_));
    browser->window()->Show();
  }

  login->ShowLoginUI(browser);

  browser->window()->Activate();

  return true;
}
