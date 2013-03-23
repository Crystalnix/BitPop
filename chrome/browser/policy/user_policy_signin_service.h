// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_POLICY_USER_POLICY_SIGNIN_SERVICE_H_
#define CHROME_BROWSER_POLICY_USER_POLICY_SIGNIN_SERVICE_H_

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/policy/cloud_policy_service.h"
#include "chrome/browser/policy/user_info_fetcher.h"
#include "chrome/browser/profiles/profile_keyed_service.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "google_apis/gaia/oauth2_access_token_consumer.h"

class OAuth2AccessTokenFetcher;
class Profile;

namespace base {
class Time;
}

namespace policy {

class UserCloudPolicyManager;

// The UserPolicySigninService is responsible for interacting with the policy
// infrastructure (mainly UserCloudPolicyManager) to load policy for the signed
// in user.
//
// At signin time, this class initializes the UCPM and loads policy before any
// other signed in services are initialized. After each restart, this class
// ensures that the CloudPolicyClient is registered (in case the policy server
// was offline during the initial policy fetch) and if not it initiates a fresh
// registration process.
//
// Finally, if the user signs out, this class is responsible for shutting down
// the policy infrastructure to ensure that any cached policy is cleared.
class UserPolicySigninService
    : public ProfileKeyedService,
      public OAuth2AccessTokenConsumer,
      public CloudPolicyService::Observer,
      public CloudPolicyClient::Observer,
      public UserInfoFetcher::Delegate,
      public content::NotificationObserver {
 public:
  // The callback invoked once policy fetch is complete. Passed boolean
  // parameter is set to true if the policy fetch succeeded.
  typedef base::Callback<void(bool)> PolicyFetchCallback;

  // Creates a UserPolicySigninService associated with the passed |profile|.
  explicit UserPolicySigninService(Profile* profile);
  virtual ~UserPolicySigninService();

  // Initiates a policy fetch as part of user signin. The |oauth2_access_token|
  // is explicitly passed because TokenService does not have the token yet
  // (to prevent services from using it until after we've fetched policy).
  // |callback| is invoked once the policy fetch is complete, passing true if
  // the policy fetch succeeded.
  void FetchPolicyForSignedInUser(const std::string& oauth2_access_token,
                                  const PolicyFetchCallback& callback);

  // content::NotificationObserver implementation.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // CloudPolicyService::Observer implementation.
  virtual void OnInitializationCompleted(CloudPolicyService* service) OVERRIDE;

  // CloudPolicyClient::Observer implementation.
  virtual void OnClientError(CloudPolicyClient* client) OVERRIDE;
  virtual void OnPolicyFetched(CloudPolicyClient* client) OVERRIDE;
  virtual void OnRegistrationStateChanged(CloudPolicyClient* client) OVERRIDE;

  // OAuth2AccessTokenConsumer implementation.
  virtual void OnGetTokenSuccess(const std::string& access_token,
                                 const base::Time& expiration_time) OVERRIDE;
  virtual void OnGetTokenFailure(const GoogleServiceAuthError& error) OVERRIDE;

  // ProfileKeyedService implementation:
  virtual void Shutdown() OVERRIDE;

  // UserInfoFetcher::Delegate implementation:
  virtual void OnGetUserInfoSuccess(const DictionaryValue* response) OVERRIDE;
  virtual void OnGetUserInfoFailure(
      const GoogleServiceAuthError& error) OVERRIDE;

 private:
  // Returns false if cloud policy is disabled or if the currently signed-in
  // user is definitely not from a hosted domain (according to the blacklist in
  // BrowserPolicyConnector::IsNonEnterpriseUser()).
  bool ShouldLoadPolicyForSignedInUser();

  // Initializes the UserCloudPolicyManager to reflect the currently-signed-in
  // user.
  void InitializeUserCloudPolicyManager();

  // Fetches an OAuth token to allow the cloud policy service to register with
  // the cloud policy server. |oauth_login_token| should contain an OAuth login
  // refresh token that can be downscoped to get an access token for the
  // device_management service.
  void RegisterCloudPolicyService(std::string oauth_login_token);

  // Helper routines to (un)register for CloudPolicyService and
  // CloudPolicyClient notifications.
  void StartObserving();
  void StopObserving();

  // If a policy fetch was requested, invokes the callback passing through the
  // |success| flag.
  void NotifyPendingFetchCallback(bool success);

  // Shuts down the UserCloudPolicyManager (for example, after the user signs
  // out) and deletes any cached policy.
  void ShutdownUserCloudPolicyManager();

  // Convenience helper to get the UserCloudPolicyManager for |profile_|.
  UserCloudPolicyManager* GetManager();

  // WeakPtrFactory used to create callbacks for loading policy.
  base::WeakPtrFactory<UserPolicySigninService> weak_factory_;

  // Weak pointer to the profile this service is associated with.
  Profile* profile_;

  // If true, we have a pending fetch so notify the callback the next time
  // the appropriate notification is delivered from CloudPolicyService/Client.
  bool pending_fetch_;

  // The callback to invoke when the pending policy fetch is completed.
  PolicyFetchCallback pending_fetch_callback_;

  content::NotificationRegistrar registrar_;

  // Fetcher used while obtaining an OAuth token for client registration.
  scoped_ptr<OAuth2AccessTokenFetcher> oauth2_access_token_fetcher_;

  // Helper class for fetching information from GAIA about the currently
  // signed-in user.
  scoped_ptr<UserInfoFetcher> user_info_fetcher_;

  // Access token used to register the CloudPolicyClient and also access
  // GAIA to get information about the signed in user.
  std::string oauth_access_token_;

  DISALLOW_COPY_AND_ASSIGN(UserPolicySigninService);
};

}  // namespace policy

#endif  // CHROME_BROWSER_POLICY_USER_POLICY_SIGNIN_SERVICE_H_
