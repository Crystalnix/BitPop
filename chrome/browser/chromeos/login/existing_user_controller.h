// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_EXISTING_USER_CONTROLLER_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_EXISTING_USER_CONTROLLER_H_
#pragma once

#include <string>

#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "base/string16.h"
#include "base/task.h"
#include "base/timer.h"
#include "chrome/browser/chromeos/login/captcha_view.h"
#include "chrome/browser/chromeos/login/login_display.h"
#include "chrome/browser/chromeos/login/login_performer.h"
#include "chrome/browser/chromeos/login/login_utils.h"
#include "chrome/browser/chromeos/login/ownership_status_checker.h"
#include "chrome/browser/chromeos/login/password_changed_view.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/wm_message_listener.h"
#include "content/common/notification_observer.h"
#include "content/common/notification_registrar.h"
#include "googleurl/src/gurl.h"
#include "testing/gtest/include/gtest/gtest_prod.h"
#include "ui/gfx/rect.h"

namespace chromeos {

class LoginDisplayHost;
class UserCrosSettingsProvider;

// ExistingUserController is used to handle login when someone has
// already logged into the machine.
// To use ExistingUserController create an instance of it and invoke Init.
// When Init is called it creates LoginDisplay instance which encapsulates
// all login UI implementation.
// ExistingUserController maintains it's own life cycle and deletes itself when
// the user logs in (or chooses to see other settings).
class ExistingUserController : public LoginDisplay::Delegate,
                               public NotificationObserver,
                               public LoginPerformer::Delegate,
                               public LoginUtils::Delegate,
                               public CaptchaView::Delegate,
                               public PasswordChangedView::Delegate {
 public:
  // All UI initialization is deferred till Init() call.
  explicit ExistingUserController(LoginDisplayHost* host);
  ~ExistingUserController();

  // Returns the current existing user controller if it has been created.
  static ExistingUserController* current_controller() {
    return current_controller_;
  }

  // Creates and shows login UI for known users.
  void Init(const UserVector& users);

  // LoginDisplay::Delegate: implementation
  virtual void CreateAccount() OVERRIDE;
  virtual string16 GetConnectedNetworkName() OVERRIDE;
  virtual void FixCaptivePortal() OVERRIDE;
  virtual void Login(const std::string& username,
                     const std::string& password) OVERRIDE;
  virtual void LoginAsGuest() OVERRIDE;
  virtual void OnUserSelected(const std::string& username) OVERRIDE;
  virtual void OnStartEnterpriseEnrollment() OVERRIDE;

  // NotificationObserver implementation.
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

 private:
  friend class ExistingUserControllerTest;
  friend class MockLoginPerformerDelegate;

  // LoginPerformer::Delegate implementation:
  virtual void OnLoginFailure(const LoginFailure& error);
  virtual void OnLoginSuccess(
      const std::string& username,
      const std::string& password,
      const GaiaAuthConsumer::ClientLoginResult& credentials,
      bool pending_requests);
  virtual void OnOffTheRecordLoginSuccess();
  virtual void OnPasswordChangeDetected(
      const GaiaAuthConsumer::ClientLoginResult& credentials);
  virtual void WhiteListCheckFailed(const std::string& email);

  // LoginUtils::Delegate implementation:
  virtual void OnProfilePrepared(Profile* profile);

  // CaptchaView::Delegate:
  virtual void OnCaptchaEntered(const std::string& captcha);

  // PasswordChangedView::Delegate:
  virtual void RecoverEncryptedData(const std::string& old_password);
  virtual void ResyncEncryptedData();

  // Starts WizardController with the specified screen.
  void ActivateWizard(const std::string& screen_name);

  // Returns corresponding native window.
  gfx::NativeWindow GetNativeWindow() const;

  // Changes state of the status area. During login operation it's disabled.
  void SetStatusAreaEnabled(bool enable);

  // Show error message. |error_id| error message ID in resources.
  // If |details| string is not empty, it specify additional error text
  // provided by authenticator, it is not localized.
  void ShowError(int error_id, const std::string& details);

  // Handles result of ownership check and starts enterprise enrollment if
  // applicable.
  void OnEnrollmentOwnershipCheckCompleted(OwnershipService::Status status);

  void set_login_performer_delegate(LoginPerformer::Delegate* d) {
    login_performer_delegate_.reset(d);
  }

  // Used to execute login operations.
  scoped_ptr<LoginPerformer> login_performer_;

  // Login UI implementation instance.
  LoginDisplay* login_display_;

  // Delegate for login performer to be overridden by tests.
  // |this| is used if |login_performer_delegate_| is NULL.
  scoped_ptr<LoginPerformer::Delegate> login_performer_delegate_;

  // Username of the last login attempt.
  std::string last_login_attempt_username_;

  // OOBE/login display host.
  LoginDisplayHost* host_;

  // Number of login attempts. Used to show help link when > 1 unsuccessful
  // logins for the same user.
  size_t num_login_attempts_;

  // Pointer to the current instance of the controller to be used by
  // automation tests.
  static ExistingUserController* current_controller_;

  // Triggers prefetching of user settings.
  scoped_ptr<UserCrosSettingsProvider> user_settings_;

  // URL to append to start Guest mode with.
  GURL guest_mode_url_;

  // Used for user image changed notifications.
  NotificationRegistrar registrar_;

  // Factory of callbacks.
  ScopedRunnableMethodFactory<ExistingUserController> method_factory_;

  // Whether everything is ready to launch the browser.
  bool ready_for_browser_launch_;

  // Whether two factor credentials were used.
  bool two_factor_credentials_;

  // Used to verify ownership before starting enterprise enrollment.
  scoped_ptr<OwnershipStatusChecker> ownership_checker_;

  FRIEND_TEST_ALL_PREFIXES(ExistingUserControllerTest, NewUserLogin);

  DISALLOW_COPY_AND_ASSIGN(ExistingUserController);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_EXISTING_USER_CONTROLLER_H_
