// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/safe_browsing/client_side_detection_service.h"

#include "base/command_line.h"
#include "base/file_path.h"
#include "base/file_util_proxy.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "base/metrics/histogram.h"
#include "base/platform_file.h"
#include "base/stl_util-inl.h"
#include "base/task.h"
#include "base/time.h"
#include "chrome/common/net/http_return.h"
#include "chrome/common/safe_browsing/csd.pb.h"
#include "chrome/common/safe_browsing/safebrowsing_messages.h"
#include "content/browser/browser_thread.h"
#include "content/browser/renderer_host/render_process_host.h"
#include "content/common/notification_service.h"
#include "content/common/url_fetcher.h"
#include "googleurl/src/gurl.h"
#include "ipc/ipc_platform_file.h"
#include "net/base/load_flags.h"
#include "net/url_request/url_request_context_getter.h"
#include "net/url_request/url_request_status.h"

#if defined(OS_MACOSX)
#include "base/mac/mac_util.h"
#endif

namespace safe_browsing {

const int ClientSideDetectionService::kMaxReportsPerInterval = 3;

const base::TimeDelta ClientSideDetectionService::kReportsInterval =
    base::TimeDelta::FromDays(1);
const base::TimeDelta ClientSideDetectionService::kNegativeCacheInterval =
    base::TimeDelta::FromDays(1);
const base::TimeDelta ClientSideDetectionService::kPositiveCacheInterval =
    base::TimeDelta::FromMinutes(30);

const char ClientSideDetectionService::kClientReportPhishingUrl[] =
    "https://sb-ssl.google.com/safebrowsing/clientreport/phishing";
// Note: when updatng the model version, don't forget to change the filename
// in chrome/common/chrome_constants.cc as well, or else existing users won't
// download the new model.
//
// TODO(bryner): add version metadata so that clients can download new models
// without needing a new model filename.
const char ClientSideDetectionService::kClientModelUrl[] =
    "https://ssl.gstatic.com/safebrowsing/csd/client_model_v1.pb";

struct ClientSideDetectionService::ClientReportInfo {
  scoped_ptr<ClientReportPhishingRequestCallback> callback;
  GURL phishing_url;
};

ClientSideDetectionService::CacheState::CacheState(bool phish, base::Time time)
    : is_phishing(phish),
      timestamp(time) {}

ClientSideDetectionService::ClientSideDetectionService(
    const FilePath& model_path,
    net::URLRequestContextGetter* request_context_getter)
    : model_path_(model_path),
      model_status_(UNKNOWN_STATUS),
      model_file_(base::kInvalidPlatformFileValue),
      ALLOW_THIS_IN_INITIALIZER_LIST(method_factory_(this)),
      ALLOW_THIS_IN_INITIALIZER_LIST(callback_factory_(this)),
      request_context_getter_(request_context_getter) {
  registrar_.Add(this, NotificationType::RENDERER_PROCESS_CREATED,
                 NotificationService::AllSources());
}

ClientSideDetectionService::~ClientSideDetectionService() {
  method_factory_.RevokeAll();
  STLDeleteContainerPairPointers(client_phishing_reports_.begin(),
                                 client_phishing_reports_.end());
  client_phishing_reports_.clear();
  CloseModelFile();
}

/* static */
ClientSideDetectionService* ClientSideDetectionService::Create(
    const FilePath& model_path,
    net::URLRequestContextGetter* request_context_getter) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  scoped_ptr<ClientSideDetectionService> service(
      new ClientSideDetectionService(model_path, request_context_getter));
  if (!service->InitializePrivateNetworks()) {
    UMA_HISTOGRAM_COUNTS("SBClientPhishing.InitPrivateNetworksFailed", 1);
    return NULL;
  }

  // We try to open the model file right away and start fetching it if
  // it does not already exist on disk.
  base::FileUtilProxy::CreateOrOpenCallback* cb =
      service.get()->callback_factory_.NewCallback(
          &ClientSideDetectionService::OpenModelFileDone);
  if (!base::FileUtilProxy::CreateOrOpen(
          BrowserThread::GetMessageLoopProxyForThread(BrowserThread::FILE),
          model_path,
          base::PLATFORM_FILE_OPEN | base::PLATFORM_FILE_READ,
          cb)) {
    delete cb;
    return NULL;
  }

  // Delete the previous-version model file.
  // TODO(bryner): Remove this for M14.
  base::FileUtilProxy::Delete(
      BrowserThread::GetMessageLoopProxyForThread(BrowserThread::FILE),
      model_path.DirName().AppendASCII("Safe Browsing Phishing Model"),
      false /* not recursive */,
      NULL /* not interested in result */);
  return service.release();
}

void ClientSideDetectionService::SendClientReportPhishingRequest(
    ClientPhishingRequest* verdict,
    ClientReportPhishingRequestCallback* callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  MessageLoop::current()->PostTask(
      FROM_HERE,
      method_factory_.NewRunnableMethod(
          &ClientSideDetectionService::StartClientReportPhishingRequest,
          verdict, callback));
}

bool ClientSideDetectionService::IsPrivateIPAddress(
    const std::string& ip_address) const {
  net::IPAddressNumber ip_number;
  if (!net::ParseIPLiteralToNumber(ip_address, &ip_number)) {
    DLOG(WARNING) << "Unable to parse IP address: " << ip_address;
    // Err on the side of safety and assume this might be private.
    return true;
  }

  for (std::vector<AddressRange>::const_iterator it =
           private_networks_.begin();
       it != private_networks_.end(); ++it) {
    if (net::IPNumberMatchesPrefix(ip_number, it->first, it->second)) {
      return true;
    }
  }
  return false;
}

void ClientSideDetectionService::OnURLFetchComplete(
    const URLFetcher* source,
    const GURL& url,
    const net::URLRequestStatus& status,
    int response_code,
    const net::ResponseCookies& cookies,
    const std::string& data) {
  if (source == model_fetcher_.get()) {
    HandleModelResponse(source, url, status, response_code, cookies, data);
  } else if (client_phishing_reports_.find(source) !=
             client_phishing_reports_.end()) {
    HandlePhishingVerdict(source, url, status, response_code, cookies, data);
  } else {
    NOTREACHED();
  }
}

void ClientSideDetectionService::Observe(NotificationType type,
                                         const NotificationSource& source,
                                         const NotificationDetails& details) {
  DCHECK(type == NotificationType::RENDERER_PROCESS_CREATED);
  if (model_status_ == UNKNOWN_STATUS) {
    // The model isn't ready.  When it's known, we'll call all renderers.
    return;
  }

  RenderProcessHost* process = Source<RenderProcessHost>(source).ptr();
  SendModelToProcess(process);
}

void ClientSideDetectionService::SendModelToProcess(
    RenderProcessHost* process) {
  if (model_file_ == base::kInvalidPlatformFileValue)
    return;

  IPC::PlatformFileForTransit file;
#if defined(OS_POSIX)
  file = base::FileDescriptor(model_file_, false);
#elif defined(OS_WIN)
  ::DuplicateHandle(::GetCurrentProcess(), model_file_, process->GetHandle(),
                    &file, 0, false, DUPLICATE_SAME_ACCESS);
#endif
  process->Send(new SafeBrowsingMsg_SetPhishingModel(file));
}

void ClientSideDetectionService::SetModelStatus(ModelStatus status) {
  DCHECK_NE(READY_STATUS, model_status_);
  model_status_ = status;

  for (RenderProcessHost::iterator i(RenderProcessHost::AllHostsIterator());
       !i.IsAtEnd(); i.Advance()) {
    RenderProcessHost* process = i.GetCurrentValue();
    if (process->GetHandle())
      SendModelToProcess(process);
  }
}

void ClientSideDetectionService::OpenModelFileDone(
    base::PlatformFileError error_code,
    base::PassPlatformFile file,
    bool created) {
  DCHECK(!created);
  if (base::PLATFORM_FILE_OK == error_code) {
    // The model file already exists.  There is no need to fetch the model.
    model_file_ = file.ReleaseValue();
    SetModelStatus(READY_STATUS);
#if defined(OS_MACOSX)
    base::mac::SetFileBackupExclusion(model_path_);
#endif
  } else if (base::PLATFORM_FILE_ERROR_NOT_FOUND == error_code) {
    // We need to fetch the model since it does not exist yet.
    model_fetcher_.reset(URLFetcher::Create(0 /* ID is not used */,
                                            GURL(kClientModelUrl),
                                            URLFetcher::GET,
                                            this));
    model_fetcher_->set_request_context(request_context_getter_.get());
    model_fetcher_->Start();
  } else {
    // It is not clear what we should do in this case.  For now we simply fail.
    // Hopefully, we'll be able to read the model during the next browser
    // restart.
    SetModelStatus(ERROR_STATUS);
  }
}

void ClientSideDetectionService::CreateModelFileDone(
    base::PlatformFileError error_code,
    base::PassPlatformFile file,
    bool created) {
  model_file_ = file.ReleaseValue();
  base::FileUtilProxy::WriteCallback* cb = callback_factory_.NewCallback(
      &ClientSideDetectionService::WriteModelFileDone);
  if (!created ||
      base::PLATFORM_FILE_OK != error_code ||
      !base::FileUtilProxy::Write(
          BrowserThread::GetMessageLoopProxyForThread(BrowserThread::FILE),
          model_file_,
          0 /* offset */, tmp_model_string_->data(), tmp_model_string_->size(),
          cb)) {
    delete cb;
    // An error occurred somewhere.  We close the model file if necessary and
    // then run all the pending callbacks giving them an invalid model file.
    CloseModelFile();
    SetModelStatus(ERROR_STATUS);
#if defined(OS_MACOSX)
  } else {
    base::mac::SetFileBackupExclusion(model_path_);
#endif
  }
}

void ClientSideDetectionService::WriteModelFileDone(
    base::PlatformFileError error_code,
    int bytes_written) {
  if (base::PLATFORM_FILE_OK == error_code) {
    SetModelStatus(READY_STATUS);
  } else {
    // TODO(noelutz): maybe we should retry writing the model since we
    // did already fetch the model?
    CloseModelFile();
    SetModelStatus(ERROR_STATUS);
  }
  // Delete the model string that we kept around while we were writing the
  // string to disk - we don't need it anymore.
  tmp_model_string_.reset();
}

void ClientSideDetectionService::CloseModelFile() {
  if (model_file_ != base::kInvalidPlatformFileValue) {
    base::FileUtilProxy::Close(
        BrowserThread::GetMessageLoopProxyForThread(BrowserThread::FILE),
        model_file_,
        NULL);
  }
  model_file_ = base::kInvalidPlatformFileValue;
}

void ClientSideDetectionService::StartClientReportPhishingRequest(
    ClientPhishingRequest* verdict,
    ClientReportPhishingRequestCallback* callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  scoped_ptr<ClientPhishingRequest> request(verdict);
  scoped_ptr<ClientReportPhishingRequestCallback> cb(callback);

  std::string request_data;
  if (!request->SerializeToString(&request_data)) {
    UMA_HISTOGRAM_COUNTS("SBClientPhishing.RequestNotSerialized", 1);
    VLOG(1) << "Unable to serialize the CSD request. Proto file changed?";
    cb->Run(GURL(request->url()), false);
    return;
  }

  URLFetcher* fetcher = URLFetcher::Create(0 /* ID is not used */,
                                           GURL(kClientReportPhishingUrl),
                                           URLFetcher::POST,
                                           this);

  // Remember which callback and URL correspond to the current fetcher object.
  ClientReportInfo* info = new ClientReportInfo;
  info->callback.swap(cb);  // takes ownership of the callback.
  info->phishing_url = GURL(request->url());
  client_phishing_reports_[fetcher] = info;

  fetcher->set_load_flags(net::LOAD_DISABLE_CACHE);
  fetcher->set_request_context(request_context_getter_.get());
  fetcher->set_upload_data("application/octet-stream", request_data);
  fetcher->Start();

  // Record that we made a request
  phishing_report_times_.push(base::Time::Now());
}

void ClientSideDetectionService::HandleModelResponse(
    const URLFetcher* source,
    const GURL& url,
    const net::URLRequestStatus& status,
    int response_code,
    const net::ResponseCookies& cookies,
    const std::string& data) {
  if (status.is_success() && RC_REQUEST_OK == response_code) {
    // Copy the model because it has to be accessible after this function
    // returns.  Once we have written the model to a file we will delete the
    // temporary model string. TODO(noelutz): don't store the model to disk if
    // it's invalid.
    tmp_model_string_.reset(new std::string(data));
    base::FileUtilProxy::CreateOrOpenCallback* cb =
        callback_factory_.NewCallback(
            &ClientSideDetectionService::CreateModelFileDone);
    if (!base::FileUtilProxy::CreateOrOpen(
            BrowserThread::GetMessageLoopProxyForThread(BrowserThread::FILE),
            model_path_,
            base::PLATFORM_FILE_CREATE_ALWAYS |
            base::PLATFORM_FILE_WRITE |
            base::PLATFORM_FILE_READ,
            cb)) {
      delete cb;
      SetModelStatus(ERROR_STATUS);
    }
  } else {
    SetModelStatus(ERROR_STATUS);
  }
}

void ClientSideDetectionService::HandlePhishingVerdict(
    const URLFetcher* source,
    const GURL& url,
    const net::URLRequestStatus& status,
    int response_code,
    const net::ResponseCookies& cookies,
    const std::string& data) {
  ClientPhishingResponse response;
  scoped_ptr<ClientReportInfo> info(client_phishing_reports_[source]);
  if (status.is_success() && RC_REQUEST_OK == response_code &&
      response.ParseFromString(data)) {
    // Cache response, possibly flushing an old one.
    cache_[info->phishing_url] =
        make_linked_ptr(new CacheState(response.phishy(), base::Time::Now()));
    info->callback->Run(info->phishing_url, response.phishy());
  } else {
    DLOG(ERROR) << "Unable to get the server verdict for URL: "
                << info->phishing_url << " status: " << status.status() << " "
                << "response_code:" << response_code;
    info->callback->Run(info->phishing_url, false);
  }
  client_phishing_reports_.erase(source);
  delete source;
}

bool ClientSideDetectionService::IsInCache(const GURL& url) {
  UpdateCache();

  return cache_.find(url) != cache_.end();
}

bool ClientSideDetectionService::GetValidCachedResult(const GURL& url,
                                                      bool* is_phishing) {
  UpdateCache();

  PhishingCache::iterator it = cache_.find(url);
  if (it == cache_.end()) {
    return false;
  }

  // We still need to check if the result is valid.
  const CacheState& cache_state = *it->second;
  if (cache_state.is_phishing ?
      cache_state.timestamp > base::Time::Now() - kPositiveCacheInterval :
      cache_state.timestamp > base::Time::Now() - kNegativeCacheInterval) {
    *is_phishing = cache_state.is_phishing;
    return true;
  }
  return false;
}

void ClientSideDetectionService::UpdateCache() {
  // Since we limit the number of requests but allow pass-through for cache
  // refreshes, we don't want to remove elements from the cache if they
  // could be used for this purpose even if we will not use the entry to
  // satisfy the request from the cache.
  base::TimeDelta positive_cache_interval =
      std::max(kPositiveCacheInterval, kReportsInterval);
  base::TimeDelta negative_cache_interval =
      std::max(kNegativeCacheInterval, kReportsInterval);

  // Remove elements from the cache that will no longer be used.
  for (PhishingCache::iterator it = cache_.begin(); it != cache_.end();) {
    const CacheState& cache_state = *it->second;
    if (cache_state.is_phishing ?
        cache_state.timestamp > base::Time::Now() - positive_cache_interval :
        cache_state.timestamp > base::Time::Now() - negative_cache_interval) {
      ++it;
    } else {
      cache_.erase(it++);
    }
  }
}

bool ClientSideDetectionService::OverReportLimit() {
  return GetNumReports() > kMaxReportsPerInterval;
}

int ClientSideDetectionService::GetNumReports() {
  base::Time cutoff = base::Time::Now() - kReportsInterval;

  // Erase items older than cutoff because we will never care about them again.
  while (!phishing_report_times_.empty() &&
         phishing_report_times_.front() < cutoff) {
    phishing_report_times_.pop();
  }

  // Return the number of elements that are above the cutoff.
  return phishing_report_times_.size();
}

bool ClientSideDetectionService::InitializePrivateNetworks() {
  static const char* const kPrivateNetworks[] = {
    "10.0.0.0/8",
    "127.0.0.0/8",
    "172.16.0.0/12",
    "192.168.0.0/16",
    // IPv6 address ranges
    "fc00::/7",
    "fec0::/10",
    "::1/128",
  };

  for (size_t i = 0; i < arraysize(kPrivateNetworks); ++i) {
    net::IPAddressNumber ip_number;
    size_t prefix_length;
    if (net::ParseCIDRBlock(kPrivateNetworks[i], &ip_number, &prefix_length)) {
      private_networks_.push_back(std::make_pair(ip_number, prefix_length));
    } else {
      DLOG(FATAL) << "Unable to parse IP address range: "
                  << kPrivateNetworks[i];
      return false;
    }
  }
  return true;
}

}  // namespace safe_browsing
