// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_SYNC_PROMO_SYNC_PROMO_HANDLER2_H_
#define CHROME_BROWSER_UI_WEBUI_SYNC_PROMO_SYNC_PROMO_HANDLER2_H_
#pragma once

#include "chrome/browser/ui/webui/sync_setup_handler2.h"

class PrefService;

namespace options2 {

// The handler for JavaScript messages related to the "sync promo" page.
class SyncPromoHandler2 : public SyncSetupHandler2 {
 public:
  explicit SyncPromoHandler2(const std::string& source,
                             ProfileManager* profile_manager);
  virtual ~SyncPromoHandler2();

  // Called to register our preferences before we use them (so there will be a
  // default if not present yet).
  static void RegisterUserPrefs(PrefService* prefs);

  // WebUIMessageHandler implementation.
  virtual WebUIMessageHandler* Attach(content::WebUI* web_ui) OVERRIDE;
  virtual void RegisterMessages() OVERRIDE;

  // SyncSetupFlowHandler implementation.
  virtual void ShowGaiaSuccessAndClose() OVERRIDE;
  virtual void ShowGaiaSuccessAndSettingUp() OVERRIDE;
  virtual void ShowConfigure(const base::DictionaryValue& args) OVERRIDE;

  // content::NotificationObserver implementation.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

 protected:
  virtual void StepWizardForShowSetupUI() OVERRIDE;

  virtual void ShowSetupUI() OVERRIDE;

 private:
  // JavaScript callback handler to close the sync promo.
  void HandleCloseSyncPromo(const base::ListValue* args);

  // Gets the sync promo layout for the current sync promo version.
  int GetPromoVersion();

  // JavaScript callback handler to initialize the sync promo.
  void HandleInitializeSyncPromo(const base::ListValue* args);

  // JavaScript handler to record the duration for which the throbber was
  // visible during an attempted sign-in flow.
  void HandleRecordThrobberTime(const base::ListValue* args);

  // JavaScript handler to record the number of times a user attempted to sign
  // in to chrome while they were on the sync promo page.
  void HandleRecordSignInAttempts(const base::ListValue* args);

  // JavaScript callback handler to switch the advanced sync settings. |args| is
  // the list of arguments passed from JS and should be an empty list.
  void HandleShowAdvancedSettings(const base::ListValue* args);

  // JavaScript callback handler to record user actions on the sync promo.
  void HandleUserFlowAction(const base::ListValue* args);

  // JavaScript callback handler for when a user clicks skip.
  void HandleUserSkipped(const base::ListValue* args);

  // Return the number of times the user with the current profile has seen the
  // sync promo.
  int GetViewCount() const;

  // Increment the local view count by the specified non-negative integer
  // amount. Returns the new total view count.
  int IncrementViewCountBy(size_t amount);

  // Record a user's flow through the promo to our histogram in UMA.
  void RecordUserFlowAction(int action);

  // Load any experiments that run on the promo page.
  void LoadPromoExperiments();

  // Use this to register for certain notifications (currently when tabs or
  // windows close).
  content::NotificationRegistrar registrar_;

  // Weak reference that's initialized and checked in Attach() (after that
  // guaranteed to be non-NULL).
  PrefService* prefs_;

  // If the user closes the whole window we'll get a close notification from the
  // tab as well, so this bool acts as a small mutex to only report the close
  // method once.
  bool window_already_closed_;

  // Extra UMA histogram name to log stats to, based on the source for showing
  // the sync promo page.
  std::string histogram_name_;

  DISALLOW_COPY_AND_ASSIGN(SyncPromoHandler2);
};

}  // namespace options2

#endif  // CHROME_BROWSER_UI_WEBUI_SYNC_PROMO_SYNC_PROMO_HANDLER2_H_
