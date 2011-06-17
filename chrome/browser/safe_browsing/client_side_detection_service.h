// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Helper class which handles communication with the SafeBrowsing backends for
// client-side phishing detection.  This class can be used to get a file
// descriptor to the client-side phishing model and also to send a ping back to
// Google to verify if a particular site is really phishing or not.
//
// This class is not thread-safe and expects all calls to GetModelFile() and
// SendClientReportPhishingRequest() to be made on the UI thread.  We also
// expect that the calling thread runs a message loop and that there is a FILE
// thread running to execute asynchronous file operations.

#ifndef CHROME_BROWSER_SAFE_BROWSING_CLIENT_SIDE_DETECTION_SERVICE_H_
#define CHROME_BROWSER_SAFE_BROWSING_CLIENT_SIDE_DETECTION_SERVICE_H_
#pragma once

#include <map>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include "base/basictypes.h"
#include "base/callback.h"
#include "base/file_path.h"
#include "base/gtest_prod_util.h"
#include "base/memory/linked_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_callback_factory.h"
#include "base/memory/scoped_ptr.h"
#include "base/platform_file.h"
#include "base/task.h"
#include "base/time.h"
#include "chrome/common/net/url_fetcher.h"
#include "googleurl/src/gurl.h"
#include "net/base/net_util.h"

namespace net {
class URLRequestContextGetter;
class URLRequestStatus;
}  // namespace net

namespace safe_browsing {
class ClientPhishingRequest;

class ClientSideDetectionService : public URLFetcher::Delegate {
 public:
  typedef Callback1<base::PlatformFile>::Type OpenModelDoneCallback;

  typedef Callback2<GURL /* phishing URL */, bool /* is phishing */>::Type
      ClientReportPhishingRequestCallback;

  virtual ~ClientSideDetectionService();

  // Creates a client-side detection service and starts fetching the client-side
  // detection model if necessary.  The model will be stored in |model_path|.
  // The caller takes ownership of the object.  This function may return NULL.
  static ClientSideDetectionService* Create(
      const FilePath& model_path,
      net::URLRequestContextGetter* request_context_getter);

  // From the URLFetcher::Delegate interface.
  virtual void OnURLFetchComplete(const URLFetcher* source,
                                  const GURL& url,
                                  const net::URLRequestStatus& status,
                                  int response_code,
                                  const ResponseCookies& cookies,
                                  const std::string& data);

  // Gets the model file descriptor once the model is ready and stored
  // on disk.  If there was an error the callback is called and the
  // platform file is set to kInvalidPlatformFileValue. The
  // ClientSideDetectionService takes ownership of the |callback|.
  // The callback is always called after GetModelFile() returns and on the
  // same thread as GetModelFile() was called.
  void GetModelFile(OpenModelDoneCallback* callback);

  // Sends a request to the SafeBrowsing servers with the ClientPhishingRequest.
  // The URL scheme of the |url()| in the request should be HTTP.  This method
  // takes ownership of the |verdict| as well as the |callback| and calls the
  // the callback once the result has come back from the server or if an error
  // occurs during the fetch.  If an error occurs the phishing verdict will
  // always be false.  The callback is always called after
  // SendClientReportPhishingRequest() returns and on the same thread as
  // SendClientReportPhishingRequest() was called.
  virtual void SendClientReportPhishingRequest(
      ClientPhishingRequest* verdict,
      ClientReportPhishingRequestCallback* callback);

  // Returns true if the given IP address string falls within a private
  // (unroutable) network block.  Pages which are hosted on these IP addresses
  // are exempt from client-side phishing detection.  This is called by the
  // ClientSideDetectionHost prior to sending the renderer a
  // SafeBrowsingMsg_StartPhishingDetection IPC.
  //
  // ip_address should be a dotted IPv4 address, or an unbracketed IPv6
  // address.
  virtual bool IsPrivateIPAddress(const std::string& ip_address) const;

  // Returns true and sets is_phishing if url is in the cache and valid.
  virtual bool GetValidCachedResult(const GURL& url, bool* is_phishing);

  // Returns true if the url is in the cache.
  virtual bool IsInCache(const GURL& url);

  // Returns true if we have sent more than kMaxReportsPerInterval in the last
  // kReportsInterval.
  virtual bool OverReportLimit();

 protected:
  // Use Create() method to create an instance of this object.
  ClientSideDetectionService(
      const FilePath& model_path,
      net::URLRequestContextGetter* request_context_getter);

 private:
  friend class ClientSideDetectionServiceTest;

  enum ModelStatus {
    // It's unclear whether or not the model was already fetched.
    UNKNOWN_STATUS,
    // Model is fetched and is stored on disk.
    READY_STATUS,
    // Error occured during fetching or writing.
    ERROR_STATUS,
  };

  // CacheState holds all information necessary to respond to a caller without
  // actually making a HTTP request.
  struct CacheState {
    bool is_phishing;
    base::Time timestamp;

    CacheState(bool phish, base::Time time);
  };
  typedef std::map<GURL, linked_ptr<CacheState> > PhishingCache;

  // A tuple of (IP address block, prefix size) representing a private
  // IP address range.
  typedef std::pair<net::IPAddressNumber, size_t> AddressRange;

  static const char kClientReportPhishingUrl[];
  static const char kClientModelUrl[];
  static const int kMaxReportsPerInterval;
  static const base::TimeDelta kReportsInterval;
  static const base::TimeDelta kNegativeCacheInterval;
  static const base::TimeDelta kPositiveCacheInterval;

  // Sets the model status and invokes all the pending callbacks in
  // |open_callbacks_| with the current |model_file_| as parameter.
  void SetModelStatus(ModelStatus status);

  // Called once the initial open() of the model file is done.  If the file
  // exists we're done and we can call all the pending callbacks.  If the
  // file doesn't exist this method will asynchronously fetch the model
  // from the server by invoking StartFetchingModel().
  void OpenModelFileDone(base::PlatformFileError error_code,
                         base::PassPlatformFile file,
                         bool created);

  // Callback that is invoked once the attempt to create the model
  // file on disk is done.  If the file was created successfully we
  // start writing the model to disk (asynchronously).  Otherwise, we
  // give up and send an invalid platform file to all the pending callbacks.
  void CreateModelFileDone(base::PlatformFileError error_code,
                           base::PassPlatformFile file,
                           bool created);

  // Callback is invoked once we're done writing the model file to disk.
  // If everything went well then |model_file_| is a valid platform file
  // that can be sent to all the pending callbacks.  If an error occurs
  // we give up and send an invalid platform file to all the pending callbacks.
  void WriteModelFileDone(base::PlatformFileError error_code,
                          int bytes_written);

  // Helper function which closes the |model_file_| if necessary.
  void CloseModelFile();

  // Starts sending the request to the client-side detection frontends.
  // This method takes ownership of both pointers.
  void StartClientReportPhishingRequest(
      ClientPhishingRequest* verdict,
      ClientReportPhishingRequestCallback* callback);

  // Starts getting the model file.
  void StartGetModelFile(OpenModelDoneCallback* callback);

  // Called by OnURLFetchComplete to handle the response from fetching the
  // model.
  void HandleModelResponse(const URLFetcher* source,
                           const GURL& url,
                           const net::URLRequestStatus& status,
                           int response_code,
                           const ResponseCookies& cookies,
                           const std::string& data);

  // Called by OnURLFetchComplete to handle the server response from
  // sending the client-side phishing request.
  void HandlePhishingVerdict(const URLFetcher* source,
                             const GURL& url,
                             const net::URLRequestStatus& status,
                             int response_code,
                             const ResponseCookies& cookies,
                             const std::string& data);

  // Invalidate cache results which are no longer useful.
  void UpdateCache();

  // Get the number of phishing reports that we have sent over kReportsInterval
  int GetNumReports();

  // Initializes the |private_networks_| vector with the network blocks
  // that we consider non-public IP addresses.  Returns true on success.
  bool InitializePrivateNetworks();

  FilePath model_path_;
  ModelStatus model_status_;
  base::PlatformFile model_file_;
  scoped_ptr<URLFetcher> model_fetcher_;
  scoped_ptr<std::string> tmp_model_string_;
  std::vector<OpenModelDoneCallback*> open_callbacks_;

  // Map of client report phishing request to the corresponding callback that
  // has to be invoked when the request is done.
  struct ClientReportInfo;
  std::map<const URLFetcher*, ClientReportInfo*> client_phishing_reports_;

  // Cache of completed requests. Used to satisfy requests for the same urls
  // as long as the next request falls within our caching window (which is
  // determined by kNegativeCacheInterval and kPositiveCacheInterval). The
  // size of this cache is limited by kMaxReportsPerDay *
  // ceil(InDays(max(kNegativeCacheInterval, kPositiveCacheInterval))).
  // TODO(gcasto): Serialize this so that it doesn't reset on browser restart.
  PhishingCache cache_;

  // Timestamp of when we sent a phishing request. Used to limit the number
  // of phishing requests that we send in a day.
  // TODO(gcasto): Serialize this so that it doesn't reset on browser restart.
  std::queue<base::Time> phishing_report_times_;

  // Used to asynchronously call the callbacks for GetModelFile and
  // SendClientReportPhishingRequest.
  ScopedRunnableMethodFactory<ClientSideDetectionService> method_factory_;

  // The client-side detection service object (this) might go away before some
  // of the callbacks are done (e.g., asynchronous file operations).  The
  // callback factory will revoke all pending callbacks if this goes away to
  // avoid a crash.
  base::ScopedCallbackFactory<ClientSideDetectionService> callback_factory_;

  // The context we use to issue network requests.
  scoped_refptr<net::URLRequestContextGetter> request_context_getter_;

  // The network blocks that we consider private IP address ranges.
  std::vector<AddressRange> private_networks_;

  DISALLOW_COPY_AND_ASSIGN(ClientSideDetectionService);
};

}  // namepsace safe_browsing

#endif  // CHROME_BROWSER_SAFE_BROWSING_CLIENT_SIDE_DETECTION_SERVICE_H_
