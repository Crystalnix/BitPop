// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PROTECTOR_SETTINGS_CHANGE_GLOBAL_ERROR_H_
#define CHROME_BROWSER_PROTECTOR_SETTINGS_CHANGE_GLOBAL_ERROR_H_
#pragma once

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/protector/base_setting_change.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/global_error.h"

class Browser;
class Profile;

namespace protector {

class BaseSettingChange;
class SettingsChangeGlobalErrorDelegate;

// Global error about unwanted settings changes.
class SettingsChangeGlobalError : public GlobalError,
                                  public BrowserList::Observer {
 public:
  // Creates new global error about setting changes |change| which must not be
  // deleted until |delegate->OnRemovedFromProfile| is called. Uses |delegate|
  // to notify about user decision.
  SettingsChangeGlobalError(BaseSettingChange* change,
                            SettingsChangeGlobalErrorDelegate* delegate);
  virtual ~SettingsChangeGlobalError();

  // Displays a global error bubble for the given browser profile.
  // Can be called from any thread.
  void ShowForProfile(Profile* profile);

  // Removes global error from its profile.
  void RemoveFromProfile();

 private:
  // GlobalError implementation.
  virtual bool HasBadge() OVERRIDE;
  virtual int GetBadgeResourceID() OVERRIDE;
  virtual bool HasMenuItem() OVERRIDE;
  virtual int MenuItemCommandID() OVERRIDE;
  virtual string16 MenuItemLabel() OVERRIDE;
  virtual int MenuItemIconResourceID() OVERRIDE;
  virtual void ExecuteMenuItem(Browser* browser) OVERRIDE;
  virtual bool HasBubbleView() OVERRIDE;
  virtual int GetBubbleViewIconResourceID() OVERRIDE;
  virtual string16 GetBubbleViewTitle() OVERRIDE;
  virtual string16 GetBubbleViewMessage() OVERRIDE;
  virtual string16 GetBubbleViewAcceptButtonLabel() OVERRIDE;
  virtual string16 GetBubbleViewCancelButtonLabel() OVERRIDE;
  virtual void OnBubbleViewDidClose(Browser* browser) OVERRIDE;
  virtual void BubbleViewAcceptButtonPressed(Browser* browser) OVERRIDE;
  virtual void BubbleViewCancelButtonPressed(Browser* browser) OVERRIDE;

  // BrowserList::Observer implementation.
  virtual void OnBrowserAdded(const Browser* browser) OVERRIDE {}
  virtual void OnBrowserRemoved(const Browser* browser) OVERRIDE {}
  virtual void OnBrowserSetLastActive(const Browser* browser) OVERRIDE;

  // Helper called on the UI thread to add this global error to the default
  // profile (stored in |profile_|).
  void AddToProfile(Profile* profile);

  // Displays the bubble in the last active tabbed browser. Must be called
  // on the UI thread.
  void Show();

  // Displays the bubble in |browser|'s window. Must be called
  // on the UI thread.
  void ShowInBrowser(Browser* browser);

  // Called when the wrench menu item has been displayed for enough time
  // without user interaction.
  void OnInactiveTimeout();

  // Change to show.
  BaseSettingChange* change_;

  // Delegate to notify about user actions.
  SettingsChangeGlobalErrorDelegate* delegate_;

  // Profile that we have been added to.
  Profile* profile_;

  // True if user has dismissed the bubble by clicking on one of the buttons.
  bool closed_by_button_;

  // True if the bubble has to be shown on the next browser window activation.
  bool show_on_browser_activation_;

  base::WeakPtrFactory<SettingsChangeGlobalError> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(SettingsChangeGlobalError);
};

}  // namespace protector

#endif  // CHROME_BROWSER_PROTECTOR_SETTINGS_CHANGE_GLOBAL_ERROR_H_
