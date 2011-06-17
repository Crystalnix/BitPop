// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "webkit/appcache/appcache_url_request_job.h"

#include "base/compiler_specific.h"
#include "base/message_loop.h"
#include "base/string_util.h"
#include "base/stringprintf.h"
#include "net/base/net_errors.h"
#include "net/base/net_log.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_util.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_status.h"

namespace appcache {

AppCacheURLRequestJob::AppCacheURLRequestJob(
    net::URLRequest* request, AppCacheStorage* storage)
    : net::URLRequestJob(request), storage_(storage),
      has_been_started_(false), has_been_killed_(false),
      delivery_type_(AWAITING_DELIVERY_ORDERS),
      cache_id_(kNoCacheId), is_fallback_(false),
      cache_entry_not_found_(false),
      ALLOW_THIS_IN_INITIALIZER_LIST(read_callback_(
          this, &AppCacheURLRequestJob::OnReadComplete)),
      ALLOW_THIS_IN_INITIALIZER_LIST(method_factory_(this)) {
  DCHECK(storage_);
}

AppCacheURLRequestJob::~AppCacheURLRequestJob() {
  if (storage_)
    storage_->CancelDelegateCallbacks(this);
}

void AppCacheURLRequestJob::DeliverAppCachedResponse(
    const GURL& manifest_url, int64 cache_id, const AppCacheEntry& entry,
    bool is_fallback) {
  DCHECK(!has_delivery_orders());
  DCHECK(entry.has_response_id());
  delivery_type_ = APPCACHED_DELIVERY;
  manifest_url_ = manifest_url;
  cache_id_ = cache_id;
  entry_ = entry;
  is_fallback_ = is_fallback;
  MaybeBeginDelivery();
}

void AppCacheURLRequestJob::DeliverNetworkResponse() {
  DCHECK(!has_delivery_orders());
  delivery_type_ = NETWORK_DELIVERY;
  storage_ = NULL;  // not needed
  MaybeBeginDelivery();
}

void AppCacheURLRequestJob::DeliverErrorResponse() {
  DCHECK(!has_delivery_orders());
  delivery_type_ = ERROR_DELIVERY;
  storage_ = NULL;  // not needed
  MaybeBeginDelivery();
}

void AppCacheURLRequestJob::MaybeBeginDelivery() {
  if (has_been_started() && has_delivery_orders()) {
    // Start asynchronously so that all error reporting and data
    // callbacks happen as they would for network requests.
    MessageLoop::current()->PostTask(
        FROM_HERE,
        method_factory_.NewRunnableMethod(
            &AppCacheURLRequestJob::BeginDelivery));
  }
}

void AppCacheURLRequestJob::BeginDelivery() {
  DCHECK(has_delivery_orders() && has_been_started());

  if (has_been_killed())
    return;

  switch (delivery_type_) {
    case NETWORK_DELIVERY:
      // To fallthru to the network, we restart the request which will
      // cause a new job to be created to retrieve the resource from the
      // network. Our caller is responsible for arranging to not re-intercept
      // the same request.
      NotifyRestartRequired();
      break;

    case ERROR_DELIVERY:
      request()->net_log().AddEvent(
          net::NetLog::TYPE_APPCACHE_DELIVERING_ERROR_RESPONSE, NULL);
      NotifyStartError(net::URLRequestStatus(net::URLRequestStatus::FAILED,
                                             net::ERR_FAILED));
      break;

    case APPCACHED_DELIVERY:
      request()->net_log().AddEvent(
          is_fallback_ ?
              net::NetLog::TYPE_APPCACHE_DELIVERING_FALLBACK_RESPONSE :
              net::NetLog::TYPE_APPCACHE_DELIVERING_CACHED_RESPONSE,
          NULL);
      storage_->LoadResponseInfo(manifest_url_, entry_.response_id(), this);
      break;

    default:
      NOTREACHED();
      break;
  }
}

void AppCacheURLRequestJob::OnResponseInfoLoaded(
      AppCacheResponseInfo* response_info, int64 response_id) {
  DCHECK(is_delivering_appcache_response());
  scoped_refptr<AppCacheURLRequestJob> protect(this);
  if (response_info) {
    info_ = response_info;
    reader_.reset(
        storage_->CreateResponseReader(manifest_url_, entry_.response_id()));

    if (is_range_request())
      SetupRangeResponse();

    NotifyHeadersComplete();
  } else {
    // A resource that is expected to be in the appcache is missing.
    // See http://code.google.com/p/chromium/issues/detail?id=50657
    // Instead of failing the request, we restart the request. The retry
    // attempt will fallthru to the network instead of trying to load
    // from the appcache.
    cache_entry_not_found_ = true;
    NotifyRestartRequired();
  }
  storage_ = NULL;  // no longer needed
}

const net::HttpResponseInfo* AppCacheURLRequestJob::http_info() const {
  if (!info_.get())
    return NULL;
  if (range_response_info_.get())
    return range_response_info_.get();
  return info_->http_response_info();
}

void AppCacheURLRequestJob::SetupRangeResponse() {
  DCHECK(is_range_request() && info_.get() && reader_.get() &&
         is_delivering_appcache_response());
  int resource_size = static_cast<int>(info_->response_data_size());
  if (resource_size < 0 || !range_requested_.ComputeBounds(resource_size)) {
    range_requested_ = net::HttpByteRange();
    return;
  }

  DCHECK(range_requested_.HasFirstBytePosition() &&
         range_requested_.HasLastBytePosition());
  int offset = static_cast<int>(range_requested_.first_byte_position());
  int length = static_cast<int>(range_requested_.last_byte_position() -
                                range_requested_.first_byte_position() + 1);

  // Tell the reader about the range to read.
  reader_->SetReadRange(offset, length);

  // Make a copy of the full response headers and fix them up
  // for the range we'll be returning.
  const char kLengthHeader[] = "Content-Length";
  const char kRangeHeader[] = "Content-Range";
  const char kPartialStatusLine[] = "HTTP/1.1 206 Partial Content";
  range_response_info_.reset(
      new net::HttpResponseInfo(*info_->http_response_info()));
  net::HttpResponseHeaders* headers = range_response_info_->headers;
  headers->RemoveHeader(kLengthHeader);
  headers->RemoveHeader(kRangeHeader);
  headers->ReplaceStatusLine(kPartialStatusLine);
  headers->AddHeader(
      base::StringPrintf("%s: %d", kLengthHeader, length));
  headers->AddHeader(
      base::StringPrintf("%s: bytes %d-%d/%d",
                         kRangeHeader,
                         offset,
                         offset + length - 1,
                         resource_size));
}

void AppCacheURLRequestJob::OnReadComplete(int result) {
  DCHECK(is_delivering_appcache_response());
  if (result == 0)
    NotifyDone(net::URLRequestStatus());
  else if (result < 0)
    NotifyDone(net::URLRequestStatus(net::URLRequestStatus::FAILED, result));
  else
    SetStatus(net::URLRequestStatus());  // Clear the IO_PENDING status

  NotifyReadComplete(result);
}

// net::URLRequestJob overrides ------------------------------------------------

void AppCacheURLRequestJob::Start() {
  DCHECK(!has_been_started());
  has_been_started_ = true;
  MaybeBeginDelivery();
}

void AppCacheURLRequestJob::Kill() {
  if (!has_been_killed_) {
    has_been_killed_ = true;
    reader_.reset();
    if (storage_) {
      storage_->CancelDelegateCallbacks(this);
      storage_ = NULL;
    }
    net::URLRequestJob::Kill();
    method_factory_.RevokeAll();
  }
}

net::LoadState AppCacheURLRequestJob::GetLoadState() const {
  if (!has_been_started())
    return net::LOAD_STATE_IDLE;
  if (!has_delivery_orders())
    return net::LOAD_STATE_WAITING_FOR_CACHE;
  if (delivery_type_ != APPCACHED_DELIVERY)
    return net::LOAD_STATE_IDLE;
  if (!info_.get())
    return net::LOAD_STATE_WAITING_FOR_CACHE;
  if (reader_.get() && reader_->IsReadPending())
    return net::LOAD_STATE_READING_RESPONSE;
  return net::LOAD_STATE_IDLE;
}

bool AppCacheURLRequestJob::GetMimeType(std::string* mime_type) const {
  if (!http_info())
    return false;
  return http_info()->headers->GetMimeType(mime_type);
}

bool AppCacheURLRequestJob::GetCharset(std::string* charset) {
  if (!http_info())
    return false;
  return http_info()->headers->GetCharset(charset);
}

void AppCacheURLRequestJob::GetResponseInfo(net::HttpResponseInfo* info) {
  if (!http_info())
    return;
  *info = *http_info();
}

int AppCacheURLRequestJob::GetResponseCode() const {
  if (!http_info())
    return -1;
  return http_info()->headers->response_code();
}

bool AppCacheURLRequestJob::ReadRawData(net::IOBuffer* buf, int buf_size,
                                        int *bytes_read) {
  DCHECK(is_delivering_appcache_response());
  DCHECK_NE(buf_size, 0);
  DCHECK(bytes_read);
  DCHECK(!reader_->IsReadPending());
  reader_->ReadData(buf, buf_size, &read_callback_);
  SetStatus(net::URLRequestStatus(net::URLRequestStatus::IO_PENDING, 0));
  return false;
}

void AppCacheURLRequestJob::SetExtraRequestHeaders(
    const net::HttpRequestHeaders& headers) {
  std::string value;
  std::vector<net::HttpByteRange> ranges;
  if (!headers.GetHeader(net::HttpRequestHeaders::kRange, &value) ||
      !net::HttpUtil::ParseRangeHeader(value, &ranges)) {
    return;
  }

  // If multiple ranges are requested, we play dumb and
  // return the entire response with 200 OK.
  if (ranges.size() == 1U)
    range_requested_ = ranges[0];
}

}  // namespace appcache
