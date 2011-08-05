// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_SERVICE_GAIA_SERVICE_GAIA_AUTHENTICATOR_H_
#define CHROME_SERVICE_GAIA_SERVICE_GAIA_AUTHENTICATOR_H_
#pragma once

#include <string>

#include "base/memory/ref_counted.h"
#include "base/synchronization/waitable_event.h"
#include "chrome/common/net/gaia/gaia_authenticator.h"
#include "content/common/url_fetcher.h"

namespace base {
class MessageLoopProxy;
}

// A GaiaAuthenticator implementation to be used in the service process (where
// we cannot rely on the existence of a Profile)
class ServiceGaiaAuthenticator
    : public base::RefCountedThreadSafe<ServiceGaiaAuthenticator>,
      public URLFetcher::Delegate,
      public gaia::GaiaAuthenticator {
 public:
  ServiceGaiaAuthenticator(const std::string& user_agent,
                           const std::string& service_id,
                           const std::string& gaia_url,
                           base::MessageLoopProxy* io_message_loop_proxy);
  virtual ~ServiceGaiaAuthenticator();

  // URLFetcher::Delegate implementation.
  virtual void OnURLFetchComplete(const URLFetcher *source,
                                  const GURL &url,
                                  const net::URLRequestStatus &status,
                                  int response_code,
                                  const net::ResponseCookies &cookies,
                                  const std::string &data);

 protected:
  // GaiaAuthenticator overrides.
  virtual bool Post(const GURL& url, const std::string& post_body,
                    unsigned long* response_code, std::string* response_body);
  virtual int GetBackoffDelaySeconds(int current_backoff_delay);

 private:
  void DoPost(const GURL& post_url, const std::string& post_body);

  base::WaitableEvent http_post_completed_;
  scoped_refptr<base::MessageLoopProxy> io_message_loop_proxy_;
  int http_response_code_;
  std::string response_data_;

  DISALLOW_COPY_AND_ASSIGN(ServiceGaiaAuthenticator);
};

#endif  // CHROME_SERVICE_GAIA_SERVICE_GAIA_AUTHENTICATOR_H_

