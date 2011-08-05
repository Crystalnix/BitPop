// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/glue/http_bridge.h"

#include "base/message_loop.h"
#include "base/message_loop_proxy.h"
#include "base/string_number_conversions.h"
#include "content/browser/browser_thread.h"
#include "net/base/cookie_monster.h"
#include "net/base/host_resolver.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/http/http_cache.h"
#include "net/http/http_network_layer.h"
#include "net/http/http_response_headers.h"
#include "net/proxy/proxy_service.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_status.h"
#include "webkit/glue/webkit_glue.h"

namespace browser_sync {

HttpBridge::RequestContextGetter::RequestContextGetter(
    net::URLRequestContextGetter* baseline_context_getter)
    : baseline_context_getter_(baseline_context_getter) {
}

net::URLRequestContext*
HttpBridge::RequestContextGetter::GetURLRequestContext() {
  // Lazily create the context.
  if (!context_) {
    net::URLRequestContext* baseline_context =
        baseline_context_getter_->GetURLRequestContext();
    context_ = new RequestContext(baseline_context);
    baseline_context_getter_ = NULL;
  }

  // Apply the user agent which was set earlier.
  if (is_user_agent_set())
    context_->set_user_agent(user_agent_);

  return context_;
}

scoped_refptr<base::MessageLoopProxy>
HttpBridge::RequestContextGetter::GetIOMessageLoopProxy() const {
  return BrowserThread::GetMessageLoopProxyForThread(BrowserThread::IO);
}

HttpBridgeFactory::HttpBridgeFactory(
    net::URLRequestContextGetter* baseline_context_getter) {
  DCHECK(baseline_context_getter != NULL);
  request_context_getter_ =
      new HttpBridge::RequestContextGetter(baseline_context_getter);
}

HttpBridgeFactory::~HttpBridgeFactory() {
}

sync_api::HttpPostProviderInterface* HttpBridgeFactory::Create() {
  HttpBridge* http = new HttpBridge(request_context_getter_);
  http->AddRef();
  return http;
}

void HttpBridgeFactory::Destroy(sync_api::HttpPostProviderInterface* http) {
  static_cast<HttpBridge*>(http)->Release();
}

HttpBridge::RequestContext::RequestContext(
    net::URLRequestContext* baseline_context)
    : baseline_context_(baseline_context) {

  // Create empty, in-memory cookie store.
  set_cookie_store(new net::CookieMonster(NULL, NULL));

  // We don't use a cache for bridged loads, but we do want to share proxy info.
  set_host_resolver(baseline_context->host_resolver());
  set_proxy_service(baseline_context->proxy_service());
  set_ssl_config_service(baseline_context->ssl_config_service());

  // We want to share the HTTP session data with the network layer factory,
  // which includes auth_cache for proxies.
  // Session is not refcounted so we need to be careful to not lose the parent
  // context.
  net::HttpNetworkSession* session =
      baseline_context->http_transaction_factory()->GetSession();
  DCHECK(session);
  set_http_transaction_factory(new net::HttpNetworkLayer(session));

  // TODO(timsteele): We don't currently listen for pref changes of these
  // fields or CookiePolicy; I'm not sure we want to strictly follow the
  // default settings, since for example if the user chooses to block all
  // cookies, sync will start failing. Also it seems like accept_lang/charset
  // should be tied to whatever the sync servers expect (if anything). These
  // fields should probably just be settable by sync backend; though we should
  // figure out if we need to give the user explicit control over policies etc.
  set_accept_language(baseline_context->accept_language());
  set_accept_charset(baseline_context->accept_charset());

  // We default to the browser's user agent. This can (and should) be overridden
  // with set_user_agent.
  set_user_agent(webkit_glue::GetUserAgent(GURL()));

  set_net_log(baseline_context->net_log());
}

HttpBridge::RequestContext::~RequestContext() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  delete http_transaction_factory();
}

HttpBridge::URLFetchState::URLFetchState() : url_poster(NULL),
                                             aborted(false),
                                             request_completed(false),
                                             request_succeeded(false),
                                             http_response_code(-1),
                                             os_error_code(-1) {}
HttpBridge::URLFetchState::~URLFetchState() {}

HttpBridge::HttpBridge(HttpBridge::RequestContextGetter* context_getter)
    : context_getter_for_request_(context_getter),
      created_on_loop_(MessageLoop::current()),
      http_post_completed_(false, false) {
}

HttpBridge::~HttpBridge() {
}

void HttpBridge::SetUserAgent(const char* user_agent) {
  DCHECK_EQ(MessageLoop::current(), created_on_loop_);
  if (DCHECK_IS_ON()) {
    base::AutoLock lock(fetch_state_lock_);
    DCHECK(!fetch_state_.request_completed);
  }
  context_getter_for_request_->set_user_agent(user_agent);
}

void HttpBridge::SetExtraRequestHeaders(const char * headers) {
  DCHECK(extra_headers_.empty())
      << "HttpBridge::SetExtraRequestHeaders called twice.";
  extra_headers_.assign(headers);
}

void HttpBridge::SetURL(const char* url, int port) {
  DCHECK_EQ(MessageLoop::current(), created_on_loop_);
  if (DCHECK_IS_ON()) {
    base::AutoLock lock(fetch_state_lock_);
    DCHECK(!fetch_state_.request_completed);
  }
  DCHECK(url_for_request_.is_empty())
      << "HttpBridge::SetURL called more than once?!";
  GURL temp(url);
  GURL::Replacements replacements;
  std::string port_str = base::IntToString(port);
  replacements.SetPort(port_str.c_str(),
                       url_parse::Component(0, port_str.length()));
  url_for_request_ = temp.ReplaceComponents(replacements);
}

void HttpBridge::SetPostPayload(const char* content_type,
                                int content_length,
                                const char* content) {
  DCHECK_EQ(MessageLoop::current(), created_on_loop_);
  if (DCHECK_IS_ON()) {
    base::AutoLock lock(fetch_state_lock_);
    DCHECK(!fetch_state_.request_completed);
  }
  DCHECK(content_type_.empty()) << "Bridge payload already set.";
  DCHECK_GE(content_length, 0) << "Content length < 0";
  content_type_ = content_type;
  if (!content || (content_length == 0)) {
    DCHECK_EQ(content_length, 0);
    request_content_ = " ";  // TODO(timsteele): URLFetcher requires non-empty
                             // content for POSTs whereas CURL does not, for now
                             // we hack this to support the sync backend.
  } else {
    request_content_.assign(content, content_length);
  }
}

bool HttpBridge::MakeSynchronousPost(int* os_error_code, int* response_code) {
  DCHECK_EQ(MessageLoop::current(), created_on_loop_);
  if (DCHECK_IS_ON()) {
    base::AutoLock lock(fetch_state_lock_);
    DCHECK(!fetch_state_.request_completed);
  }
  DCHECK(url_for_request_.is_valid()) << "Invalid URL for request";
  DCHECK(!content_type_.empty()) << "Payload not set";

  if (!BrowserThread::PostTask(
          BrowserThread::IO, FROM_HERE,
          NewRunnableMethod(this, &HttpBridge::CallMakeAsynchronousPost))) {
    // This usually happens when we're in a unit test.
    LOG(WARNING) << "Could not post CallMakeAsynchronousPost task";
    return false;
  }

  if (!http_post_completed_.Wait())  // Block until network request completes
    NOTREACHED();                    // or is aborted. See OnURLFetchComplete
                                     // and Abort.

  base::AutoLock lock(fetch_state_lock_);
  DCHECK(fetch_state_.request_completed || fetch_state_.aborted);
  *os_error_code = fetch_state_.os_error_code;
  *response_code = fetch_state_.http_response_code;
  return fetch_state_.request_succeeded;
}

void HttpBridge::MakeAsynchronousPost() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  base::AutoLock lock(fetch_state_lock_);
  DCHECK(!fetch_state_.request_completed);
  if (fetch_state_.aborted)
    return;

  fetch_state_.url_poster = URLFetcher::Create(0, url_for_request_,
                                               URLFetcher::POST, this);
  fetch_state_.url_poster->set_request_context(context_getter_for_request_);
  fetch_state_.url_poster->set_upload_data(content_type_, request_content_);
  fetch_state_.url_poster->set_extra_request_headers(extra_headers_);
  fetch_state_.url_poster->set_load_flags(net::LOAD_DO_NOT_SEND_COOKIES);
  fetch_state_.url_poster->Start();
}

int HttpBridge::GetResponseContentLength() const {
  DCHECK_EQ(MessageLoop::current(), created_on_loop_);
  base::AutoLock lock(fetch_state_lock_);
  DCHECK(fetch_state_.request_completed);
  return fetch_state_.response_content.size();
}

const char* HttpBridge::GetResponseContent() const {
  DCHECK_EQ(MessageLoop::current(), created_on_loop_);
  base::AutoLock lock(fetch_state_lock_);
  DCHECK(fetch_state_.request_completed);
  return fetch_state_.response_content.data();
}

const std::string HttpBridge::GetResponseHeaderValue(
    const std::string& name) const {

  DCHECK_EQ(MessageLoop::current(), created_on_loop_);
  base::AutoLock lock(fetch_state_lock_);
  DCHECK(fetch_state_.request_completed);

  std::string value;
  fetch_state_.response_headers->EnumerateHeader(NULL, name, &value);
  return value;
}

void HttpBridge::Abort() {
  base::AutoLock lock(fetch_state_lock_);
  DCHECK(!fetch_state_.aborted);
  if (fetch_state_.aborted || fetch_state_.request_completed)
    return;

  fetch_state_.aborted = true;
  BrowserThread::DeleteSoon(BrowserThread::IO, FROM_HERE,
                            fetch_state_.url_poster);
  fetch_state_.url_poster = NULL;
  fetch_state_.os_error_code = net::ERR_ABORTED;
  http_post_completed_.Signal();
}

void HttpBridge::OnURLFetchComplete(const URLFetcher *source,
                                    const GURL &url,
                                    const net::URLRequestStatus &status,
                                    int response_code,
                                    const net::ResponseCookies &cookies,
                                    const std::string &data) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  base::AutoLock lock(fetch_state_lock_);
  if (fetch_state_.aborted)
    return;

  fetch_state_.request_completed = true;
  fetch_state_.request_succeeded =
      (net::URLRequestStatus::SUCCESS == status.status());
  fetch_state_.http_response_code = response_code;
  fetch_state_.os_error_code = status.os_error();

  fetch_state_.response_content = data;
  fetch_state_.response_headers = source->response_headers();

  // End of the line for url_poster_. It lives only on the IO loop.
  // We defer deletion because we're inside a callback from a component of the
  // URLFetcher, so it seems most natural / "polite" to let the stack unwind.
  MessageLoop::current()->DeleteSoon(FROM_HERE, fetch_state_.url_poster);
  fetch_state_.url_poster = NULL;

  // Wake the blocked syncer thread in MakeSynchronousPost.
  // WARNING: DONT DO ANYTHING AFTER THIS CALL! |this| may be deleted!
  http_post_completed_.Signal();
}

}  // namespace browser_sync
