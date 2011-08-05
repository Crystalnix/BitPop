// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/mock_authenticator.h"

namespace chromeos {

bool MockAuthenticator::AuthenticateToLogin(Profile* profile,
                                 const std::string& username,
                                 const std::string& password,
                                 const std::string& login_token,
                                 const std::string& login_captcha) {
  if (expected_username_ == username && expected_password_ == password) {
    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
        NewRunnableMethod(this, &MockAuthenticator::OnLoginSuccess,
                          GaiaAuthConsumer::ClientLoginResult(), false));
    return true;
  }
  GoogleServiceAuthError error(
      GoogleServiceAuthError::INVALID_GAIA_CREDENTIALS);
  BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
      NewRunnableMethod(this, &MockAuthenticator::OnLoginFailure,
                        LoginFailure::FromNetworkAuthFailure(error)));
  return false;
}

bool MockAuthenticator::AuthenticateToUnlock(const std::string& username,
                                  const std::string& password) {
  return AuthenticateToLogin(NULL /* not used */, username, password,
                             std::string(), std::string());
}

void MockAuthenticator::LoginOffTheRecord() {
  consumer_->OnOffTheRecordLoginSuccess();
}

void MockAuthenticator::OnLoginSuccess(
    const GaiaAuthConsumer::ClientLoginResult& credentials,
    bool request_pending) {
  // If we want to be more like the real thing, we could save username
  // in AuthenticateToLogin, but there's not much of a point.
  consumer_->OnLoginSuccess(expected_username_,
                            expected_password_,
                            credentials,
                            request_pending);
}

void MockAuthenticator::OnLoginFailure(const LoginFailure& failure) {
    consumer_->OnLoginFailure(failure);
    VLOG(1) << "Posting a QuitTask to UI thread";
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE, new MessageLoop::QuitTask);
}

////////////////////////////////////////////////////////////////////////////////
// MockLoginUtils

MockLoginUtils::MockLoginUtils(const std::string& expected_username,
                               const std::string& expected_password)
    : expected_username_(expected_username),
      expected_password_(expected_password) {
}

MockLoginUtils::~MockLoginUtils() {}

bool MockLoginUtils::ShouldWaitForWifi() {
  return false;
}

void MockLoginUtils::PrepareProfile(
    const std::string& username,
    const std::string& password,
    const GaiaAuthConsumer::ClientLoginResult& res,
    bool pending_requests,
    Delegate* delegate) {
  DCHECK_EQ(expected_username_, username);
  DCHECK_EQ(expected_password_, password);
  // Profile hasn't been loaded.
  delegate->OnProfilePrepared(NULL);
}

Authenticator* MockLoginUtils::CreateAuthenticator(
    LoginStatusConsumer* consumer) {
  return new MockAuthenticator(
      consumer, expected_username_, expected_password_);
}

void MockLoginUtils::SetBackgroundView(BackgroundView* background_view) {
  background_view_ = background_view;
}

BackgroundView* MockLoginUtils::GetBackgroundView() {
  return background_view_;
}

std::string MockLoginUtils::GetOffTheRecordCommandLine(
    const GURL& start_url,
    const CommandLine& base_command_line,
    CommandLine* command_line) {
  return std::string();
}

}  // namespace chromeos
