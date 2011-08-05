// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/net/sdch_dictionary_fetcher.h"

#include "base/compiler_specific.h"
#include "chrome/browser/profiles/profile.h"
#include "net/url_request/url_request_status.h"

SdchDictionaryFetcher::SdchDictionaryFetcher()
    : ALLOW_THIS_IN_INITIALIZER_LIST(method_factory_(this)),
      task_is_pending_(false) {
}

SdchDictionaryFetcher::~SdchDictionaryFetcher() {
}

// static
void SdchDictionaryFetcher::Shutdown() {
  net::SdchManager::Shutdown();
}

void SdchDictionaryFetcher::Schedule(const GURL& dictionary_url) {
  // Avoid pushing duplicate copy onto queue.  We may fetch this url again later
  // and get a different dictionary, but there is no reason to have it in the
  // queue twice at one time.
  if (!fetch_queue_.empty() && fetch_queue_.back() == dictionary_url) {
    net::SdchManager::SdchErrorRecovery(
        net::SdchManager::DICTIONARY_ALREADY_SCHEDULED_TO_DOWNLOAD);
    return;
  }
  if (attempted_load_.find(dictionary_url) != attempted_load_.end()) {
    net::SdchManager::SdchErrorRecovery(
        net::SdchManager::DICTIONARY_ALREADY_TRIED_TO_DOWNLOAD);
    return;
  }
  attempted_load_.insert(dictionary_url);
  fetch_queue_.push(dictionary_url);
  ScheduleDelayedRun();
}

void SdchDictionaryFetcher::ScheduleDelayedRun() {
  if (fetch_queue_.empty() || current_fetch_.get() || task_is_pending_)
    return;
  MessageLoop::current()->PostDelayedTask(FROM_HERE,
      method_factory_.NewRunnableMethod(&SdchDictionaryFetcher::StartFetching),
      kMsDelayFromRequestTillDownload);
  task_is_pending_ = true;
}

void SdchDictionaryFetcher::StartFetching() {
  DCHECK(task_is_pending_);
  task_is_pending_ = false;

  net::URLRequestContextGetter* context = Profile::GetDefaultRequestContext();
  if (!context) {
    // Shutdown in progress.
    // Simulate handling of all dictionary requests by clearing queue.
    while (!fetch_queue_.empty())
      fetch_queue_.pop();
    return;
  }

  current_fetch_.reset(new URLFetcher(fetch_queue_.front(), URLFetcher::GET,
                                      this));
  fetch_queue_.pop();
  current_fetch_->set_request_context(context);
  current_fetch_->Start();
}

void SdchDictionaryFetcher::OnURLFetchComplete(
    const URLFetcher* source,
    const GURL& url,
    const net::URLRequestStatus& status,
    int response_code,
    const net::ResponseCookies& cookies,
    const std::string& data) {
  if ((200 == response_code) &&
      (status.status() == net::URLRequestStatus::SUCCESS)) {
    net::SdchManager::Global()->AddSdchDictionary(data, url);
  }
  current_fetch_.reset(NULL);
  ScheduleDelayedRun();
}
