// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/safe_browsing/protocol_manager.h"

#ifndef NDEBUG
#include "base/base64.h"
#endif
#include "base/environment.h"
#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "base/rand_util.h"
#include "base/stl_util.h"
#include "base/string_util.h"
#include "base/stringprintf.h"
#include "base/timer.h"
#include "chrome/browser/safe_browsing/protocol_parser.h"
#include "chrome/browser/safe_browsing/safe_browsing_service.h"
#include "chrome/common/chrome_version_info.h"
#include "chrome/common/env_vars.h"
#include "content/public/browser/browser_thread.h"
#include "net/base/escape.h"
#include "net/base/load_flags.h"
#include "net/url_request/url_fetcher.h"
#include "net/url_request/url_request_context_getter.h"
#include "net/url_request/url_request_status.h"

using base::Time;
using base::TimeDelta;
using content::BrowserThread;

// Maximum time, in seconds, from start up before we must issue an update query.
static const int kSbTimerStartIntervalSec = 5 * 60;

// The maximum time, in seconds, to wait for a response to an update request.
static const int kSbMaxUpdateWaitSec = 30;

// Maximum back off multiplier.
static const int kSbMaxBackOff = 8;

// The default SBProtocolManagerFactory.
class SBProtocolManagerFactoryImpl : public SBProtocolManagerFactory {
 public:
  SBProtocolManagerFactoryImpl() { }
  virtual ~SBProtocolManagerFactoryImpl() { }
  virtual SafeBrowsingProtocolManager* CreateProtocolManager(
      SafeBrowsingService* sb_service,
      const std::string& client_name,
      net::URLRequestContextGetter* request_context_getter,
      const std::string& url_prefix,
      bool disable_auto_update) {
    return new SafeBrowsingProtocolManager(
        sb_service, client_name, request_context_getter,
        url_prefix, disable_auto_update);
  }
 private:
  DISALLOW_COPY_AND_ASSIGN(SBProtocolManagerFactoryImpl);
};

// SafeBrowsingProtocolManager implementation ----------------------------------

// static
SBProtocolManagerFactory* SafeBrowsingProtocolManager::factory_ = NULL;

// static
SafeBrowsingProtocolManager* SafeBrowsingProtocolManager::Create(
    SafeBrowsingService* sb_service,
    const std::string& client_name,
    net::URLRequestContextGetter* request_context_getter,
    const std::string& url_prefix,
    bool disable_auto_update) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  if (!factory_)
    factory_ = new SBProtocolManagerFactoryImpl();
  return factory_->CreateProtocolManager(sb_service, client_name,
                                         request_context_getter,
                                         url_prefix, disable_auto_update);
}

SafeBrowsingProtocolManager::SafeBrowsingProtocolManager(
    SafeBrowsingService* sb_service,
    const std::string& client_name,
    net::URLRequestContextGetter* request_context_getter,
    const std::string& url_prefix,
    bool disable_auto_update)
    : sb_service_(sb_service),
      request_type_(NO_REQUEST),
      update_error_count_(0),
      gethash_error_count_(0),
      update_back_off_mult_(1),
      gethash_back_off_mult_(1),
      next_update_sec_(-1),
      update_state_(FIRST_REQUEST),
      chunk_pending_to_write_(false),
      update_size_(0),
      client_name_(client_name),
      request_context_getter_(request_context_getter),
      url_prefix_(url_prefix),
      disable_auto_update_(disable_auto_update) {
  DCHECK(!url_prefix_.empty());

  // Set the backoff multiplier fuzz to a random value between 0 and 1.
  back_off_fuzz_ = static_cast<float>(base::RandDouble());
  // The first update must happen between 1-5 minutes of start up.
  next_update_sec_ = base::RandInt(60, kSbTimerStartIntervalSec);

  chrome::VersionInfo version_info;
  if (!version_info.is_valid() || version_info.Version().empty())
    version_ = "0.1";
  else
    version_ = version_info.Version();
}

// static
void SafeBrowsingProtocolManager::RecordGetHashResult(
    bool is_download, ResultType result_type) {
  if (is_download) {
    UMA_HISTOGRAM_ENUMERATION("SB2.GetHashResultDownload", result_type,
                              GET_HASH_RESULT_MAX);
  } else {
    UMA_HISTOGRAM_ENUMERATION("SB2.GetHashResult", result_type,
                              GET_HASH_RESULT_MAX);
  }
}

SafeBrowsingProtocolManager::~SafeBrowsingProtocolManager() {
  // Delete in-progress SafeBrowsing requests.
  STLDeleteContainerPairFirstPointers(hash_requests_.begin(),
                                      hash_requests_.end());
  hash_requests_.clear();

  // Delete in-progress safebrowsing reports (hits and details).
  STLDeleteContainerPointers(safebrowsing_reports_.begin(),
                             safebrowsing_reports_.end());
  safebrowsing_reports_.clear();
}

// Public API used by the SafeBrowsingService ----------------------------------

// We can only have one update or chunk request outstanding, but there may be
// multiple GetHash requests pending since we don't want to serialize them and
// slow down the user.
void SafeBrowsingProtocolManager::GetFullHash(
    SafeBrowsingService::SafeBrowsingCheck* check,
    const std::vector<SBPrefix>& prefixes) {
  // If we are in GetHash backoff, we need to check if we're past the next
  // allowed time. If we are, we can proceed with the request. If not, we are
  // required to return empty results (i.e. treat the page as safe).
  if (gethash_error_count_ && Time::Now() <= next_gethash_time_) {
    std::vector<SBFullHashResult> full_hashes;
    sb_service_->HandleGetHashResults(check, full_hashes, false);
    return;
  }
  GURL gethash_url = GetHashUrl();
  net::URLFetcher* fetcher = net::URLFetcher::Create(
      gethash_url, net::URLFetcher::POST, this);
  hash_requests_[fetcher] = check;

  std::string get_hash;
  SafeBrowsingProtocolParser parser;
  parser.FormatGetHash(prefixes, &get_hash);

  fetcher->SetLoadFlags(net::LOAD_DISABLE_CACHE);
  fetcher->SetRequestContext(request_context_getter_);
  fetcher->SetUploadData("text/plain", get_hash);
  fetcher->Start();
}

void SafeBrowsingProtocolManager::GetNextUpdate() {
  if (!request_.get())
    IssueUpdateRequest();
}

// net::URLFetcherDelegate implementation ----------------------------------

// All SafeBrowsing request responses are handled here.
// TODO(paulg): Clarify with the SafeBrowsing team whether a failed parse of a
//              chunk should retry the download and parse of that chunk (and
//              what back off / how many times to try), and if that effects the
//              update back off. For now, a failed parse of the chunk means we
//              drop it. This isn't so bad because the next UPDATE_REQUEST we
//              do will report all the chunks we have. If that chunk is still
//              required, the SafeBrowsing servers will tell us to get it again.
void SafeBrowsingProtocolManager::OnURLFetchComplete(
    const net::URLFetcher* source) {
  scoped_ptr<const net::URLFetcher> fetcher;
  bool parsed_ok = true;
  bool must_back_off = false;  // Reduce SafeBrowsing service query frequency.

  // See if this is a safebrowsing report fetcher. We don't take any action for
  // the response to those.
  std::set<const net::URLFetcher*>::iterator sit =
      safebrowsing_reports_.find(source);
  if (sit != safebrowsing_reports_.end()) {
    const net::URLFetcher* report = *sit;
    safebrowsing_reports_.erase(sit);
    delete report;
    return;
  }

  HashRequests::iterator it = hash_requests_.find(source);
  if (it != hash_requests_.end()) {
    // GetHash response.
    fetcher.reset(it->first);
    SafeBrowsingService::SafeBrowsingCheck* check = it->second;
    std::vector<SBFullHashResult> full_hashes;
    bool can_cache = false;
    if (source->GetStatus().is_success() &&
        (source->GetResponseCode() == 200 ||
         source->GetResponseCode() == 204)) {
      // For tracking our GetHash false positive (204) rate, compared to real
      // (200) responses.
      if (source->GetResponseCode() == 200)
        RecordGetHashResult(check->is_download, GET_HASH_STATUS_200);
      else
        RecordGetHashResult(check->is_download, GET_HASH_STATUS_204);
      can_cache = true;
      gethash_error_count_ = 0;
      gethash_back_off_mult_ = 1;
      SafeBrowsingProtocolParser parser;
      std::string data;
      source->GetResponseAsString(&data);
      parsed_ok = parser.ParseGetHash(
          data.data(),
          static_cast<int>(data.length()),
          &full_hashes);
      if (!parsed_ok) {
        // If we fail to parse it, we must still inform the SafeBrowsingService
        // so that it doesn't hold up the user's request indefinitely. Not sure
        // what to do at that point though!
        full_hashes.clear();
      }
    } else {
      HandleGetHashError(Time::Now());
      if (source->GetStatus().status() == net::URLRequestStatus::FAILED) {
        VLOG(1) << "SafeBrowsing GetHash request for: " << source->GetURL()
                << " failed with error: " << source->GetStatus().error();
      } else {
        VLOG(1) << "SafeBrowsing GetHash request for: " << source->GetURL()
                << " failed with error: " << source->GetResponseCode();
      }
    }

    // Call back the SafeBrowsingService with full_hashes, even if there was a
    // parse error or an error response code (in which case full_hashes will be
    // empty). We can't block the user regardless of the error status.
    sb_service_->HandleGetHashResults(check, full_hashes, can_cache);

    hash_requests_.erase(it);
  } else {
    // Update or chunk response.
    fetcher.reset(request_.release());

    if (request_type_ == UPDATE_REQUEST) {
      if (!fetcher.get()) {
        // We've timed out waiting for an update response, so we've cancelled
        // the update request and scheduled a new one. Ignore this response.
        return;
      }

      // Cancel the update response timeout now that we have the response.
      update_timer_.Stop();
    }

    if (source->GetStatus().is_success() && source->GetResponseCode() == 200) {
      // We have data from the SafeBrowsing service.
      std::string data;
      source->GetResponseAsString(&data);
      parsed_ok = HandleServiceResponse(
          source->GetURL(), data.data(), static_cast<int>(data.length()));
      if (!parsed_ok) {
        VLOG(1) << "SafeBrowsing request for: " << source->GetURL()
                << " failed parse.";
        must_back_off = true;
        chunk_request_urls_.clear();
        UpdateFinished(false);
      }

      switch (request_type_) {
        case CHUNK_REQUEST:
          if (parsed_ok)
            chunk_request_urls_.pop_front();
          break;
        case UPDATE_REQUEST:
          if (chunk_request_urls_.empty() && parsed_ok) {
            // We are up to date since the servers gave us nothing new, so we
            // are done with this update cycle.
            UpdateFinished(true);
          }
          break;
        default:
          NOTREACHED();
          break;
      }
    } else {
      // The SafeBrowsing service error, or very bad response code: back off.
      must_back_off = true;
      if (request_type_ == CHUNK_REQUEST)
        chunk_request_urls_.clear();
      UpdateFinished(false);
      if (source->GetStatus().status() == net::URLRequestStatus::FAILED) {
        VLOG(1) << "SafeBrowsing request for: " << source->GetURL()
                << " failed with error: " << source->GetStatus().error();
      } else {
        VLOG(1) << "SafeBrowsing request for: " << source->GetURL()
                << " failed with error: " << source->GetResponseCode();
      }
    }
  }

  // Schedule a new update request if we've finished retrieving all the chunks
  // from the previous update. We treat the update request and the chunk URLs it
  // contains as an atomic unit as far as back off is concerned.
  if (chunk_request_urls_.empty() &&
      (request_type_ == CHUNK_REQUEST || request_type_ == UPDATE_REQUEST))
    ScheduleNextUpdate(must_back_off);

  // Get the next chunk if available.
  IssueChunkRequest();
}

bool SafeBrowsingProtocolManager::HandleServiceResponse(const GURL& url,
                                                        const char* data,
                                                        int length) {
  SafeBrowsingProtocolParser parser;

  switch (request_type_) {
    case UPDATE_REQUEST: {
      int next_update_sec = -1;
      bool reset = false;
      scoped_ptr<std::vector<SBChunkDelete> > chunk_deletes(
          new std::vector<SBChunkDelete>);
      std::vector<ChunkUrl> chunk_urls;
      if (!parser.ParseUpdate(data, length, &next_update_sec,
                              &reset, chunk_deletes.get(), &chunk_urls)) {
        return false;
      }

      last_update_ = Time::Now();

      if (update_state_ == FIRST_REQUEST)
        update_state_ = SECOND_REQUEST;
      else if (update_state_ == SECOND_REQUEST)
        update_state_ = NORMAL_REQUEST;

      // New time for the next update.
      if (next_update_sec > 0) {
        next_update_sec_ = next_update_sec;
      } else if (update_state_ == SECOND_REQUEST) {
        next_update_sec_ = base::RandInt(15 * 60, 45 * 60);
      }

      // New chunks to download.
      if (!chunk_urls.empty()) {
        UMA_HISTOGRAM_COUNTS("SB2.UpdateUrls", chunk_urls.size());
        for (size_t i = 0; i < chunk_urls.size(); ++i)
          chunk_request_urls_.push_back(chunk_urls[i]);
      }

      // Handle the case were the SafeBrowsing service tells us to dump our
      // database.
      if (reset) {
        sb_service_->ResetDatabase();
        return true;
      }

      // Chunks to delete from our storage.  Pass ownership of
      // |chunk_deletes|.
      if (!chunk_deletes->empty())
        sb_service_->HandleChunkDelete(chunk_deletes.release());

      break;
    }
    case CHUNK_REQUEST: {
      UMA_HISTOGRAM_TIMES("SB2.ChunkRequest",
                          base::Time::Now() - chunk_request_start_);

      const ChunkUrl chunk_url = chunk_request_urls_.front();
      scoped_ptr<SBChunkList> chunks(new SBChunkList);
      UMA_HISTOGRAM_COUNTS("SB2.ChunkSize", length);
      update_size_ += length;
      if (!parser.ParseChunk(chunk_url.list_name, data, length,
                             chunks.get())) {
#ifndef NDEBUG
        std::string data_str;
        data_str.assign(data, length);
        std::string encoded_chunk;
        base::Base64Encode(data_str, &encoded_chunk);
        VLOG(1) << "ParseChunk error for chunk: " << chunk_url.url
                << ", Base64Encode(data): " << encoded_chunk
                << ", length: " << length;
#endif
        return false;
      }

      // Chunks to add to storage.  Pass ownership of |chunks|.
      if (!chunks->empty()) {
        chunk_pending_to_write_ = true;
        sb_service_->HandleChunk(chunk_url.list_name, chunks.release());
      }

      break;
    }

    default:
      return false;
  }

  return true;
}

void SafeBrowsingProtocolManager::Initialize() {
  // Don't want to hit the safe browsing servers on build/chrome bots.
  scoped_ptr<base::Environment> env(base::Environment::Create());
  if (env->HasVar(env_vars::kHeadless))
    return;

  ScheduleNextUpdate(false /* no back off */);
}

void SafeBrowsingProtocolManager::ScheduleNextUpdate(bool back_off) {
  DCHECK_GT(next_update_sec_, 0);

  if (disable_auto_update_) {
    // Unschedule any current timer.
    update_timer_.Stop();
    return;
  }
  // Reschedule with the new update.
  const int next_update = GetNextUpdateTime(back_off);
  ForceScheduleNextUpdate(next_update);
}

void SafeBrowsingProtocolManager::ForceScheduleNextUpdate(
    const int next_update_msec) {
  DCHECK_GE(next_update_msec, 0);
  // Unschedule any current timer.
  update_timer_.Stop();
  update_timer_.Start(FROM_HERE, TimeDelta::FromMilliseconds(next_update_msec),
                      this, &SafeBrowsingProtocolManager::GetNextUpdate);
}

// According to section 5 of the SafeBrowsing protocol specification, we must
// back off after a certain number of errors. We only change 'next_update_sec_'
// when we receive a response from the SafeBrowsing service.
int SafeBrowsingProtocolManager::GetNextUpdateTime(bool back_off) {
  int next = next_update_sec_;
  if (back_off) {
    next = GetNextBackOffTime(&update_error_count_, &update_back_off_mult_);
  } else {
    // Successful response means error reset.
    update_error_count_ = 0;
    update_back_off_mult_ = 1;
  }
  return next * 1000;  // milliseconds
}

int SafeBrowsingProtocolManager::GetNextBackOffTime(int* error_count,
                                                    int* multiplier) {
  DCHECK(multiplier && error_count);
  (*error_count)++;
  if (*error_count > 1 && *error_count < 6) {
    int next = static_cast<int>(*multiplier * (1 + back_off_fuzz_) * 30 * 60);
    *multiplier *= 2;
    if (*multiplier > kSbMaxBackOff)
      *multiplier = kSbMaxBackOff;
    return next;
  }

  if (*error_count >= 6)
    return 60 * 60 * 8;  // 8 hours

  return 60;  // 1 minute
}

// This request requires getting a list of all the chunks for each list from the
// database asynchronously. The request will be issued when we're called back in
// OnGetChunksComplete.
// TODO(paulg): We should get this at start up and maintain a ChunkRange cache
//              to avoid hitting the database with each update request. On the
//              otherhand, this request will only occur ~20-30 minutes so there
//              isn't that much overhead. Measure!
void SafeBrowsingProtocolManager::IssueUpdateRequest() {
  request_type_ = UPDATE_REQUEST;
  sb_service_->UpdateStarted();
}

void SafeBrowsingProtocolManager::IssueChunkRequest() {
  // We are only allowed to have one request outstanding at any time.  Also,
  // don't get the next url until the previous one has been written to disk so
  // that we don't use too much memory.
  if (request_.get() || chunk_request_urls_.empty() || chunk_pending_to_write_)
    return;

  ChunkUrl next_chunk = chunk_request_urls_.front();
  DCHECK(!next_chunk.url.empty());
  GURL chunk_url = NextChunkUrl(next_chunk.url);
  request_type_ = CHUNK_REQUEST;
  request_.reset(net::URLFetcher::Create(
      chunk_url, net::URLFetcher::GET, this));
  request_->SetLoadFlags(net::LOAD_DISABLE_CACHE);
  request_->SetRequestContext(request_context_getter_);
  chunk_request_start_ = base::Time::Now();
  request_->Start();
}

void SafeBrowsingProtocolManager::OnGetChunksComplete(
    const std::vector<SBListChunkRanges>& lists, bool database_error) {
  DCHECK_EQ(request_type_, UPDATE_REQUEST);
  if (database_error) {
    UpdateFinished(false);
    ScheduleNextUpdate(false);
    return;
  }

  // Format our stored chunks:
  std::string list_data;
  bool found_malware = false;
  bool found_phishing = false;
  for (size_t i = 0; i < lists.size(); ++i) {
    list_data.append(FormatList(lists[i]));
    if (lists[i].name == safe_browsing_util::kPhishingList)
      found_phishing = true;

    if (lists[i].name == safe_browsing_util::kMalwareList)
      found_malware = true;
  }

  // If we have an empty database, let the server know we want data for these
  // lists.
  if (!found_phishing)
    list_data.append(FormatList(
        SBListChunkRanges(safe_browsing_util::kPhishingList)));

  if (!found_malware)
    list_data.append(FormatList(
        SBListChunkRanges(safe_browsing_util::kMalwareList)));

  GURL update_url = UpdateUrl();
  request_.reset(net::URLFetcher::Create(
      update_url, net::URLFetcher::POST, this));
  request_->SetLoadFlags(net::LOAD_DISABLE_CACHE);
  request_->SetRequestContext(request_context_getter_);
  request_->SetUploadData("text/plain", list_data);
  request_->Start();

  // Begin the update request timeout.
  update_timer_.Start(FROM_HERE, TimeDelta::FromSeconds(kSbMaxUpdateWaitSec),
                      this,
                      &SafeBrowsingProtocolManager::UpdateResponseTimeout);
}

// If we haven't heard back from the server with an update response, this method
// will run. Close the current update session and schedule another update.
void SafeBrowsingProtocolManager::UpdateResponseTimeout() {
  DCHECK_EQ(request_type_, UPDATE_REQUEST);
  request_.reset();
  UpdateFinished(false);
  ScheduleNextUpdate(true);
}

void SafeBrowsingProtocolManager::OnChunkInserted() {
  chunk_pending_to_write_ = false;

  if (chunk_request_urls_.empty()) {
    UMA_HISTOGRAM_LONG_TIMES("SB2.Update", Time::Now() - last_update_);
    UpdateFinished(true);
  } else {
    IssueChunkRequest();
  }
}

// Sends a SafeBrowsing "hit" for UMA users.
void SafeBrowsingProtocolManager::ReportSafeBrowsingHit(
    const GURL& malicious_url,
    const GURL& page_url,
    const GURL& referrer_url,
    bool is_subresource,
    SafeBrowsingService::UrlCheckResult threat_type,
    const std::string& post_data) {
  GURL report_url = SafeBrowsingHitUrl(malicious_url, page_url,
                                       referrer_url, is_subresource,
                                       threat_type);
  net::URLFetcher* report = net::URLFetcher::Create(
      report_url,
      post_data.empty() ? net::URLFetcher::GET : net::URLFetcher::POST,
      this);
  report->SetLoadFlags(net::LOAD_DISABLE_CACHE);
  report->SetRequestContext(request_context_getter_);
  if (!post_data.empty())
    report->SetUploadData("text/plain", post_data);
  report->Start();
  safebrowsing_reports_.insert(report);
}

// Sends malware details for users who opt-in.
void SafeBrowsingProtocolManager::ReportMalwareDetails(
    const std::string& report) {
  GURL report_url = MalwareDetailsUrl();
  net::URLFetcher* fetcher = net::URLFetcher::Create(
      report_url, net::URLFetcher::POST, this);
  fetcher->SetLoadFlags(net::LOAD_DISABLE_CACHE);
  fetcher->SetRequestContext(request_context_getter_);
  fetcher->SetUploadData("application/octet-stream", report);
  // Don't try too hard to send reports on failures.
  fetcher->SetAutomaticallyRetryOn5xx(false);
  fetcher->Start();
  safebrowsing_reports_.insert(fetcher);
}


// static
std::string SafeBrowsingProtocolManager::FormatList(
    const SBListChunkRanges& list) {
  std::string formatted_results;
  formatted_results.append(list.name);
  formatted_results.append(";");
  if (!list.adds.empty()) {
    formatted_results.append("a:" + list.adds);
    if (!list.subs.empty())
      formatted_results.append(":");
  }
  if (!list.subs.empty()) {
    formatted_results.append("s:" + list.subs);
  }
  formatted_results.append("\n");

  return formatted_results;
}

void SafeBrowsingProtocolManager::HandleGetHashError(const Time& now) {
  int next = GetNextBackOffTime(&gethash_error_count_, &gethash_back_off_mult_);
  next_gethash_time_ = now + TimeDelta::FromSeconds(next);
}

void SafeBrowsingProtocolManager::UpdateFinished(bool success) {
  UMA_HISTOGRAM_COUNTS("SB2.UpdateSize", update_size_);
  update_size_ = 0;
  sb_service_->UpdateFinished(success);
}

std::string SafeBrowsingProtocolManager::ComposeUrl(
    const std::string& prefix, const std::string& method,
    const std::string& client_name, const std::string& version,
    const std::string& additional_query) {
  DCHECK(!prefix.empty() && !method.empty() &&
         !client_name.empty() && !version.empty());
  std::string url = base::StringPrintf("%s/%s?client=%s&appver=%s&pver=2.2",
                                       prefix.c_str(), method.c_str(),
                                       client_name.c_str(), version.c_str());
  if (!additional_query.empty()) {
    DCHECK(url.find("?") != std::string::npos);
    url.append("&");
    url.append(additional_query);
  }
  return url;
}

GURL SafeBrowsingProtocolManager::UpdateUrl() const {
  return GURL(ComposeUrl(url_prefix_, "downloads", client_name_, version_,
                         additional_query_));
}

GURL SafeBrowsingProtocolManager::GetHashUrl() const {
  return GURL(ComposeUrl(url_prefix_, "gethash", client_name_, version_,
                         additional_query_));
}

GURL SafeBrowsingProtocolManager::SafeBrowsingHitUrl(
    const GURL& malicious_url, const GURL& page_url,
    const GURL& referrer_url, bool is_subresource,
    SafeBrowsingService::UrlCheckResult threat_type) const {
  DCHECK(threat_type == SafeBrowsingService::URL_MALWARE ||
         threat_type == SafeBrowsingService::URL_PHISHING ||
         threat_type == SafeBrowsingService::BINARY_MALWARE_URL ||
         threat_type == SafeBrowsingService::BINARY_MALWARE_HASH ||
         threat_type == SafeBrowsingService::CLIENT_SIDE_PHISHING_URL);
  std::string url = ComposeUrl(url_prefix_, "report", client_name_,
                               version_, additional_query_);
  std::string threat_list = "none";
  switch (threat_type) {
    case SafeBrowsingService::URL_MALWARE:
      threat_list = "malblhit";
      break;
    case SafeBrowsingService::URL_PHISHING:
      threat_list = "phishblhit";
      break;
    case SafeBrowsingService::BINARY_MALWARE_URL:
      threat_list = "binurlhit";
      break;
    case SafeBrowsingService::BINARY_MALWARE_HASH:
      threat_list = "binhashhit";
      break;
    case SafeBrowsingService::CLIENT_SIDE_PHISHING_URL:
      threat_list = "phishcsdhit";
      break;
    default:
      NOTREACHED();
  }
  return GURL(base::StringPrintf("%s&evts=%s&evtd=%s&evtr=%s&evhr=%s&evtb=%d",
      url.c_str(), threat_list.c_str(),
      net::EscapeQueryParamValue(malicious_url.spec(), true).c_str(),
      net::EscapeQueryParamValue(page_url.spec(), true).c_str(),
      net::EscapeQueryParamValue(referrer_url.spec(), true).c_str(),
      is_subresource));
}

GURL SafeBrowsingProtocolManager::MalwareDetailsUrl() const {
  std::string url = base::StringPrintf(
          "%s/clientreport/malware?client=%s&appver=%s&pver=1.0",
          url_prefix_.c_str(),
          client_name_.c_str(),
          version_.c_str());
  return GURL(url);
}

GURL SafeBrowsingProtocolManager::NextChunkUrl(const std::string& url) const {
  std::string next_url;
  if (!StartsWithASCII(url, "http://", false) &&
      !StartsWithASCII(url, "https://", false)) {
    // Use https if we updated via https, otherwise http (useful for testing).
    if (StartsWithASCII(url_prefix_, "https://", false))
      next_url.append("https://");
    else
      next_url.append("http://");
    next_url.append(url);
  } else {
    next_url = url;
  }
  if (!additional_query_.empty()) {
    if (next_url.find("?") != std::string::npos) {
      next_url.append("&");
    } else {
      next_url.append("?");
    }
    next_url.append(additional_query_);
  }
  return GURL(next_url);
}
