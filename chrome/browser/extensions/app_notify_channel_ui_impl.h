// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_APP_NOTIFY_CHANNEL_UI_IMPL_H_
#define CHROME_BROWSER_EXTENSIONS_APP_NOTIFY_CHANNEL_UI_IMPL_H_

#include <string>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "chrome/browser/extensions/app_notify_channel_ui.h"
#include "chrome/browser/sync/profile_sync_service_observer.h"

class Profile;
class TabContents;

namespace extensions {

class AppNotifyChannelUIImpl : public AppNotifyChannelUI,
                               public ProfileSyncServiceObserver {
 public:
  AppNotifyChannelUIImpl(Profile* profile,
                         TabContents* tab_contents,
                         const std::string& app_name,
                         AppNotifyChannelUI::UIType ui_type);
  virtual ~AppNotifyChannelUIImpl();

  // AppNotifyChannelUI.
  virtual void PromptSyncSetup(AppNotifyChannelUI::Delegate* delegate) OVERRIDE;

 protected:
  // A private class we use to put up an infobar - its lifetime is managed by
  // |tab_contents_|, so we don't have one as an instance variable.
  class InfoBar;
  friend class AppNotifyChannelUIImpl::InfoBar;

  // Called by our InfoBar when it's accepted or cancelled/closed.
  void OnInfoBarResult(bool accepted);

  // ProfileSyncServiceObserver.
  virtual void OnStateChanged() OVERRIDE;

 private:
  void StartObservingSync();
  void StopObservingSync();

  Profile* profile_;
  TabContents* tab_contents_;
  std::string app_name_;
  AppNotifyChannelUI::UIType ui_type_;
  AppNotifyChannelUI::Delegate* delegate_;

  // Have we registered ourself as a ProfileSyncServiceObserver?
  bool observing_sync_;

  // This is for working around a bug that ProfileSyncService calls
  // ProfileSyncServiceObserver::OnStateChanged callback many times
  // after ShowLoginDialog is called and before the wizard is
  // actually visible to the user. So we record if the wizard was
  // shown to user and then wait for wizard to get dismissed.
  // See crbug.com/101842.
  bool wizard_shown_to_user_;

  DISALLOW_COPY_AND_ASSIGN(AppNotifyChannelUIImpl);
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_APP_NOTIFY_CHANNEL_UI_IMPL_H_
