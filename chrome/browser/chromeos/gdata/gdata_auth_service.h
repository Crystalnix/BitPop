// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_GDATA_GDATA_AUTH_SERVICE_H_
#define CHROME_BROWSER_CHROMEOS_GDATA_GDATA_AUTH_SERVICE_H_

#include <string>

#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop_proxy.h"
#include "base/observer_list.h"
#include "chrome/browser/chromeos/gdata/gdata_errorcode.h"
#include "chrome/browser/chromeos/gdata/gdata_params.h"
#include "chrome/common/net/gaia/oauth2_access_token_fetcher.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"

class Profile;

namespace gdata {

class GDataOperationRegistry;

// This class provides authentication for GData based services.
// It integrates specific service integration with OAuth2 stack
// (TokenService) and provides OAuth2 token refresh infrastructure.
// All public functions must be called on UI thread.
class GDataAuthService : public content::NotificationObserver {
 public:
  class Observer {
   public:
    // Triggered when a new OAuth2 refresh token is received from TokenService.
    virtual void OnOAuth2RefreshTokenChanged() = 0;

   protected:
    virtual ~Observer() {}
  };

  GDataAuthService();
  virtual ~GDataAuthService();

  // Adds and removes the observer. AddObserver() should be called before
  // Initialize() as it can change the refresh token.
  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Initializes the auth service. Starts TokenService to retrieve the
  // refresh token.
  void Initialize(Profile* profile);

  // Starts fetching OAuth2 auth token from the refresh token.
  void StartAuthentication(GDataOperationRegistry* registry,
                           const AuthStatusCallback& callback);

  // True if an OAuth2 access token is retrieved and believed to be fresh.
  // The access token is used to access the gdata server.
  bool HasAccessToken() const { return !access_token_.empty(); }

  // True if an OAuth2 refresh token is present. Its absence means that user
  // is not properly authenticated.
  // The refresh token is used to get the access token.
  bool HasRefreshToken() const { return !refresh_token_.empty(); }

  // Returns OAuth2 access token.
  const std::string& access_token() const { return access_token_; }

  // Clears OAuth2 access token.
  void ClearAccessToken() { access_token_.clear(); }

  // Callback for AuthOperation (InternalAuthStatusCallback).
  void OnAuthCompleted(scoped_refptr<base::MessageLoopProxy> relay_proxy,
                       const AuthStatusCallback& callback,
                       GDataErrorCode error,
                       const std::string& access_token);

  // Overridden from content::NotificationObserver:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // Sets the access_token as specified.  This should be used only for testing.
  void set_access_token_for_testing(const std::string& token) {
    access_token_ = token;
  }

 private:
  // Helper function for StartAuthentication() call.
  void StartAuthenticationOnUIThread(
      GDataOperationRegistry* registry,
      scoped_refptr<base::MessageLoopProxy> relay_proxy,
      const AuthStatusCallback& callback);

  Profile* profile_;
  std::string refresh_token_;
  std::string access_token_;
  ObserverList<Observer> observers_;

  content::NotificationRegistrar registrar_;

  // Note: This should remain the last member so it'll be destroyed and
  // invalidate its weak pointers before any other members are destroyed.
  base::WeakPtrFactory<GDataAuthService> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(GDataAuthService);
};

}  // namespace gdata

#endif  // CHROME_BROWSER_CHROMEOS_GDATA_GDATA_AUTH_SERVICE_H_
