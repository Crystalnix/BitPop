// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROME_TO_MOBILE_SERVICE_H_
#define CHROME_BROWSER_CHROME_TO_MOBILE_SERVICE_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/file_path.h"
#include "base/memory/scoped_vector.h"
#include "base/memory/weak_ptr.h"
#include "base/string16.h"
#include "base/timer.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile_keyed_service.h"
#include "chrome/browser/sessions/session_id.h"
#include "chrome/common/net/gaia/oauth2_access_token_consumer.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "googleurl/src/gurl.h"
#include "net/url_request/url_fetcher_delegate.h"

class OAuth2AccessTokenFetcher;
class Browser;
class CloudPrintURL;
class MockChromeToMobileService;
class PrefService;
class Profile;

namespace net {
class URLFetcher;
}

// ChromeToMobileService connects to the cloud print service to enumerate
// compatible mobiles owned by its profile and send URLs and MHTML snapshots.
class ChromeToMobileService : public ProfileKeyedService,
                              public net::URLFetcherDelegate,
                              public content::NotificationObserver,
                              public OAuth2AccessTokenConsumer {
 public:
  class Observer {
   public:
    virtual ~Observer();

    // Called on generation of the page's MHTML snapshot.
    virtual void SnapshotGenerated(const FilePath& path, int64 bytes) = 0;

    // Called after URLFetcher responses from sending the URL (and snapshot).
    virtual void OnSendComplete(bool success) = 0;
  };

  enum Metric {
    DEVICES_REQUESTED = 0,  // Cloud print was contacted to list devices.
    DEVICES_AVAILABLE,      // Cloud print returned 1+ compatible devices.
    BUBBLE_SHOWN,           // The page action bubble was shown.
    SNAPSHOT_GENERATED,     // A snapshot was successfully generated.
    SNAPSHOT_ERROR,         // An error occurred during snapshot generation.
    SENDING_URL,            // Send was invoked (with or without a snapshot).
    SENDING_SNAPSHOT,       // A snapshot was sent along with the page URL.
    SEND_SUCCESS,           // Cloud print responded with success on send.
    SEND_ERROR,             // Cloud print responded with failure on send.
    LEARN_MORE_CLICKED,     // The "Learn more" help article link was clicked.
    NUM_METRICS
  };

  // The supported mobile device operating systems.
  enum MobileOS {
    ANDROID = 0,
    IOS,
  };

  // The cloud print job types.
  enum JobType {
    URL = 0,
    DELAYED_SNAPSHOT,
    SNAPSHOT,
  };

  // The cloud print job submission data.
  struct JobData {
    JobData();
    ~JobData();

    MobileOS mobile_os;
    string16 mobile_id;
    GURL url;
    string16 title;
    FilePath snapshot;
    std::string snapshot_id;
    JobType type;
  };

  // Returns whether Chrome To Mobile is enabled. Check for the 'disable' or
  // 'enable' command line switches, otherwise relay the default enabled state.
  static bool IsChromeToMobileEnabled();

  // Register the user prefs associated with this service.
  static void RegisterUserPrefs(PrefService* prefs);

  explicit ChromeToMobileService(Profile* profile);
  virtual ~ChromeToMobileService();

  // Returns true if the service has found any registered mobile devices.
  bool HasMobiles();

  // Get the non-NULL ListValue of mobile devices from the cloud print service.
  // The list is owned by PrefService, which outlives ChromeToMobileService.
  // Each device DictionaryValue contains strings "type", "name", and "id".
  // Virtual for unit test mocking.
  virtual const base::ListValue* GetMobiles() const;

  // Request an updated mobile device list, request auth first if needed.
  // Virtual for unit test mocking.
  virtual void RequestMobileListUpdate();

  // Callback with an MHTML snapshot of the browser's selected WebContents.
  // Virtual for unit test mocking.
  virtual void GenerateSnapshot(Browser* browser,
                                base::WeakPtr<Observer> observer);

  // Send the browser's selected WebContents to the specified mobile device.
  // Virtual for unit test mocking.
  virtual void SendToMobile(const base::DictionaryValue& mobile,
                            const FilePath& snapshot,
                            Browser* browser,
                            base::WeakPtr<Observer> observer);

  // Delete the snapshot file (should be called on observer destruction).
  // Virtual for unit test mocking.
  virtual void DeleteSnapshot(const FilePath& snapshot);

  // Log a metric for the "ChromeToMobile.Service" histogram.
  // Virtual for unit test mocking.
  virtual void LogMetric(Metric metric) const;

  // Opens the "Learn More" help article link in the supplied |browser|.
  void LearnMore(Browser* browser) const;

  // net::URLFetcherDelegate method.
  virtual void OnURLFetchComplete(const net::URLFetcher* source) OVERRIDE;

  // content::NotificationObserver method.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // OAuth2AccessTokenConsumer methods.
  virtual void OnGetTokenSuccess(const std::string& access_token,
                                 const base::Time& expiration_time) OVERRIDE;
  virtual void OnGetTokenFailure(const GoogleServiceAuthError& error) OVERRIDE;

 private:
  friend class MockChromeToMobileService;

  // Handle the attempted creation of a temporary file for snapshot generation.
  // Alert the observer of failure or generate MHTML with an observer callback.
  void SnapshotFileCreated(base::WeakPtr<Observer> observer,
                           SessionID::id_type browser_id,
                           const FilePath& path,
                           bool success);

  // Create a cloud print job submission request for a URL or snapshot.
  net::URLFetcher* CreateRequest(const JobData& data);

  // Initialize URLFetcher requests (search and jobs submit).
  void InitRequest(net::URLFetcher* request);

  // Submit a cloud print job request with the requisite data.
  void SendRequest(net::URLFetcher* request, const JobData& data);

  // Send the OAuth2AccessTokenFetcher request.
  // Virtual for unit test mocking.
  virtual void RefreshAccessToken();

  // Request account information to limit cloud print access to existing users.
  void RequestAccountInfo();

  // Send the cloud print URLFetcher search request.
  void RequestSearch();

  void HandleAccountInfoResponse();
  void HandleSearchResponse();
  void HandleSubmitResponse(const net::URLFetcher* source);

  base::WeakPtrFactory<ChromeToMobileService> weak_ptr_factory_;

  Profile* profile_;

  // Used to recieve TokenService notifications for GaiaOAuth2LoginRefreshToken.
  content::NotificationRegistrar registrar_;

  // Cloud print helper class and auth token.
  scoped_ptr<CloudPrintURL> cloud_print_url_;
  std::string access_token_;

  // The set of snapshots currently available.
  std::set<FilePath> snapshots_;

  // Map URLFetchers to observers for reporting OnSendComplete.
  typedef std::map<const net::URLFetcher*, base::WeakPtr<Observer> >
      RequestObserverMap;
  RequestObserverMap request_observer_map_;

  // The pending OAuth access token request and a timer for retrying on failure.
  scoped_ptr<OAuth2AccessTokenFetcher> access_token_fetcher_;
  base::OneShotTimer<ChromeToMobileService> auth_retry_timer_;

  // The pending account information request and the cloud print access flag.
  scoped_ptr<net::URLFetcher> account_info_request_;
  bool cloud_print_accessible_;

  // The pending mobile device search request.
  scoped_ptr<net::URLFetcher> search_request_;

  DISALLOW_COPY_AND_ASSIGN(ChromeToMobileService);
};

#endif  // CHROME_BROWSER_CHROME_TO_MOBILE_SERVICE_H_
