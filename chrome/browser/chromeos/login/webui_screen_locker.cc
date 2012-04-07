// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/webui_screen_locker.h"

#include "base/command_line.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/cros/network_library.h"
#include "chrome/browser/chromeos/login/helper.h"
#include "chrome/browser/chromeos/login/screen_locker.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/login/webui_login_display.h"
#include "chrome/browser/chromeos/status/status_area_view_chromeos.h"
#include "chrome/browser/ui/views/dom_view.h"
#include "chrome/browser/ui/webui/chromeos/login/oobe_ui.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/url_constants.h"
#include "content/browser/renderer_host/render_widget_host_view.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/web_ui.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/x/x11_util.h"
#include "ui/gfx/screen.h"

namespace {

// URL which corresponds to the login WebUI.
const char kLoginURL[] = "chrome://oobe/login";

}  // namespace

namespace chromeos {

////////////////////////////////////////////////////////////////////////////////
// WebUIScreenLocker implementation.

WebUIScreenLocker::WebUIScreenLocker(ScreenLocker* screen_locker)
    : ScreenLockerDelegate(screen_locker),
      lock_ready_(false),
      webui_ready_(false) {
}

void WebUIScreenLocker::LockScreen(bool unlock_on_input) {
  gfx::Rect bounds(gfx::Screen::GetMonitorAreaNearestWindow(NULL));

  LockWindow* lock_window = LockWindow::Create();
  lock_window->set_observer(this);
  lock_window_ = lock_window->GetWidget();
  WebUILoginView::Init(lock_window_);
  lock_window_->SetContentsView(this);
  lock_window_->Show();
  OnWindowCreated();
  LoadURL(GURL(kLoginURL));
  lock_window->Grab(webui_login_);

  // User list consisting of a single logged-in user.
  UserList users(1, &chromeos::UserManager::Get()->logged_in_user());
  login_display_.reset(new WebUILoginDisplay(this));
  login_display_->set_background_bounds(bounds);
  login_display_->Init(users, false, true, false);

  static_cast<OobeUI*>(GetWebUI()->GetController())->ShowSigninScreen(
      login_display_.get());

  registrar_.Add(this,
                 chrome::NOTIFICATION_LOGIN_USER_IMAGE_CHANGED,
                 content::NotificationService::AllSources());
  registrar_.Add(this,
                 chrome::NOTIFICATION_LOCK_WEBUI_READY,
                 content::NotificationService::AllSources());
}

void WebUIScreenLocker::ScreenLockReady() {
  ScreenLockerDelegate::ScreenLockReady();
  SetInputEnabled(true);
}

void WebUIScreenLocker::OnAuthenticate() {
}

void WebUIScreenLocker::SetInputEnabled(bool enabled) {
  login_display_->SetUIEnabled(enabled);
  SetStatusAreaEnabled(enabled);
}

void WebUIScreenLocker::SetSignoutEnabled(bool enabled) {
  // TODO(flackr): Implement (crbug.com/105267).
  NOTIMPLEMENTED();
}

void WebUIScreenLocker::ShowErrorMessage(const string16& message,
                                         bool sign_out_only) {
  // TODO(flackr): Use login_display_ to show error message (requires either
  // adding a method to display error strings or strictly passing error ids:
  // crbug.com/105267).
  base::FundamentalValue login_attempts_value(0);
  base::StringValue error_message(message);
  base::StringValue help_link("");
  base::FundamentalValue help_id(0);
  GetWebUI()->CallJavascriptFunction("cr.ui.Oobe.showSignInError",
                                     login_attempts_value,
                                     error_message,
                                     help_link,
                                     help_id);
}

void WebUIScreenLocker::ShowCaptchaAndErrorMessage(const GURL& captcha_url,
                                                   const string16& message) {
  ShowErrorMessage(message, true);
}

void WebUIScreenLocker::ClearErrors() {
  GetWebUI()->CallJavascriptFunction("cr.ui.Oobe.clearErrors");
}

gfx::NativeWindow WebUIScreenLocker::GetNativeWindow() const {
  return lock_window_->GetNativeWindow();
}

WebUIScreenLocker::~WebUIScreenLocker() {
  DCHECK(lock_window_);
  lock_window_->Close();
  // If LockScreen() was called, we need to clear the signin screen handler
  // delegate set in ShowSigninScreen so that it no longer points to us.
  if (login_display_.get()) {
    static_cast<OobeUI*>(GetWebUI()->GetController())->
        ResetSigninScreenHandlerDelegate();
  }
  // WebUILoginView::OnTabMainFrameFirstRender sets the screen mode to
  // WebUIScreenLocker::GetScreenMode() = SCREEN_LOCKER_MODE. We need to reset
  // the screen mode when the lock screen is hidden here.
  chromeos::StatusAreaViewChromeos::SetScreenMode(
      chromeos::StatusAreaViewChromeos::BROWSER_MODE);
  SetStatusAreaEnabled(true);
}

////////////////////////////////////////////////////////////////////////////////
// WebUIScreenLocker, content::NotificationObserver implementation:

void WebUIScreenLocker::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  switch (type) {
    case chrome::NOTIFICATION_LOGIN_USER_IMAGE_CHANGED: {
      const User& user = *content::Details<User>(details).ptr();
      login_display_->OnUserImageChanged(user);
      break;
    }
    case chrome::NOTIFICATION_LOCK_WEBUI_READY: {
      webui_ready_ = true;
      if (lock_ready_)
        ScreenLockReady();
      break;
    }
    default:
      NOTREACHED();
  }
}

////////////////////////////////////////////////////////////////////////////////
// WebUIScreenLocker, LoginDisplay::Delegate implementation:

void WebUIScreenLocker::CreateAccount() {
  NOTREACHED();
}

string16 WebUIScreenLocker::GetConnectedNetworkName() {
  return GetCurrentNetworkName(CrosLibrary::Get()->GetNetworkLibrary());
}

void WebUIScreenLocker::FixCaptivePortal() {
  NOTREACHED();
}

void WebUIScreenLocker::SetDisplayEmail(const std::string& email) {
  NOTREACHED();
}

void WebUIScreenLocker::CompleteLogin(const std::string& username,
                                      const std::string& password) {
  NOTREACHED();
}

void WebUIScreenLocker::Login(const std::string& username,
                              const std::string& password) {
  chromeos::ScreenLocker::default_screen_locker()->Authenticate(
      ASCIIToUTF16(password));
}

void WebUIScreenLocker::LoginAsGuest() {
  NOTREACHED();
}

void WebUIScreenLocker::OnUserSelected(const std::string& username) {
}

void WebUIScreenLocker::OnStartEnterpriseEnrollment() {
  NOTREACHED();
}

////////////////////////////////////////////////////////////////////////////////
// LockWindow::Observer implementation:

void WebUIScreenLocker::OnLockWindowReady() {
  lock_ready_ = true;
  if (webui_ready_)
    ScreenLockReady();
}

////////////////////////////////////////////////////////////////////////////////
// Overridden from WebUILoginView:

StatusAreaViewChromeos::ScreenMode WebUIScreenLocker::GetScreenMode() {
  return StatusAreaViewChromeos::SCREEN_LOCKER_MODE;
}

views::Widget::InitParams::Type WebUIScreenLocker::GetStatusAreaWidgetType() {
  return views::Widget::InitParams::TYPE_POPUP;
}

}  // namespace chromeos
