// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/media/buffered_resource_loader.h"

#include "base/format_macros.h"
#include "base/stringprintf.h"
#include "base/string_util.h"
#include "media/base/media_log.h"
#include "net/base/net_errors.h"
#include "net/http/http_request_headers.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebKit.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebKitPlatformSupport.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebString.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebURLError.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebURLLoaderOptions.h"
#include "webkit/glue/multipart_response_delegate.h"

using WebKit::WebFrame;
using WebKit::WebString;
using WebKit::WebURLError;
using WebKit::WebURLLoader;
using WebKit::WebURLLoaderOptions;
using WebKit::WebURLRequest;
using WebKit::WebURLResponse;
using webkit_glue::MultipartResponseDelegate;

namespace webkit_media {

static const int kHttpOK = 200;
static const int kHttpPartialContent = 206;

// Define the number of bytes in a megabyte.
static const size_t kMegabyte = 1024 * 1024;

// Minimum capacity of the buffer in forward or backward direction.
//
// 2MB is an arbitrary limit; it just seems to be "good enough" in practice.
static const size_t kMinBufferCapacity = 2 * kMegabyte;

// Maximum capacity of the buffer in forward or backward direction. This is
// effectively the largest single read the code path can handle.
// 20MB is an arbitrary limit; it just seems to be "good enough" in practice.
static const size_t kMaxBufferCapacity = 20 * kMegabyte;

// Maximum number of bytes outside the buffer we will wait for in order to
// fulfill a read. If a read starts more than 2MB away from the data we
// currently have in the buffer, we will not wait for buffer to reach the read's
// location and will instead reset the request.
static const int kForwardWaitThreshold = 2 * kMegabyte;

// Computes the suggested backward and forward capacity for the buffer
// if one wants to play at |playback_rate| * the natural playback speed.
// Use a value of 0 for |bitrate| if it is unknown.
static void ComputeTargetBufferWindow(float playback_rate, int bitrate,
                                      size_t* out_backward_capacity,
                                      size_t* out_forward_capacity) {
  static const size_t kDefaultBitrate = 200 * 1024 * 8;  // 200 Kbps.
  static const size_t kMaxBitrate = 20 * kMegabyte * 8;  // 20 Mbps.
  static const float kMaxPlaybackRate = 25.0;
  static const size_t kTargetSecondsBufferedAhead = 10;
  static const size_t kTargetSecondsBufferedBehind = 2;

  // Use a default bit rate if unknown and clamp to prevent overflow.
  if (bitrate <= 0)
    bitrate = kDefaultBitrate;
  bitrate = std::min(static_cast<size_t>(bitrate), kMaxBitrate);

  // Only scale the buffer window for playback rates greater than 1.0 in
  // magnitude and clamp to prevent overflow.
  bool backward_playback = false;
  if (playback_rate < 0.0f) {
    backward_playback = true;
    playback_rate *= -1.0f;
  }

  playback_rate = std::max(playback_rate, 1.0f);
  playback_rate = std::min(playback_rate, kMaxPlaybackRate);

  size_t bytes_per_second = static_cast<size_t>(playback_rate * bitrate / 8.0);

  // Clamp between kMinBufferCapacity and kMaxBufferCapacity.
  *out_forward_capacity = std::max(
      kTargetSecondsBufferedAhead * bytes_per_second, kMinBufferCapacity);
  *out_backward_capacity = std::max(
      kTargetSecondsBufferedBehind * bytes_per_second, kMinBufferCapacity);

  *out_forward_capacity = std::min(*out_forward_capacity, kMaxBufferCapacity);
  *out_backward_capacity = std::min(*out_backward_capacity, kMaxBufferCapacity);

  if (backward_playback)
    std::swap(*out_forward_capacity, *out_backward_capacity);
}

BufferedResourceLoader::BufferedResourceLoader(
    const GURL& url,
    int64 first_byte_position,
    int64 last_byte_position,
    DeferStrategy strategy,
    int bitrate,
    float playback_rate,
    media::MediaLog* media_log)
    : defer_strategy_(strategy),
      range_requested_(false),
      range_supported_(false),
      saved_forward_capacity_(0),
      url_(url),
      first_byte_position_(first_byte_position),
      last_byte_position_(last_byte_position),
      single_origin_(true),
      offset_(0),
      content_length_(kPositionNotSpecified),
      instance_size_(kPositionNotSpecified),
      read_position_(0),
      read_size_(0),
      read_buffer_(NULL),
      first_offset_(0),
      last_offset_(0),
      bitrate_(bitrate),
      playback_rate_(playback_rate),
      media_log_(media_log) {

  size_t backward_capacity;
  size_t forward_capacity;
  ComputeTargetBufferWindow(
      playback_rate_, bitrate_, &backward_capacity, &forward_capacity);
  buffer_.reset(new media::SeekableBuffer(backward_capacity, forward_capacity));
}

BufferedResourceLoader::~BufferedResourceLoader() {}

void BufferedResourceLoader::Start(
    const net::CompletionCallback& start_callback,
    const base::Closure& event_callback,
    WebFrame* frame) {
  // Make sure we have not started.
  DCHECK(start_callback_.is_null());
  DCHECK(event_callback_.is_null());
  DCHECK(!start_callback.is_null());
  DCHECK(!event_callback.is_null());
  CHECK(frame);

  start_callback_ = start_callback;
  event_callback_ = event_callback;

  if (first_byte_position_ != kPositionNotSpecified) {
    // TODO(hclam): server may not support range request so |offset_| may not
    // equal to |first_byte_position_|.
    offset_ = first_byte_position_;
  }

  // Prepare the request.
  WebURLRequest request(url_);
  request.setTargetType(WebURLRequest::TargetIsMedia);

  if (IsRangeRequest()) {
    range_requested_ = true;
    request.setHTTPHeaderField(
        WebString::fromUTF8(net::HttpRequestHeaders::kRange),
        WebString::fromUTF8(GenerateHeaders(first_byte_position_,
                                            last_byte_position_)));
  }
  frame->setReferrerForRequest(request, WebKit::WebURL());

  // Disable compression, compression for audio/video doesn't make sense...
  request.setHTTPHeaderField(
      WebString::fromUTF8(net::HttpRequestHeaders::kAcceptEncoding),
      WebString::fromUTF8("identity;q=1, *;q=0"));

  // Check for our test WebURLLoader.
  scoped_ptr<WebURLLoader> loader;
  if (test_loader_.get()) {
    loader = test_loader_.Pass();
  } else {
    WebURLLoaderOptions options;
    options.allowCredentials = true;
    options.crossOriginRequestPolicy =
        WebURLLoaderOptions::CrossOriginRequestPolicyAllow;
    loader.reset(frame->createAssociatedURLLoader(options));
  }

  // Start the resource loading.
  loader->loadAsynchronously(request, this);
  active_loader_.reset(new ActiveLoader(loader.Pass()));
}

void BufferedResourceLoader::Stop() {
  // Reset callbacks.
  start_callback_.Reset();
  event_callback_.Reset();
  read_callback_.Reset();

  // Use the internal buffer to signal that we have been stopped.
  // TODO(hclam): Not so pretty to do this.
  if (!buffer_.get())
    return;

  // Destroy internal buffer.
  buffer_.reset();

  // Cancel and reset any active loaders.
  active_loader_.reset();
}

void BufferedResourceLoader::Read(
    int64 position,
    int read_size,
    uint8* buffer,
    const net::CompletionCallback& read_callback) {
  DCHECK(start_callback_.is_null());
  DCHECK(read_callback_.is_null());
  DCHECK(!read_callback.is_null());
  DCHECK(buffer_.get());
  DCHECK(buffer);
  DCHECK_GT(read_size, 0);

  // Save the parameter of reading.
  read_callback_ = read_callback;
  read_position_ = position;
  read_size_ = read_size;
  read_buffer_ = buffer;

  // If we're attempting to read past the end of the file, return a zero
  // indicating EOF.
  //
  // This can happen with callees that read in fixed-sized amounts for parsing
  // or at the end of chunked 200 responses when we discover the actual length
  // of the file.
  if (instance_size_ != kPositionNotSpecified &&
      instance_size_ <= read_position_) {
    DoneRead(0);
    return;
  }

  // Make sure |offset_| and |read_position_| does not differ by a large
  // amount.
  if (read_position_ > offset_ + kint32max ||
      read_position_ < offset_ + kint32min) {
    DoneRead(net::ERR_CACHE_MISS);
    return;
  }

  // Make sure |read_size_| is not too large for the buffer to ever be able to
  // fulfill the read request.
  if (read_size_ > kMaxBufferCapacity) {
    DoneRead(net::ERR_FAILED);
    return;
  }

  // Prepare the parameters.
  first_offset_ = static_cast<int>(read_position_ - offset_);
  last_offset_ = first_offset_ + read_size_;

  // If we can serve the request now, do the actual read.
  if (CanFulfillRead()) {
    ReadInternal();
    UpdateDeferBehavior();
    return;
  }

  // If we expect the read request to be fulfilled later, expand capacity as
  // necessary and disable deferring.
  if (WillFulfillRead()) {
    // Advance offset as much as possible to create additional capacity.
    int advance = std::min(first_offset_,
                           static_cast<int>(buffer_->forward_bytes()));
    bool ret = buffer_->Seek(advance);
    DCHECK(ret);

    offset_ += advance;
    first_offset_ -= advance;
    last_offset_ -= advance;

    // Expand capacity to accomodate a read that extends past the normal
    // capacity.
    //
    // This can happen when reading in a large seek index or when the
    // first byte of a read request falls within kForwardWaitThreshold.
    if (last_offset_ > static_cast<int>(buffer_->forward_capacity())) {
      saved_forward_capacity_ = buffer_->forward_capacity();
      buffer_->set_forward_capacity(last_offset_);
    }

    // Make sure we stop deferring now that there's additional capacity.
    if (active_loader_->deferred())
      SetDeferred(false);

    DCHECK(!ShouldEnableDefer())
        << "Capacity was not adjusted properly to prevent deferring.";

    return;
  }

  // Make a callback to report failure.
  DoneRead(net::ERR_CACHE_MISS);
}

int64 BufferedResourceLoader::GetBufferedPosition() {
  if (buffer_.get())
    return offset_ + static_cast<int>(buffer_->forward_bytes()) - 1;
  return kPositionNotSpecified;
}

int64 BufferedResourceLoader::content_length() {
  return content_length_;
}

int64 BufferedResourceLoader::instance_size() {
  return instance_size_;
}

bool BufferedResourceLoader::range_supported() {
  return range_supported_;
}

bool BufferedResourceLoader::is_downloading_data() {
  return active_loader_.get() && !active_loader_->deferred();
}

const GURL& BufferedResourceLoader::url() {
  return url_;
}

void BufferedResourceLoader::SetURLLoaderForTest(
    scoped_ptr<WebURLLoader> test_loader) {
  test_loader_ = test_loader.Pass();
}

/////////////////////////////////////////////////////////////////////////////
// WebKit::WebURLLoaderClient implementation.
void BufferedResourceLoader::willSendRequest(
    WebURLLoader* loader,
    WebURLRequest& newRequest,
    const WebURLResponse& redirectResponse) {

  // The load may have been stopped and |start_callback| is destroyed.
  // In this case we shouldn't do anything.
  if (start_callback_.is_null()) {
    // Set the url in the request to an invalid value (empty url).
    newRequest.setURL(WebKit::WebURL());
    return;
  }

  // Only allow |single_origin_| if we haven't seen a different origin yet.
  if (single_origin_)
    single_origin_ = url_.GetOrigin() == GURL(newRequest.url()).GetOrigin();

  url_ = newRequest.url();
}

void BufferedResourceLoader::didSendData(
    WebURLLoader* loader,
    unsigned long long bytes_sent,
    unsigned long long total_bytes_to_be_sent) {
  NOTIMPLEMENTED();
}

void BufferedResourceLoader::didReceiveResponse(
    WebURLLoader* loader,
    const WebURLResponse& response) {
  DVLOG(1) << "didReceiveResponse: " << response.httpStatusCode();
  DCHECK(active_loader_.get());

  // The loader may have been stopped and |start_callback| is destroyed.
  // In this case we shouldn't do anything.
  if (start_callback_.is_null())
    return;

  bool partial_response = false;

  // We make a strong assumption that when we reach here we have either
  // received a response from HTTP/HTTPS protocol or the request was
  // successful (in particular range request). So we only verify the partial
  // response for HTTP and HTTPS protocol.
  if (url_.SchemeIs(kHttpScheme) || url_.SchemeIs(kHttpsScheme)) {
    int error = net::OK;

    // Check to see whether the server supports byte ranges.
    std::string accept_ranges =
        response.httpHeaderField("Accept-Ranges").utf8();
    range_supported_ = (accept_ranges.find("bytes") != std::string::npos);

    partial_response = (response.httpStatusCode() == kHttpPartialContent);

    if (range_requested_) {
      // If we have verified the partial response and it is correct, we will
      // return net::OK. It's also possible for a server to support range
      // requests without advertising Accept-Ranges: bytes.
      if (partial_response && VerifyPartialResponse(response))
        range_supported_ = true;
      else
        error = net::ERR_INVALID_RESPONSE;
    } else if (response.httpStatusCode() != kHttpOK) {
      // We didn't request a range but server didn't reply with "200 OK".
      error = net::ERR_FAILED;
    }

    if (error != net::OK) {
      DoneStart(error);
      return;
    }
  } else {
    // For any protocol other than HTTP and HTTPS, assume range request is
    // always fulfilled.
    partial_response = range_requested_;
  }

  // Expected content length can be |kPositionNotSpecified|, in that case
  // |content_length_| is not specified and this is a streaming response.
  content_length_ = response.expectedContentLength();

  // If we have not requested a range, then the size of the instance is equal
  // to the content length.
  if (!partial_response)
    instance_size_ = content_length_;

  // Calls with a successful response.
  DoneStart(net::OK);
}

void BufferedResourceLoader::didReceiveData(
    WebURLLoader* loader,
    const char* data,
    int data_length,
    int encoded_data_length) {
  DVLOG(1) << "didReceiveData: " << data_length << " bytes";
  DCHECK(active_loader_.get());
  DCHECK_GT(data_length, 0);

  // If this loader has been stopped, |buffer_| would be destroyed.
  // In this case we shouldn't do anything.
  if (!buffer_.get())
    return;

  // Writes more data to |buffer_|.
  buffer_->Append(reinterpret_cast<const uint8*>(data), data_length);

  // If there is an active read request, try to fulfill the request.
  if (HasPendingRead() && CanFulfillRead())
    ReadInternal();

  // At last see if the buffer is full and we need to defer the downloading.
  UpdateDeferBehavior();

  // Consume excess bytes from our in-memory buffer if necessary.
  if (buffer_->forward_bytes() > buffer_->forward_capacity()) {
    size_t excess = buffer_->forward_bytes() - buffer_->forward_capacity();
    bool success = buffer_->Seek(excess);
    DCHECK(success);
    offset_ += first_offset_ + excess;
  }

  // Notify that we have received some data.
  NotifyNetworkEvent();
  Log();
}

void BufferedResourceLoader::didDownloadData(
    WebKit::WebURLLoader* loader,
    int dataLength) {
  NOTIMPLEMENTED();
}

void BufferedResourceLoader::didReceiveCachedMetadata(
    WebURLLoader* loader,
    const char* data,
    int data_length) {
  NOTIMPLEMENTED();
}

void BufferedResourceLoader::didFinishLoading(
    WebURLLoader* loader,
    double finishTime) {
  DVLOG(1) << "didFinishLoading";
  DCHECK(active_loader_.get());

  // We're done with the loader.
  active_loader_.reset();
  NotifyNetworkEvent();

  // If we didn't know the |instance_size_| we do now.
  if (instance_size_ == kPositionNotSpecified) {
    instance_size_ = offset_ + buffer_->forward_bytes();
  }

  // If there is a start callback, run it.
  if (!start_callback_.is_null()) {
    DCHECK(read_callback_.is_null())
        << "Shouldn't have a read callback during start";
    DoneStart(net::OK);
    return;
  }

  // If there is a pending read but the request has ended, return with what
  // we have.
  if (HasPendingRead()) {
    // Make sure we have a valid buffer before we satisfy a read request.
    DCHECK(buffer_.get());

    // Try to fulfill with what is in the buffer.
    if (CanFulfillRead())
      ReadInternal();
    else
      DoneRead(net::ERR_CACHE_MISS);
  }

  // There must not be any outstanding read request.
  DCHECK(!HasPendingRead());
}

void BufferedResourceLoader::didFail(
    WebURLLoader* loader,
    const WebURLError& error) {
  DVLOG(1) << "didFail: reason=" << error.reason
           << " isCancellation=" << error.isCancellation;
  DCHECK(active_loader_.get());

  // We don't need to continue loading after failure.
  //
  // Keep it alive until we exit this method so that |error| remains valid.
  scoped_ptr<ActiveLoader> active_loader(active_loader_.release());
  NotifyNetworkEvent();

  // Don't leave start callbacks hanging around.
  if (!start_callback_.is_null()) {
    DCHECK(read_callback_.is_null())
        << "Shouldn't have a read callback during start";
    DoneStart(net::ERR_FAILED);
    return;
  }

  // Don't leave read callbacks hanging around.
  if (HasPendingRead()) {
    DoneRead(net::ERR_FAILED);
  }
}

bool BufferedResourceLoader::HasSingleOrigin() const {
  return single_origin_;
}

void BufferedResourceLoader::UpdateDeferStrategy(DeferStrategy strategy) {
  defer_strategy_ = strategy;
  UpdateDeferBehavior();
}

void BufferedResourceLoader::SetPlaybackRate(float playback_rate) {
  playback_rate_ = playback_rate;

  // This is a pause so don't bother updating the buffer window as we'll likely
  // get unpaused in the future.
  if (playback_rate_ == 0.0)
    return;

  UpdateBufferWindow();
}

void BufferedResourceLoader::SetBitrate(int bitrate) {
  DCHECK(bitrate >= 0);
  bitrate_ = bitrate;
  UpdateBufferWindow();
}

/////////////////////////////////////////////////////////////////////////////
// Helper methods.

void BufferedResourceLoader::UpdateBufferWindow() {
  if (!buffer_.get())
    return;

  size_t backward_capacity;
  size_t forward_capacity;
  ComputeTargetBufferWindow(
      playback_rate_, bitrate_, &backward_capacity, &forward_capacity);

  // This does not evict data from the buffer if the new capacities are less
  // than the current capacities; the new limits will be enforced after the
  // existing excess buffered data is consumed.
  buffer_->set_backward_capacity(backward_capacity);
  buffer_->set_forward_capacity(forward_capacity);
}

void BufferedResourceLoader::UpdateDeferBehavior() {
  if (!active_loader_.get() || !buffer_.get())
    return;

  // If necessary, toggle defer state and continue/pause downloading data
  // accordingly.
  if (ShouldEnableDefer() || ShouldDisableDefer())
    SetDeferred(!active_loader_->deferred());
}

void BufferedResourceLoader::SetDeferred(bool deferred) {
  active_loader_->SetDeferred(deferred);
  NotifyNetworkEvent();
}

bool BufferedResourceLoader::ShouldEnableDefer() const {
  // If we're already deferring, then enabling makes no sense.
  if (active_loader_->deferred())
    return false;

  switch(defer_strategy_) {
    // Never defer at all, so never enable defer.
    case kNeverDefer:
      return false;

    // Defer if nothing is being requested.
    case kReadThenDefer:
      return read_callback_.is_null();

    // Defer if we've reached the max capacity of the threshold.
    case kThresholdDefer:
      return buffer_->forward_bytes() >= buffer_->forward_capacity();
  }
  // Otherwise don't enable defer.
  return false;
}

bool BufferedResourceLoader::ShouldDisableDefer() const {
  // If we're not deferring, then disabling makes no sense.
  if (!active_loader_->deferred())
    return false;

  switch(defer_strategy_) {
    // Always disable deferring.
    case kNeverDefer:
      return true;

    // We have an outstanding read request, and we have not buffered enough
    // yet to fulfill the request; disable defer to get more data.
    case kReadThenDefer:
      return !read_callback_.is_null() &&
          last_offset_ > static_cast<int>(buffer_->forward_bytes());

    // We have less than half the capacity of our threshold, so
    // disable defer to get more data.
    case kThresholdDefer: {
      size_t amount_buffered = buffer_->forward_bytes();
      size_t half_capacity = buffer_->forward_capacity() / 2;
      return amount_buffered < half_capacity;
    }
  }

  // Otherwise keep deferring.
  return false;
}

bool BufferedResourceLoader::CanFulfillRead() const {
  // If we are reading too far in the backward direction.
  if (first_offset_ < 0 &&
      first_offset_ + static_cast<int>(buffer_->backward_bytes()) < 0)
    return false;

  // If the start offset is too far ahead.
  if (first_offset_ >= static_cast<int>(buffer_->forward_bytes()))
    return false;

  // At the point, we verified that first byte requested is within the buffer.
  // If the request has completed, then just returns with what we have now.
  if (!active_loader_.get())
    return true;

  // If the resource request is still active, make sure the whole requested
  // range is covered.
  if (last_offset_ > static_cast<int>(buffer_->forward_bytes()))
    return false;

  return true;
}

bool BufferedResourceLoader::WillFulfillRead() const {
  // Trying to read too far behind.
  if (first_offset_ < 0 &&
      first_offset_ + static_cast<int>(buffer_->backward_bytes()) < 0)
    return false;

  // Trying to read too far ahead.
  if (first_offset_ - static_cast<int>(buffer_->forward_bytes()) >=
      kForwardWaitThreshold)
    return false;

  // The resource request has completed, there's no way we can fulfill the
  // read request.
  if (!active_loader_.get())
    return false;

  return true;
}

void BufferedResourceLoader::ReadInternal() {
  // Seek to the first byte requested.
  bool ret = buffer_->Seek(first_offset_);
  DCHECK(ret);

  // Then do the read.
  int read = static_cast<int>(buffer_->Read(read_buffer_, read_size_));
  offset_ += first_offset_ + read;

  // And report with what we have read.
  DoneRead(read);
}

bool BufferedResourceLoader::VerifyPartialResponse(
    const WebURLResponse& response) {
  int64 first_byte_position, last_byte_position, instance_size;

  if (!MultipartResponseDelegate::ReadContentRanges(response,
                                                    &first_byte_position,
                                                    &last_byte_position,
                                                    &instance_size)) {
    return false;
  }

  if (instance_size != kPositionNotSpecified) {
    instance_size_ = instance_size;
  }

  if (first_byte_position_ != kPositionNotSpecified &&
      first_byte_position_ != first_byte_position) {
    return false;
  }

  // TODO(hclam): I should also check |last_byte_position|, but since
  // we will never make such a request that it is ok to leave it unimplemented.
  return true;
}

std::string BufferedResourceLoader::GenerateHeaders(
    int64 first_byte_position,
    int64 last_byte_position) {
  // Construct the value for the range header.
  std::string header;
  if (first_byte_position > kPositionNotSpecified &&
      last_byte_position > kPositionNotSpecified) {
    if (first_byte_position <= last_byte_position) {
      header = base::StringPrintf("bytes=%" PRId64 "-%" PRId64,
                                  first_byte_position,
                                  last_byte_position);
    }
  } else if (first_byte_position > kPositionNotSpecified) {
    header = base::StringPrintf("bytes=%" PRId64 "-",
                                first_byte_position);
  } else if (last_byte_position > kPositionNotSpecified) {
    NOTIMPLEMENTED() << "Suffix range not implemented";
  }
  return header;
}

void BufferedResourceLoader::DoneRead(int error) {
  if (buffer_.get() && saved_forward_capacity_) {
    buffer_->set_forward_capacity(saved_forward_capacity_);
    saved_forward_capacity_ = 0;
  }
  read_position_ = 0;
  read_size_ = 0;
  read_buffer_ = NULL;
  first_offset_ = 0;
  last_offset_ = 0;
  Log();

  net::CompletionCallback read_callback;
  std::swap(read_callback, read_callback_);
  read_callback.Run(error);
}

void BufferedResourceLoader::DoneStart(int error) {
  net::CompletionCallback start_callback;
  std::swap(start_callback, start_callback_);
  start_callback.Run(error);
}

void BufferedResourceLoader::NotifyNetworkEvent() {
  if (!event_callback_.is_null())
    event_callback_.Run();
}

bool BufferedResourceLoader::IsRangeRequest() const {
  return first_byte_position_ != kPositionNotSpecified;
}

void BufferedResourceLoader::Log() {
  if (buffer_.get()) {
    media_log_->AddEvent(
        media_log_->CreateBufferedExtentsChangedEvent(
            offset_ - buffer_->backward_bytes(),
            offset_,
            offset_ + buffer_->forward_bytes()));
  }
}

}  // namespace webkit_media
