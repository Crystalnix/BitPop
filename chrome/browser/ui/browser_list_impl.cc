// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/browser_list_impl.h"

#include "base/command_line.h"
#include "base/logging.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browser_shutdown.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list_observer.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_switches.h"
#include "content/public/browser/notification_service.h"

// #include "build/build_config.h"
// #include "chrome/browser/prefs/pref_service.h"


#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/login/user_manager.h"
#endif

namespace chrome {

// static
BrowserListImpl* BrowserListImpl::native_instance_ = NULL;
BrowserListImpl* BrowserListImpl::ash_instance_ = NULL;

////////////////////////////////////////////////////////////////////////////////
// BrowserListImpl, public:

// static
BrowserListImpl* BrowserListImpl::GetInstance(HostDesktopType type) {
  BrowserListImpl** list = NULL;
  if (type == HOST_DESKTOP_TYPE_NATIVE)
    list = &native_instance_;
  else if (type == HOST_DESKTOP_TYPE_ASH)
    list = &ash_instance_;
  else
    NOTREACHED();
  if (!*list)
    *list = new BrowserListImpl;
  return *list;
}

void BrowserListImpl::AddBrowser(Browser* browser) {
  DCHECK(browser);
  browsers_.push_back(browser);

  g_browser_process->AddRefModule();

  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_BROWSER_OPENED,
      content::Source<Browser>(browser),
      content::NotificationService::NoDetails());

  // Send out notifications after add has occurred. Do some basic checking to
  // try to catch evil observers that change the list from under us.
  size_t original_count = observers_.size();
  FOR_EACH_OBSERVER(BrowserListObserver, observers_, OnBrowserAdded(browser));
  DCHECK_EQ(original_count, observers_.size())
      << "observer list modified during notification";
}

void BrowserListImpl::RemoveBrowser(Browser* browser) {
  RemoveBrowserFrom(browser, &last_active_browsers_);

  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_BROWSER_CLOSED,
      content::Source<Browser>(browser),
      content::NotificationService::NoDetails());

  RemoveBrowserFrom(browser, &browsers_);

  FOR_EACH_OBSERVER(BrowserListObserver, observers_, OnBrowserRemoved(browser));

  g_browser_process->ReleaseModule();

  // If we're exiting, send out the APP_TERMINATING notification to allow other
  // modules to shut themselves down.
  if (browsers_.empty() &&
      (browser_shutdown::IsTryingToQuit() ||
       g_browser_process->IsShuttingDown())) {
    // Last browser has just closed, and this is a user-initiated quit or there
    // is no module keeping the app alive, so send out our notification. No need
    // to call ProfileManager::ShutdownSessionServices() as part of the
    // shutdown, because Browser::WindowClosing() already makes sure that the
    // SessionService is created and notified.
    browser::NotifyAppTerminating();
    browser::OnAppExiting();
  }
}

void BrowserListImpl::AddObserver(BrowserListObserver* observer) {
  observers_.AddObserver(observer);
}

void BrowserListImpl::RemoveObserver(BrowserListObserver* observer) {
  observers_.RemoveObserver(observer);
}

void BrowserListImpl::SetLastActive(Browser* browser) {
  // If the browser is currently trying to quit, we don't want to set the last
  // active browser because that can alter the last active browser that the user
  // intended depending on the order in which the windows close.
  if (browser_shutdown::IsTryingToQuit())
    return;
  RemoveBrowserFrom(browser, &last_active_browsers_);
  last_active_browsers_.push_back(browser);

  FOR_EACH_OBSERVER(BrowserListObserver, observers_,
                    OnBrowserSetLastActive(browser));
}

Browser* BrowserListImpl::GetLastActive() {
  if (!last_active_browsers_.empty())
    return *(last_active_browsers_.rbegin());
  return NULL;
}

void BrowserListImpl::CloseAllBrowsersWithProfile(Profile* profile) {
  BrowserVector browsers_to_close;
  for (BrowserListImpl::const_iterator i = BrowserListImpl::begin();
       i != BrowserListImpl::end(); ++i) {
    if ((*i)->profile()->GetOriginalProfile() == profile->GetOriginalProfile())
      browsers_to_close.push_back(*i);
  }

  for (BrowserVector::const_iterator i = browsers_to_close.begin();
       i != browsers_to_close.end(); ++i) {
    (*i)->window()->Close();
  }
}

bool BrowserListImpl::IsIncognitoWindowOpen() {
  for (BrowserListImpl::const_iterator i = BrowserListImpl::begin();
       i != BrowserListImpl::end(); ++i) {
    if ((*i)->profile()->IsOffTheRecord())
      return true;
  }
  return false;
}

bool BrowserListImpl::IsIncognitoWindowOpenForProfile(Profile* profile) {
#if defined(OS_CHROMEOS)
  // In ChromeOS, we assume that the default profile is always valid, so if
  // we are in guest mode, keep the OTR profile active so it won't be deleted.
  if (chromeos::UserManager::Get()->IsLoggedInAsGuest())
    return true;
#endif
  for (BrowserListImpl::const_iterator i = BrowserListImpl::begin();
       i != BrowserListImpl::end(); ++i) {
    if ((*i)->profile()->IsSameProfile(profile) &&
        (*i)->profile()->IsOffTheRecord()) {
      return true;
    }
  }
  return false;
}

////////////////////////////////////////////////////////////////////////////////
// BrowserListImpl, private:

BrowserListImpl::BrowserListImpl() {
}

BrowserListImpl::~BrowserListImpl() {
}

void BrowserListImpl::RemoveBrowserFrom(Browser* browser,
                                        BrowserVector* browser_list) {
  const iterator remove_browser =
      std::find(browser_list->begin(), browser_list->end(), browser);
  if (remove_browser != browser_list->end())
    browser_list->erase(remove_browser);
}

}  // namespace chrome
