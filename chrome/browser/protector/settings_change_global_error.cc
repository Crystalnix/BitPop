// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/protector/settings_change_global_error.h"

#include "base/bind.h"
#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/stl_util.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/protector/base_setting_change.h"
#include "chrome/browser/protector/settings_change_global_error_delegate.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/global_error_service.h"
#include "chrome/browser/ui/global_error_service_factory.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace protector {

namespace {

// Timeout before the global error is removed (wrench menu item disappears).
const int kMenuItemDisplayPeriodMs = 10*60*1000;  // 10 min

}  // namespace

SettingsChangeGlobalError::SettingsChangeGlobalError(
    BaseSettingChange* change,
    SettingsChangeGlobalErrorDelegate* delegate)
    : change_(change),
      delegate_(delegate),
      profile_(NULL),
      closed_by_button_(false),
      show_on_browser_activation_(false),
      ALLOW_THIS_IN_INITIALIZER_LIST(weak_factory_(this)) {
  DCHECK(delegate_);
}

SettingsChangeGlobalError::~SettingsChangeGlobalError() {
}

bool SettingsChangeGlobalError::HasBadge() {
  return true;
}

int SettingsChangeGlobalError::GetBadgeResourceID() {
  return change_->GetBadgeIconID();
}

bool SettingsChangeGlobalError::HasMenuItem() {
  return true;
}

int SettingsChangeGlobalError::MenuItemCommandID() {
  return IDC_SHOW_SETTINGS_CHANGES;
}

string16 SettingsChangeGlobalError::MenuItemLabel() {
  return change_->GetBubbleTitle();
}

int SettingsChangeGlobalError::MenuItemIconResourceID() {
  return change_->GetMenuItemIconID();
}

void SettingsChangeGlobalError::ExecuteMenuItem(Browser* browser) {
  ShowInBrowser(browser);
}

bool SettingsChangeGlobalError::HasBubbleView() {
  return true;
}

int SettingsChangeGlobalError::GetBubbleViewIconResourceID() {
  return change_->GetBubbleIconID();
}

string16 SettingsChangeGlobalError::GetBubbleViewTitle() {
  return change_->GetBubbleTitle();
}

string16 SettingsChangeGlobalError::GetBubbleViewMessage() {
  return change_->GetBubbleMessage();
}

// The Accept and Revert buttons are swapped like the 'server' and 'client'
// concepts in X11. Accept button (the default one) discards changes
// (keeps using previous setting) while cancel button applies changes
// (switches to the new setting). This is sick and blows my mind. - ivankr

string16 SettingsChangeGlobalError::GetBubbleViewAcceptButtonLabel() {
  return change_->GetDiscardButtonText();
}

string16 SettingsChangeGlobalError::GetBubbleViewCancelButtonLabel() {
  return change_->GetApplyButtonText();
}

void SettingsChangeGlobalError::BubbleViewAcceptButtonPressed(
    Browser* browser) {
  closed_by_button_ = true;
  delegate_->OnDiscardChange(browser);
}

void SettingsChangeGlobalError::BubbleViewCancelButtonPressed(
    Browser* browser) {
  closed_by_button_ = true;
  delegate_->OnApplyChange(browser);
}

void SettingsChangeGlobalError::OnBrowserSetLastActive(
    const Browser* browser) {
  if (show_on_browser_activation_ && browser && browser->is_type_tabbed()) {
    // A tabbed browser window got activated, show the error bubble again.
    // Calling Show() immediately from here does not always work because the
    // old browser window may still have focus.
    // Multiple posted Show() calls are fine since the first successful one
    // will invalidate all the weak pointers.
    // Note that Show() will display the bubble in the last active browser
    // (which may not be |browser| at the moment Show() is executed).
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(&SettingsChangeGlobalError::Show,
                   weak_factory_.GetWeakPtr()));
  }
}

void SettingsChangeGlobalError::RemoveFromProfile() {
  if (profile_)
    GlobalErrorServiceFactory::GetForProfile(profile_)->RemoveGlobalError(this);
  BrowserList::RemoveObserver(this);
  // This will delete |this|.
  delegate_->OnRemovedFromProfile();
}

void SettingsChangeGlobalError::OnBubbleViewDidClose(Browser* browser) {
  if (!closed_by_button_) {
    BrowserThread::PostDelayedTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(&SettingsChangeGlobalError::OnInactiveTimeout,
                   weak_factory_.GetWeakPtr()),
        kMenuItemDisplayPeriodMs);
    if (browser->window() &&
        !platform_util::IsWindowActive(browser->window()->GetNativeHandle())) {
      // Bubble closed because the entire window lost activation, display
      // again when a window gets active.
      show_on_browser_activation_ = true;
    }
  } else {
    RemoveFromProfile();
  }
}

void SettingsChangeGlobalError::ShowForProfile(Profile* profile) {
  if (BrowserThread::CurrentlyOn(BrowserThread::UI)) {
    AddToProfile(profile);
  } else {
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(&SettingsChangeGlobalError::AddToProfile,
                   base::Unretained(this),
                   profile));
  }
}

void SettingsChangeGlobalError::AddToProfile(Profile* profile) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  profile_ = profile;
  GlobalErrorServiceFactory::GetForProfile(profile_)->AddGlobalError(this);
  BrowserList::AddObserver(this);
  Show();
}

void SettingsChangeGlobalError::Show() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(profile_);
  Browser* browser = BrowserList::FindTabbedBrowser(
      profile_,
      // match incognito
      true);
  if (browser)
    ShowInBrowser(browser);
}

void SettingsChangeGlobalError::ShowInBrowser(Browser* browser) {
  show_on_browser_activation_ = false;
  // Cancel any previously posted tasks so that the global error
  // does not get removed on timeout while still showing the bubble.
  weak_factory_.InvalidateWeakPtrs();
  ShowBubbleView(browser);
}

void SettingsChangeGlobalError::OnInactiveTimeout() {
  delegate_->OnDecisionTimeout();
  RemoveFromProfile();
}

}  // namespace protector
