// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_OPTIONS2_CHROMEOS_ACCOUNTS_OPTIONS_HANDLER2_H_
#define CHROME_BROWSER_UI_WEBUI_OPTIONS2_CHROMEOS_ACCOUNTS_OPTIONS_HANDLER2_H_
#pragma once

#include "base/compiler_specific.h"
#include "chrome/browser/ui/webui/options2/options_ui2.h"

namespace chromeos {
namespace options2 {

// ChromeOS accounts options page handler.
class AccountsOptionsHandler : public ::options2::OptionsPageUIHandler {
 public:
  AccountsOptionsHandler();
  virtual ~AccountsOptionsHandler();

  // WebUIMessageHandler implementation.
  virtual void RegisterMessages() OVERRIDE;

  // OptionsPageUIHandler implementation.
  virtual void GetLocalizedValues(
      base::DictionaryValue* localized_strings) OVERRIDE;

 private:
  // Javascript callbacks to whitelist/unwhitelist user.
  void HandleWhitelistUser(const base::ListValue* args);
  void HandleUnwhitelistUser(const base::ListValue* args);

  // Javascript callback to auto add existing users to white list.
  void HandleWhitelistExistingUsers(const base::ListValue* args);

  DISALLOW_COPY_AND_ASSIGN(AccountsOptionsHandler);
};

}  // namespace options2
}  // namespace chromeos

#endif  // CHROME_BROWSER_UI_WEBUI_OPTIONS2_CHROMEOS_ACCOUNTS_OPTIONS_HANDLER2_H_
