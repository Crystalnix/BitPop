// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_CHROMEOS_LOGIN_CORE_OOBE_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_CHROMEOS_LOGIN_CORE_OOBE_HANDLER_H_

#include "chrome/browser/chromeos/login/version_info_updater.h"
#include "chrome/browser/ui/webui/chromeos/login/base_screen_handler.h"

namespace base {
  class ListValue;
}

namespace chromeos {

class OobeUI;

// The core handler for Javascript messages related to the "oobe" view.
class CoreOobeHandler : public BaseScreenHandler,
                        public VersionInfoUpdater::Delegate {
 public:
  explicit CoreOobeHandler(OobeUI* oobe_ui);
  virtual ~CoreOobeHandler();

  // BaseScreenHandler implementation:
  virtual void GetLocalizedStrings(
      base::DictionaryValue* localized_strings) OVERRIDE;
  virtual void Initialize() OVERRIDE;

  // WebUIMessageHandler implementation.
  virtual void RegisterMessages() OVERRIDE;

  // VersionInfoUpdater::Delegate implementation:
  virtual void OnOSVersionLabelTextUpdated(
      const std::string& os_version_label_text) OVERRIDE;
  virtual void OnBootTimesLabelTextUpdated(
      const std::string& boot_times_label_text) OVERRIDE;
  virtual void OnEnterpriseInfoUpdated(
      const std::string& message_text,
      bool reporting_hint) OVERRIDE;

  // Show or hide OOBE UI.
  void ShowOobeUI(bool show);

  bool show_oobe_ui() const {
    return show_oobe_ui_;
  }

 private:
  // Handlers for JS WebUI messages.
  void HandleInitialized(const base::ListValue* args);
  void HandleSkipUpdateEnrollAfterEula(const base::ListValue* args);

  // Calls javascript to sync OOBE UI visibility with show_oobe_ui_.
  void UpdateOobeUIVisibility();

  // Updates label with specified id with specified text.
  void UpdateLabel(const std::string& id, const std::string& text);

  // Owner of this handler.
  OobeUI* oobe_ui_;

  // True if we should show OOBE instead of login.
  bool show_oobe_ui_;

  // Updates when version info is changed.
  VersionInfoUpdater version_info_updater_;

  DISALLOW_COPY_AND_ASSIGN(CoreOobeHandler);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_UI_WEBUI_CHROMEOS_LOGIN_CORE_OOBE_HANDLER_H_
