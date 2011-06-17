// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/glue/media/simple_data_source.h"

#include "base/message_loop.h"
#include "base/process_util.h"
#include "media/base/filter_host.h"
#include "net/base/data_url.h"
#include "net/base/load_flags.h"
#include "net/url_request/url_request_status.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebKit.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebKitClient.h"
#include "webkit/glue/media/web_data_source_factory.h"
#include "webkit/glue/webkit_glue.h"

namespace webkit_glue {

static const char kDataScheme[] = "data";

static WebDataSource* NewSimpleDataSource(MessageLoop* render_loop,
                                          WebKit::WebFrame* frame) {
  return new SimpleDataSource(render_loop, frame);
}

// static
media::DataSourceFactory* SimpleDataSource::CreateFactory(
    MessageLoop* render_loop,
    WebKit::WebFrame* frame,
    WebDataSourceBuildObserverHack* build_observer) {
  return new WebDataSourceFactory(render_loop, frame, &NewSimpleDataSource,
                                  build_observer);
}

SimpleDataSource::SimpleDataSource(
    MessageLoop* render_loop,
    WebKit::WebFrame* frame)
    : render_loop_(render_loop),
      frame_(frame),
      size_(-1),
      single_origin_(true),
      state_(UNINITIALIZED),
      keep_test_loader_(false) {
  DCHECK(render_loop);
}

SimpleDataSource::~SimpleDataSource() {
  base::AutoLock auto_lock(lock_);
  DCHECK(state_ == UNINITIALIZED || state_ == STOPPED);
}

void SimpleDataSource::set_host(media::FilterHost* host) {
  DataSource::set_host(host);

  base::AutoLock auto_lock(lock_);
  if (state_ == INITIALIZED) {
    UpdateHostState();
  }
}

void SimpleDataSource::Stop(media::FilterCallback* callback) {
  base::AutoLock auto_lock(lock_);
  state_ = STOPPED;
  if (callback) {
    callback->Run();
    delete callback;
  }

  // Post a task to the render thread to cancel loading the resource.
  render_loop_->PostTask(FROM_HERE,
      NewRunnableMethod(this, &SimpleDataSource::CancelTask));
}

void SimpleDataSource::Initialize(
    const std::string& url,
    media::PipelineStatusCallback* callback) {
  // Reference to prevent destruction while inside the |initialize_callback_|
  // call. This is a temporary fix to prevent crashes caused by holding the
  // lock and running the destructor.
  scoped_refptr<SimpleDataSource> destruction_guard(this);
  {
    base::AutoLock auto_lock(lock_);
    DCHECK_EQ(state_, UNINITIALIZED);
    DCHECK(callback);
    state_ = INITIALIZING;
    initialize_callback_.reset(callback);

    // Validate the URL.
    SetURL(GURL(url));
    if (!url_.is_valid() || !IsProtocolSupportedForMedia(url_)) {
      DoneInitialization_Locked(false);
      return;
    }

    // Post a task to the render thread to start loading the resource.
    render_loop_->PostTask(FROM_HERE,
        NewRunnableMethod(this, &SimpleDataSource::StartTask));
  }
}

void SimpleDataSource::CancelInitialize() {
  base::AutoLock auto_lock(lock_);
  DCHECK(initialize_callback_.get());
  state_ = STOPPED;
  initialize_callback_.reset();

  // Post a task to the render thread to cancel loading the resource.
  render_loop_->PostTask(FROM_HERE,
      NewRunnableMethod(this, &SimpleDataSource::CancelTask));
}

const media::MediaFormat& SimpleDataSource::media_format() {
  return media_format_;
}

void SimpleDataSource::Read(int64 position,
                            size_t size,
                            uint8* data,
                            ReadCallback* read_callback) {
  DCHECK_GE(size_, 0);
  if (position >= size_) {
    read_callback->RunWithParams(Tuple1<size_t>(0));
    delete read_callback;
  } else {
    size_t copied = std::min(size, static_cast<size_t>(size_ - position));
    memcpy(data, data_.c_str() + position, copied);
    read_callback->RunWithParams(Tuple1<size_t>(copied));
    delete read_callback;
  }
}

bool SimpleDataSource::GetSize(int64* size_out) {
  *size_out = size_;
  return true;
}

bool SimpleDataSource::IsStreaming() {
  return false;
}

void SimpleDataSource::SetPreload(media::Preload preload) {}

void SimpleDataSource::SetURLLoaderForTest(WebKit::WebURLLoader* mock_loader) {
  url_loader_.reset(mock_loader);
  keep_test_loader_ = true;
}

void SimpleDataSource::willSendRequest(
    WebKit::WebURLLoader* loader,
    WebKit::WebURLRequest& newRequest,
    const WebKit::WebURLResponse& redirectResponse) {
  DCHECK(MessageLoop::current() == render_loop_);
  base::AutoLock auto_lock(lock_);

  // Only allow |single_origin_| if we haven't seen a different origin yet.
  if (single_origin_)
    single_origin_ = url_.GetOrigin() == GURL(newRequest.url()).GetOrigin();

  url_ = newRequest.url();
}

void SimpleDataSource::didSendData(
    WebKit::WebURLLoader* loader,
    unsigned long long bytesSent,
    unsigned long long totalBytesToBeSent) {
  NOTIMPLEMENTED();
}

void SimpleDataSource::didReceiveResponse(
    WebKit::WebURLLoader* loader,
    const WebKit::WebURLResponse& response) {
  DCHECK(MessageLoop::current() == render_loop_);
  size_ = response.expectedContentLength();
}

void SimpleDataSource::didDownloadData(
    WebKit::WebURLLoader* loader,
    int dataLength) {
  NOTIMPLEMENTED();
}

void SimpleDataSource::didReceiveData(
    WebKit::WebURLLoader* loader,
    const char* data,
    int data_length,
    int encoded_data_length) {
  DCHECK(MessageLoop::current() == render_loop_);
  data_.append(data, data_length);
}

void SimpleDataSource::didReceiveCachedMetadata(
    WebKit::WebURLLoader* loader,
    const char* data,
    int dataLength) {
  NOTIMPLEMENTED();
}

void SimpleDataSource::didFinishLoading(
    WebKit::WebURLLoader* loader,
    double finishTime) {
  DCHECK(MessageLoop::current() == render_loop_);
  // Reference to prevent destruction while inside the |initialize_callback_|
  // call. This is a temporary fix to prevent crashes caused by holding the
  // lock and running the destructor.
  scoped_refptr<SimpleDataSource> destruction_guard(this);
  {
    base::AutoLock auto_lock(lock_);
    // It's possible this gets called after Stop(), in which case |host_| is no
    // longer valid.
    if (state_ == STOPPED)
      return;

    // Otherwise we should be initializing and have created a WebURLLoader.
    DCHECK_EQ(state_, INITIALIZING);

    // If we don't get a content length or the request has failed, report it
    // as a network error.
    if (size_ == -1)
      size_ = data_.length();
    DCHECK(static_cast<size_t>(size_) == data_.length());

    DoneInitialization_Locked(true);
  }
}

void SimpleDataSource::didFail(
    WebKit::WebURLLoader* loader,
    const WebKit::WebURLError& error) {
  DCHECK(MessageLoop::current() == render_loop_);
  // Reference to prevent destruction while inside the |initialize_callback_|
  // call. This is a temporary fix to prevent crashes caused by holding the
  // lock and running the destructor.
  scoped_refptr<SimpleDataSource> destruction_guard(this);
  {
    base::AutoLock auto_lock(lock_);
    // It's possible this gets called after Stop(), in which case |host_| is no
    // longer valid.
    if (state_ == STOPPED)
      return;

    // Otherwise we should be initializing and have created a WebURLLoader.
    DCHECK_EQ(state_, INITIALIZING);

    // If we don't get a content length or the request has failed, report it
    // as a network error.
    if (size_ == -1)
      size_ = data_.length();
    DCHECK(static_cast<size_t>(size_) == data_.length());

    DoneInitialization_Locked(false);
  }
}

bool SimpleDataSource::HasSingleOrigin() {
  DCHECK(MessageLoop::current() == render_loop_);
  return single_origin_;
}

void SimpleDataSource::Abort() {
  DCHECK(MessageLoop::current() == render_loop_);
  frame_ = NULL;
}

void SimpleDataSource::SetURL(const GURL& url) {
  url_ = url;
  media_format_.Clear();
  media_format_.SetAsString(media::MediaFormat::kURL, url.spec());
}

void SimpleDataSource::StartTask() {
  DCHECK(MessageLoop::current() == render_loop_);
  // Reference to prevent destruction while inside the |initialize_callback_|
  // call. This is a temporary fix to prevent crashes caused by holding the
  // lock and running the destructor.
  scoped_refptr<SimpleDataSource> destruction_guard(this);
  {
    base::AutoLock auto_lock(lock_);

    // We may have stopped.
    if (state_ == STOPPED)
      return;

    CHECK(frame_);

    DCHECK_EQ(state_, INITIALIZING);

    if (url_.SchemeIs(kDataScheme)) {
      // If this using data protocol, we just need to decode it.
      std::string mime_type, charset;
      bool success = net::DataURL::Parse(url_, &mime_type, &charset, &data_);

      // Don't care about the mime-type just proceed if decoding was successful.
      size_ = data_.length();
      DoneInitialization_Locked(success);
    } else {
      // Prepare the request.
      WebKit::WebURLRequest request(url_);
      request.setTargetType(WebKit::WebURLRequest::TargetIsMedia);

      frame_->setReferrerForRequest(request, WebKit::WebURL());

      // This flag is for unittests as we don't want to reset |url_loader|
      if (!keep_test_loader_)
        url_loader_.reset(frame_->createAssociatedURLLoader());

      // Start the resource loading.
      url_loader_->loadAsynchronously(request, this);
    }
  }
}

void SimpleDataSource::CancelTask() {
  DCHECK(MessageLoop::current() == render_loop_);
  base::AutoLock auto_lock(lock_);
  DCHECK_EQ(state_, STOPPED);

  // Cancel any pending requests.
  if (url_loader_.get()) {
    url_loader_->cancel();
    url_loader_.reset();
  }
}

void SimpleDataSource::DoneInitialization_Locked(bool success) {
  lock_.AssertAcquired();
  media::PipelineStatus status = media::PIPELINE_ERROR_NETWORK;
  if (success) {
    state_ = INITIALIZED;

    UpdateHostState();
    status = media::PIPELINE_OK;
  } else {
    state_ = UNINITIALIZED;
    url_loader_.reset();
  }

  scoped_ptr<media::PipelineStatusCallback> initialize_callback(
      initialize_callback_.release());
  initialize_callback->Run(status);
}

void SimpleDataSource::UpdateHostState() {
  if (host()) {
    host()->SetTotalBytes(size_);
    host()->SetBufferedBytes(size_);
    // If scheme is file or data, say we are loaded.
    host()->SetLoaded(url_.SchemeIsFile() || url_.SchemeIs(kDataScheme));
  }
}

}  // namespace webkit_glue
