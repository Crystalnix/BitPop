// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/dom_login_display.h"

#include "chrome/browser/chromeos/wm_ipc.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser_window.h"
#include "views/widget/widget.h"

namespace {
const char kLoginURL[] = "chrome://login";
}  // namespace

namespace chromeos {

// DOMLoginDisplay, public: ---------------------------------------------------

DOMLoginDisplay::~DOMLoginDisplay() {
  if (webui_login_window_)
    webui_login_window_->Close();
}

// DOMLoginDisplay, Singleton implementation: ----------------------------------

// static
DOMLoginDisplay* DOMLoginDisplay::GetInstance() {
  return Singleton<DOMLoginDisplay>::get();
}

// LoginDisplay implementation: ------------------------------------------------

// static
views::Widget* DOMLoginDisplay::GetLoginWindow() {
  return DOMLoginDisplay::GetInstance()->LoginWindow();
}

views::Widget* DOMLoginDisplay::LoginWindow() {
  return webui_login_window_;
}

void DOMLoginDisplay::Destroy() {
  background_bounds_ = gfx::Rect();
  delegate_ = NULL;

  if (webui_login_window_)
    webui_login_window_->Close();

  webui_login_window_ = NULL;
  webui_login_view_ = NULL;
}

void DOMLoginDisplay::Init(const std::vector<UserManager::User>& users,
                           bool show_guest,
                           bool show_new_user) {
  // Testing that the delegate has been set.
  DCHECK(delegate_);
  users_ = users;

  // TODO(rharrison): Add mechanism to pass in the show_guest and show_new_user
  // values.
  webui_login_window_ = WebUILoginView::CreateWindowContainingView(
      background_bounds_,
      GURL(kLoginURL),
      &webui_login_view_);
  webui_login_window_->Show();
}

void DOMLoginDisplay::OnBeforeUserRemoved(const std::string& username) {
  // TODO(rharrison): Figure out if I need to split anything between this and
  // OnUserRemoved
}

void DOMLoginDisplay::OnUserImageChanged(UserManager::User* user) {
  // TODO(rharrison): Update the user in the user vector
  // TODO(rharrison): Push the change to DOM Login screen
}

void DOMLoginDisplay::OnUserRemoved(const std::string& username) {
  // TODO(rharrison): Remove the user from the user vector
  // TODO(rharrison): Push the change to DOM Login screen
}

void DOMLoginDisplay::OnFadeOut() { }

void DOMLoginDisplay::SetUIEnabled(bool is_enabled) {
  // Send message to WM to enable/disable click on windows.
  WmIpc::Message message(WM_IPC_MESSAGE_WM_SET_LOGIN_STATE);
  message.set_param(0, is_enabled);
  WmIpc::instance()->SendMessage(message);

  if (is_enabled)
    login_handler_->ClearAndEnablePassword();
}

void DOMLoginDisplay::ShowError(int error_msg_id,
                                int login_attempts,
                                HelpAppLauncher::HelpTopic help_topic_id) {
  // TODO(rharrison): Figure out what we should be doing here
}

// DOMLoginDisplay, LoginUIHandlerDelegate implementation: ---------------------

void DOMLoginDisplay::Login(const std::string& username,
                            const std::string& password) {
  DCHECK(delegate_);
  delegate_->Login(username, password);
}

void DOMLoginDisplay::LoginAsGuest() {
  DCHECK(delegate_);
  delegate_->LoginAsGuest();
}

// DOMLoginDisplay, private: ---------------------------------------------------

// Singleton implementation: ---------------------------------------------------

DOMLoginDisplay::DOMLoginDisplay()
    : LoginDisplay(NULL, gfx::Rect()),
      LoginUIHandlerDelegate(),
      webui_login_view_(NULL),
      webui_login_window_(NULL) {
}

}  // namespace chromeos
