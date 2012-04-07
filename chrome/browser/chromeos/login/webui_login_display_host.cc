// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/webui_login_display_host.h"

#include "base/command_line.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/chromeos/login/oobe_display.h"
#include "chrome/browser/chromeos/login/webui_login_display.h"
#include "chrome/browser/chromeos/login/webui_login_view.h"
#include "chrome/browser/chromeos/login/wizard_controller.h"
#include "chrome/browser/ui/webui/chromeos/login/oobe_ui.h"
#include "content/public/browser/web_ui.h"
#include "ui/views/widget/widget.h"

#if defined(USE_AURA)
#include "ash/shell.h"
#include "ash/shell_window_ids.h"
#include "ui/aura/window.h"
#endif

namespace chromeos {

namespace {

// URL which corresponds to the login WebUI.
const char kLoginURL[] = "chrome://oobe/login";
// URL which corresponds to the OOBE WebUI.
const char kOobeURL[] = "chrome://oobe";

}  // namespace

// WebUILoginDisplayHost -------------------------------------------------------

WebUILoginDisplayHost::WebUILoginDisplayHost(const gfx::Rect& background_bounds)
    : BaseLoginDisplayHost(background_bounds),
      login_window_(NULL),
      login_view_(NULL),
      webui_login_display_(NULL),
      is_showing_login_(false) {
}

WebUILoginDisplayHost::~WebUILoginDisplayHost() {
  CloseWindow();
}

// LoginDisplayHost implementation ---------------------------------------------

LoginDisplay* WebUILoginDisplayHost::CreateLoginDisplay(
    LoginDisplay::Delegate* delegate) {
  webui_login_display_ = new WebUILoginDisplay(delegate);
  webui_login_display_->set_background_bounds(background_bounds());
  return webui_login_display_;
}

gfx::NativeWindow WebUILoginDisplayHost::GetNativeWindow() const {
  return login_window_ ? login_window_->GetNativeWindow() : NULL;
}

views::Widget* WebUILoginDisplayHost::GetWidget() const {
  return login_window_;
}

void WebUILoginDisplayHost::OpenProxySettings() {
  if (login_view_)
    login_view_->OpenProxySettings();
}

void WebUILoginDisplayHost::SetOobeProgressBarVisible(bool visible) {
  GetOobeUI()->ShowOobeUI(visible);
}

void WebUILoginDisplayHost::SetShutdownButtonEnabled(bool enable) {
}

void WebUILoginDisplayHost::SetStatusAreaEnabled(bool enable) {
  if (login_view_)
    login_view_->SetStatusAreaEnabled(enable);
}

void WebUILoginDisplayHost::SetStatusAreaVisible(bool visible) {
  if (login_view_)
    login_view_->SetStatusAreaVisible(visible);
}

void WebUILoginDisplayHost::StartWizard(const std::string& first_screen_name,
                                        DictionaryValue* screen_parameters) {
  is_showing_login_ = false;

  scoped_ptr<DictionaryValue> scoped_parameters(screen_parameters);
  // This is a special case for WebUI. We don't want to go through the
  // OOBE WebUI page loading. Since we already have the browser we just
  // show the corresponding page.
  if (first_screen_name == WizardController::kHTMLPageScreenName) {
    const CommandLine* cmd_line = CommandLine::ForCurrentProcess();
    const CommandLine::StringVector& args = cmd_line->GetArgs();
    std::string html_page_url;
    for (size_t i = 0; i < args.size(); i++) {
      // It's strange but |args| may contain empty strings.
      if (!args[i].empty()) {
        DCHECK(html_page_url.empty()) << "More than one URL in command line";
        html_page_url = args[i];
      }
    }
    DCHECK(!html_page_url.empty()) << "No URL in command line";
    DCHECK(!login_window_) << "Login window has already been created.";

    LoadURL(GURL(html_page_url));
    return;
  }

  if (!login_window_)
    LoadURL(GURL(kOobeURL));

  BaseLoginDisplayHost::StartWizard(first_screen_name,
                                    scoped_parameters.release());
}

void WebUILoginDisplayHost::StartSignInScreen() {
  is_showing_login_ = true;

  if (!login_window_)
    LoadURL(GURL(kLoginURL));

  BaseLoginDisplayHost::StartSignInScreen();
  CHECK(webui_login_display_);
  GetOobeUI()->ShowSigninScreen(webui_login_display_);
}

void WebUILoginDisplayHost::OnPreferencesChanged() {
  if (is_showing_login_)
    webui_login_display_->OnPreferencesChanged();
}

void WebUILoginDisplayHost::CloseWindow() {
  if (login_window_) {
    login_window_->Close();
    login_window_ = NULL;
    login_view_ = NULL;
  }
}

void WebUILoginDisplayHost::LoadURL(const GURL& url) {
  if (!login_window_) {
    views::Widget::InitParams params(
        views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
    params.bounds = background_bounds();
#if defined(USE_AURA)
    params.show_state = ui::SHOW_STATE_FULLSCREEN;
#endif

    login_window_ = new views::Widget;
    login_window_->Init(params);
    login_view_ = new WebUILoginView();

    login_view_->Init(login_window_);

#if defined(USE_AURA)
    ash::Shell::GetInstance()->GetContainer(
        ash::internal::kShellWindowId_LockScreenContainer)->
        AddChild(login_window_->GetNativeView());
#endif

    login_window_->SetContentsView(login_view_);
    login_view_->UpdateWindowType();

    login_window_->Show();
#if defined(USE_AURA)
    login_window_->GetNativeView()->SetName("WebUILoginView");
#endif
    login_view_->OnWindowCreated();
  }
  login_view_->LoadURL(url);
}

OobeUI* WebUILoginDisplayHost::GetOobeUI() const {
  return static_cast<OobeUI*>(login_view_->GetWebUI()->GetController());
}

WizardController* WebUILoginDisplayHost::CreateWizardController() {
  // TODO(altimofeev): ensure that WebUI is ready.
  OobeDisplay* oobe_display = GetOobeUI();
  return new WizardController(this, oobe_display);
}

}  // namespace chromeos
