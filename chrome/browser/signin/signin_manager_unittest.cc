// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/signin/signin_manager.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/compiler_specific.h"
#include "chrome/browser/password_manager/encryptor.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/signin/token_service.h"
#include "chrome/browser/signin/token_service_unittest.h"
#include "chrome/browser/sync/util/oauth.h"
#include "chrome/browser/webdata/web_data_service.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/net/gaia/gaia_urls.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/testing_profile.h"
#include "content/test/test_url_fetcher_factory.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_status.h"

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

class MockGaiaOAuthFetcher : public GaiaOAuthFetcher {
 public:
  MockGaiaOAuthFetcher(GaiaOAuthConsumer* consumer,
                       net::URLRequestContextGetter* getter,
                       Profile* profile,
                       const std::string& service_scope)
      : GaiaOAuthFetcher(
          consumer, getter, profile, service_scope) {}

  ~MockGaiaOAuthFetcher() {}

  MOCK_METHOD1(StartOAuthGetAccessToken,
               void(const std::string& oauth1_request_token));

  MOCK_METHOD4(StartOAuthWrapBridge,
               void(const std::string& oauth1_access_token,
                    const std::string& oauth1_access_token_secret,
                    const std::string& wrap_token_duration,
                    const std::string& oauth2_scope));

  MOCK_METHOD1(StartUserInfo, void(const std::string& oauth2_access_token));
};

class SigninManagerTest : public TokenServiceTestHarness {
 public:
  virtual void SetUp() OVERRIDE {
    TokenServiceTestHarness::SetUp();
    manager_.reset(new SigninManager());
    google_login_success_.ListenFor(
        chrome::NOTIFICATION_GOOGLE_SIGNIN_SUCCESSFUL,
        content::Source<Profile>(profile_.get()));
    google_login_failure_.ListenFor(chrome::NOTIFICATION_GOOGLE_SIGNIN_FAILED,
                                    content::Source<Profile>(profile_.get()));
    originally_using_oauth_ = browser_sync::IsUsingOAuth();
  }

  virtual void TearDown() OVERRIDE {
    TokenServiceTestHarness::TearDown();
    browser_sync::SetIsUsingOAuthForTest(originally_using_oauth_);
  }

  void SimulateValidResponseClientLogin() {
    DCHECK(!browser_sync::IsUsingOAuth());
    // Simulate the correct ClientLogin response.
    TestURLFetcher* fetcher = factory_.GetFetcherByID(0);
    DCHECK(fetcher);
    DCHECK(fetcher->delegate());

    fetcher->set_url(GURL(GaiaUrls::GetInstance()->client_login_url()));
    fetcher->set_status(net::URLRequestStatus());
    fetcher->set_response_code(200);
    fetcher->SetResponseString("SID=sid\nLSID=lsid\nAuth=auth");
    fetcher->delegate()->OnURLFetchComplete(fetcher);

    // Then simulate the correct GetUserInfo response for the canonical email.
    // A new URL fetcher is used for each call.
    fetcher = factory_.GetFetcherByID(0);
    DCHECK(fetcher);
    DCHECK(fetcher->delegate());
    fetcher->set_url(GURL(GaiaUrls::GetInstance()->get_user_info_url()));
    fetcher->set_status(net::URLRequestStatus());
    fetcher->set_response_code(200);
    fetcher->SetResponseString("email=user@gmail.com");
    fetcher->delegate()->OnURLFetchComplete(fetcher);
  }

  void SimulateSigninStartOAuth() {
    DCHECK(browser_sync::IsUsingOAuth());
    // Simulate a valid OAuth-based signin
    MockGaiaOAuthFetcher* fetcher = new MockGaiaOAuthFetcher(
        manager_.get(),
        profile_->GetRequestContext(),
        profile_.get(),
        std::string());
    std::string request_token ="oauth1_request_token";
    EXPECT_CALL(*fetcher, StartOAuthGetAccessToken(request_token)).Times(1);
    manager_->StartOAuthSignIn(request_token, fetcher);
    manager_->OnGetOAuthTokenSuccess("oauth_token-Ev1Vu1hv");
    manager_->OnOAuthGetAccessTokenSuccess("oauth1_access_token-qOAmlrSM",
                                           "secret-NKKn1DuR");
    manager_->OnOAuthWrapBridgeSuccess(browser_sync::SyncServiceName(),
                                       "oauth2_wrap_access_token-R0Z3nRtw",
                                       "3600");
  }

  void SimulateOAuthUserInfoSuccess() {
    manager_->OnUserInfoSuccess("user-xZIuqTKu@gmail.com");
  }

  void SimulateValidSigninOAuth() {
    SimulateSigninStartOAuth();
    SimulateOAuthUserInfoSuccess();
  }


  TestURLFetcherFactory factory_;
  scoped_ptr<SigninManager> manager_;
  TestNotificationTracker google_login_success_;
  TestNotificationTracker google_login_failure_;
  bool originally_using_oauth_;
};

// NOTE: ClientLogin's "StartSignin" is called after collecting credentials
//       from the user.  See also SigninManagerTest::SignInOAuth.
TEST_F(SigninManagerTest, SignInClientLogin) {
  browser_sync::SetIsUsingOAuthForTest(false);
  manager_->Initialize(profile_.get());
  EXPECT_TRUE(manager_->GetAuthenticatedUsername().empty());

  manager_->StartSignIn("username", "password", "", "");
  EXPECT_TRUE(manager_->GetAuthenticatedUsername().empty());

  SimulateValidResponseClientLogin();
  EXPECT_FALSE(manager_->GetAuthenticatedUsername().empty());

  // Should go into token service and stop.
  EXPECT_EQ(1U, google_login_success_.size());
  EXPECT_EQ(0U, google_login_failure_.size());

  // Should persist across resets.
  manager_.reset(new SigninManager());
  manager_->Initialize(profile_.get());
  EXPECT_EQ("user@gmail.com", manager_->GetAuthenticatedUsername());
}

TEST_F(SigninManagerTest, ClearTransientSigninData) {
  browser_sync::SetIsUsingOAuthForTest(false);
  manager_->Initialize(profile_.get());
  EXPECT_TRUE(manager_->GetAuthenticatedUsername().empty());

  manager_->StartSignIn("username", "password", "", "");
  EXPECT_TRUE(manager_->GetAuthenticatedUsername().empty());

  SimulateValidResponseClientLogin();

  // Should go into token service and stop.
  EXPECT_EQ(1U, google_login_success_.size());
  EXPECT_EQ(0U, google_login_failure_.size());

  EXPECT_EQ("user@gmail.com", manager_->GetAuthenticatedUsername());

  // Now clear the in memory data.
  manager_->ClearTransientSigninData();
  EXPECT_TRUE(manager_->last_result_.data.empty());
  EXPECT_FALSE(manager_->GetAuthenticatedUsername().empty());

  // Ensure preferences are not modified.
  EXPECT_FALSE(
     profile_->GetPrefs()->GetString(prefs::kGoogleServicesUsername).empty());

  // On reset it should be regenerated.
  manager_.reset(new SigninManager());
  manager_->Initialize(profile_.get());

  // Now make sure we have the right user name.
  EXPECT_EQ("user@gmail.com", manager_->GetAuthenticatedUsername());
}

// NOTE: OAuth's "StartOAuthSignIn" is called before collecting credentials
//       from the user.  See also SigninManagerTest::SignInClientLogin.
TEST_F(SigninManagerTest, SignInOAuth) {
  browser_sync::SetIsUsingOAuthForTest(true);
  manager_->Initialize(profile_.get());
  EXPECT_TRUE(manager_->GetAuthenticatedUsername().empty());

  SimulateValidSigninOAuth();
  EXPECT_FALSE(manager_->GetAuthenticatedUsername().empty());

  // Should go into token service and stop.
  EXPECT_EQ(1U, google_login_success_.size());
  EXPECT_EQ(0U, google_login_failure_.size());

  // Should persist across resets.
  manager_.reset(new SigninManager());
  manager_->Initialize(profile_.get());
  EXPECT_EQ("user-xZIuqTKu@gmail.com", manager_->GetAuthenticatedUsername());
}

TEST_F(SigninManagerTest, SignOutClientLogin) {
  browser_sync::SetIsUsingOAuthForTest(false);
  manager_->Initialize(profile_.get());
  manager_->StartSignIn("username", "password", "", "");
  SimulateValidResponseClientLogin();
  manager_->OnClientLoginSuccess(credentials_);

  EXPECT_EQ("user@gmail.com", manager_->GetAuthenticatedUsername());
  manager_->SignOut();
  EXPECT_TRUE(manager_->GetAuthenticatedUsername().empty());
  // Should not be persisted anymore
  manager_.reset(new SigninManager());
  manager_->Initialize(profile_.get());
  EXPECT_TRUE(manager_->GetAuthenticatedUsername().empty());
}

TEST_F(SigninManagerTest, SignOutOAuth) {
  browser_sync::SetIsUsingOAuthForTest(true);
  manager_->Initialize(profile_.get());

  SimulateValidSigninOAuth();
  EXPECT_FALSE(manager_->GetAuthenticatedUsername().empty());

  EXPECT_EQ("user-xZIuqTKu@gmail.com", manager_->GetAuthenticatedUsername());
  manager_->SignOut();
  EXPECT_TRUE(manager_->GetAuthenticatedUsername().empty());

  // Should not be persisted anymore
  manager_.reset(new SigninManager());
  manager_->Initialize(profile_.get());
  EXPECT_TRUE(manager_->GetAuthenticatedUsername().empty());
}

TEST_F(SigninManagerTest, SignInFailureClientLogin) {
  browser_sync::SetIsUsingOAuthForTest(false);
  manager_->Initialize(profile_.get());
  manager_->StartSignIn("username", "password", "", "");
  GoogleServiceAuthError error(GoogleServiceAuthError::REQUEST_CANCELED);
  manager_->OnClientLoginFailure(error);

  EXPECT_EQ(0U, google_login_success_.size());
  EXPECT_EQ(1U, google_login_failure_.size());

  EXPECT_TRUE(manager_->GetAuthenticatedUsername().empty());

  // Should not be persisted
  manager_.reset(new SigninManager());
  manager_->Initialize(profile_.get());
  EXPECT_TRUE(manager_->GetAuthenticatedUsername().empty());
}

TEST_F(SigninManagerTest, ProvideSecondFactorSuccess) {
  browser_sync::SetIsUsingOAuthForTest(false);
  manager_->Initialize(profile_.get());
  manager_->StartSignIn("username", "password", "", "");
  GoogleServiceAuthError error(GoogleServiceAuthError::TWO_FACTOR);
  manager_->OnClientLoginFailure(error);

  EXPECT_EQ(0U, google_login_success_.size());
  EXPECT_EQ(1U, google_login_failure_.size());

  EXPECT_TRUE(manager_->GetAuthenticatedUsername().empty());
  EXPECT_FALSE(manager_->possibly_invalid_username_.empty());

  manager_->ProvideSecondFactorAccessCode("access");
  SimulateValidResponseClientLogin();

  EXPECT_EQ(1U, google_login_success_.size());
  EXPECT_EQ(1U, google_login_failure_.size());
}

TEST_F(SigninManagerTest, ProvideSecondFactorFailure) {
  browser_sync::SetIsUsingOAuthForTest(false);
  manager_->Initialize(profile_.get());
  manager_->StartSignIn("username", "password", "", "");
  GoogleServiceAuthError error1(GoogleServiceAuthError::TWO_FACTOR);
  manager_->OnClientLoginFailure(error1);

  EXPECT_EQ(0U, google_login_success_.size());
  EXPECT_EQ(1U, google_login_failure_.size());

  EXPECT_TRUE(manager_->GetAuthenticatedUsername().empty());
  EXPECT_FALSE(manager_->possibly_invalid_username_.empty());

  manager_->ProvideSecondFactorAccessCode("badaccess");
  GoogleServiceAuthError error2(
      GoogleServiceAuthError::INVALID_GAIA_CREDENTIALS);
  manager_->OnClientLoginFailure(error2);

  EXPECT_EQ(0U, google_login_success_.size());
  EXPECT_EQ(2U, google_login_failure_.size());
  EXPECT_TRUE(manager_->GetAuthenticatedUsername().empty());

  manager_->ProvideSecondFactorAccessCode("badaccess");
  GoogleServiceAuthError error3(GoogleServiceAuthError::CONNECTION_FAILED);
  manager_->OnClientLoginFailure(error3);

  EXPECT_EQ(0U, google_login_success_.size());
  EXPECT_EQ(3U, google_login_failure_.size());
  EXPECT_TRUE(manager_->GetAuthenticatedUsername().empty());
}

TEST_F(SigninManagerTest, SignOutMidConnect) {
  browser_sync::SetIsUsingOAuthForTest(false);
  manager_->Initialize(profile_.get());
  manager_->StartSignIn("username", "password", "", "");
  manager_->SignOut();
  EXPECT_EQ(0U, google_login_success_.size());
  EXPECT_EQ(0U, google_login_failure_.size());

  EXPECT_TRUE(manager_->GetAuthenticatedUsername().empty());
}

TEST_F(SigninManagerTest, SignOutOnUserInfoSucessRaceTest) {
  browser_sync::SetIsUsingOAuthForTest(true);
  manager_->Initialize(profile_.get());
  EXPECT_TRUE(manager_->GetAuthenticatedUsername().empty());
  SimulateSigninStartOAuth();
  manager_->SignOut();
  SimulateOAuthUserInfoSuccess();
  EXPECT_TRUE(manager_->GetAuthenticatedUsername().empty());
}
