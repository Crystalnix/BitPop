// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/sync_global_error.h"

#include "base/basictypes.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/signin/signin_manager.h"
#include "chrome/browser/sync/profile_sync_service_mock.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "content/test/test_browser_thread.h"
#include "testing/gmock/include/gmock/gmock-actions.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::NiceMock;
using content::BrowserThread;

namespace {

// Utility function to test that SyncGlobalError behaves correct for the given
// error condition.
void VerifySyncGlobalErrorResult(NiceMock<ProfileSyncServiceMock>* service,
                                 Browser* browser,
                                 SyncGlobalError* error,
                                 GoogleServiceAuthError::State error_state,
                                 bool is_signed_in,
                                 bool is_error) {
  EXPECT_CALL(*service, HasSyncSetupCompleted())
              .WillRepeatedly(Return(is_signed_in));

  GoogleServiceAuthError auth_error(error_state);
  EXPECT_CALL(*service, GetAuthError()).WillRepeatedly(ReturnRef(auth_error));

  error->OnStateChanged();

  // If there is an error then a wrench button badge, menu item, and bubble view
  // should be shown.
  EXPECT_EQ(error->HasBadge(), is_error);
  EXPECT_EQ(error->HasMenuItem() || error->HasCustomizedSyncMenuItem(),
            is_error);
  EXPECT_EQ(error->HasBubbleView(), is_error);

  // If there is an error then labels should not be empty.
  EXPECT_NE(error->MenuItemCommandID(), 0);
  EXPECT_NE(error->MenuItemLabel().empty(), is_error);
  EXPECT_NE(error->GetBubbleViewAcceptButtonLabel().empty(), is_error);

  // We never have a cancel button.
  EXPECT_TRUE(error->GetBubbleViewCancelButtonLabel().empty());
  // We always return a hardcoded title.
  EXPECT_FALSE(error->GetBubbleViewTitle().empty());

  // Test message handler.
  if (is_error) {
    EXPECT_CALL(*service, ShowErrorUI());
    error->ExecuteMenuItem(browser);
    EXPECT_CALL(*service, ShowErrorUI());
    error->BubbleViewAcceptButtonPressed(browser);
    error->BubbleViewDidClose(browser);
  }
}

} // namespace

typedef BrowserWithTestWindowTest SyncGlobalErrorTest;

// Test that SyncGlobalError shows an error if a passphrase is required.
TEST_F(SyncGlobalErrorTest, PassphraseGlobalError) {
  scoped_ptr<Profile> profile(
      ProfileSyncServiceMock::MakeSignedInTestingProfile());
  NiceMock<ProfileSyncServiceMock> service(profile.get());
  SyncGlobalError error(&service);

  EXPECT_CALL(service, IsPassphraseRequired())
              .WillRepeatedly(Return(true));
  EXPECT_CALL(service, IsPassphraseRequiredForDecryption())
              .WillRepeatedly(Return(true));
  VerifySyncGlobalErrorResult(
      &service, browser(), &error, GoogleServiceAuthError::NONE, true, true);
}

// Test that SyncGlobalError shows an error for conditions that can be resolved
// by the user and suppresses errors for conditions that  cannot be resolved by
// the user.
TEST_F(SyncGlobalErrorTest, AuthStateGlobalError) {
  scoped_ptr<Profile> profile(
      ProfileSyncServiceMock::MakeSignedInTestingProfile());
  NiceMock<ProfileSyncServiceMock> service(profile.get());
  SyncGlobalError error(&service);

  browser_sync::SyncBackendHost::Status status;
  EXPECT_CALL(service, QueryDetailedSyncStatus())
              .WillRepeatedly(Return(status));

  struct {
    GoogleServiceAuthError::State error_state;
    bool is_error;
  } table[] = {
    { GoogleServiceAuthError::NONE, false },
    { GoogleServiceAuthError::INVALID_GAIA_CREDENTIALS, true },
    { GoogleServiceAuthError::USER_NOT_SIGNED_UP, true },
    { GoogleServiceAuthError::CONNECTION_FAILED, false },
    { GoogleServiceAuthError::CAPTCHA_REQUIRED, true },
    { GoogleServiceAuthError::ACCOUNT_DELETED, true },
    { GoogleServiceAuthError::ACCOUNT_DISABLED, true },
    { GoogleServiceAuthError::SERVICE_UNAVAILABLE, true },
    { GoogleServiceAuthError::TWO_FACTOR, true },
    { GoogleServiceAuthError::REQUEST_CANCELED, true },
    { GoogleServiceAuthError::HOSTED_NOT_ALLOWED, true },
  };

  for (size_t i = 0; i < sizeof(table)/sizeof(*table); ++i) {
    VerifySyncGlobalErrorResult(&service, browser(), &error,
                                table[i].error_state, true, table[i].is_error);
    VerifySyncGlobalErrorResult(&service, browser(), &error,
                                table[i].error_state, false, false);
  }
}
