// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_IO_THREAD_H_
#define CHROME_BROWSER_IO_THREAD_H_
#pragma once

#include <list>
#include <string>
#include "base/basictypes.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/browser_process_sub_thread.h"
#include "chrome/browser/prefs/pref_member.h"
#include "chrome/common/net/predictor_common.h"
#include "net/base/network_change_notifier.h"

class ChromeNetLog;
class ChromeURLRequestContextGetter;
class ExtensionEventRouterForwarder;
class ListValue;
class PrefProxyConfigTracker;
class PrefService;
class SystemURLRequestContextGetter;

namespace chrome_browser_net {
class ConnectInterceptor;
class Predictor;
}  // namespace chrome_browser_net

namespace net {
class CertVerifier;
class DnsRRResolver;
class FtpTransactionFactory;
class HostResolver;
class HttpAuthHandlerFactory;
class HttpTransactionFactory;
class NetworkDelegate;
class ProxyConfigService;
class ProxyScriptFetcher;
class ProxyService;
class SSLConfigService;
class URLRequestContext;
class URLRequestContextGetter;
class URLSecurityManager;
}  // namespace net

class IOThread : public BrowserProcessSubThread {
 public:
  struct Globals {
    Globals();
    ~Globals();

    // The "system" NetworkDelegate, used for Profile-agnostic network events.
    scoped_ptr<net::NetworkDelegate> system_network_delegate;
    scoped_ptr<net::HostResolver> host_resolver;
    scoped_ptr<net::CertVerifier> cert_verifier;
    scoped_ptr<net::DnsRRResolver> dnsrr_resolver;
    scoped_refptr<net::SSLConfigService> ssl_config_service;
    scoped_ptr<net::HttpAuthHandlerFactory> http_auth_handler_factory;
    scoped_refptr<net::ProxyService> proxy_script_fetcher_proxy_service;
    scoped_ptr<net::HttpTransactionFactory>
        proxy_script_fetcher_http_transaction_factory;
    scoped_ptr<net::FtpTransactionFactory>
        proxy_script_fetcher_ftp_transaction_factory;
    scoped_ptr<net::URLSecurityManager> url_security_manager;
    scoped_refptr<net::URLRequestContext> proxy_script_fetcher_context;
    scoped_ptr<net::HttpTransactionFactory> system_http_transaction_factory;
    scoped_ptr<net::FtpTransactionFactory> system_ftp_transaction_factory;
    scoped_refptr<net::ProxyService> system_proxy_service;
    // NOTE(willchan): This request context is unusable until a system
    // SSLConfigService is provided that doesn't rely on
    // Profiles. Do NOT use this yet.
    scoped_refptr<net::URLRequestContext> system_request_context;
    scoped_refptr<ExtensionEventRouterForwarder>
        extension_event_router_forwarder;
  };

  // |net_log| must either outlive the IOThread or be NULL.
  IOThread(PrefService* local_state,
           ChromeNetLog* net_log,
           ExtensionEventRouterForwarder* extension_event_router_forwarder);

  virtual ~IOThread();

  // Can only be called on the IO thread.
  Globals* globals();

  ChromeNetLog* net_log();

  // Initializes the network predictor, which induces DNS pre-resolution and/or
  // TCP/IP preconnections.  |prefetching_enabled| indicates whether or not DNS
  // prefetching should be enabled, and |preconnect_enabled| controls whether
  // TCP/IP preconnection is enabled.  This should be called by the UI thread.
  // It will post a task to the IO thread to perform the actual initialization.
  void InitNetworkPredictor(bool prefetching_enabled,
                            base::TimeDelta max_dns_queue_delay,
                            size_t max_speculative_parallel_resolves,
                            const chrome_common_net::UrlList& startup_urls,
                            ListValue* referral_list,
                            bool preconnect_enabled);

  // Registers |url_request_context_getter| into the IO thread.  During
  // IOThread::CleanUp(), IOThread will iterate through known getters and
  // release their URLRequestContexts.  Only called on the IO thread.  It does
  // not acquire a refcount for |url_request_context_getter|.  If
  // |url_request_context_getter| is being deleted before IOThread::CleanUp() is
  // invoked, then this needs to be balanced with a call to
  // UnregisterURLRequestContextGetter().
  void RegisterURLRequestContextGetter(
      ChromeURLRequestContextGetter* url_request_context_getter);

  // Unregisters |url_request_context_getter| from the IO thread.  Only called
  // on the IO thread.
  void UnregisterURLRequestContextGetter(
      ChromeURLRequestContextGetter* url_request_context_getter);

  // Handles changing to On The Record mode, discarding confidential data.
  void ChangedToOnTheRecord();

  // Returns a getter for the URLRequestContext.  Only called on the UI thread.
  net::URLRequestContextGetter* system_url_request_context_getter();

  // Clear all network stack history, including the host cache, as well as
  // speculative data about subresources of visited sites, and startup-time
  // navigations.
  void ClearNetworkingHistory();

 protected:
  virtual void Init();
  virtual void CleanUp();

 private:
  // Provide SystemURLRequestContextGetter with access to
  // InitSystemRequestContext().
  friend class SystemURLRequestContextGetter;

  static void RegisterPrefs(PrefService* local_state);

  net::HttpAuthHandlerFactory* CreateDefaultAuthHandlerFactory(
      net::HostResolver* resolver);

  // Lazy initialization of system request context for
  // SystemURLRequestContextGetter. To be called on IO thread.
  void InitSystemRequestContext();

  void InitNetworkPredictorOnIOThread(
      bool prefetching_enabled,
      base::TimeDelta max_dns_queue_delay,
      size_t max_speculative_parallel_resolves,
      const chrome_common_net::UrlList& startup_urls,
      ListValue* referral_list,
      bool preconnect_enabled);

  void ChangedToOnTheRecordOnIOThread();

  // Clears the host cache.  Intended to be used to prevent exposing recently
  // visited sites on about:net-internals/#dns and about:dns pages.  Must be
  // called on the IO thread.
  void ClearHostCache();

  // The NetLog is owned by the browser process, to allow logging from other
  // threads during shutdown, but is used most frequently on the IOThread.
  ChromeNetLog* net_log_;

  // The ExtensionEventRouterForwarder allows for sending events to extensions
  // from the IOThread.
  ExtensionEventRouterForwarder* extension_event_router_forwarder_;

  // These member variables are basically global, but their lifetimes are tied
  // to the IOThread.  IOThread owns them all, despite not using scoped_ptr.
  // This is because the destructor of IOThread runs on the wrong thread.  All
  // member variables should be deleted in CleanUp().

  // These member variables are initialized in Init() and do not change for the
  // lifetime of the IO thread.

  Globals* globals_;

  // Observer that logs network changes to the ChromeNetLog.
  scoped_ptr<net::NetworkChangeNotifier::IPAddressObserver>
      network_change_observer_;

  BooleanPrefMember system_enable_referrers_;

  // Store HTTP Auth-related policies in this thread.
  std::string auth_schemes_;
  bool negotiate_disable_cname_lookup_;
  bool negotiate_enable_port_;
  std::string auth_server_whitelist_;
  std::string auth_delegate_whitelist_;
  std::string gssapi_library_name_;

  // These member variables are initialized by a task posted to the IO thread,
  // which gets posted by calling certain member functions of IOThread.

  // Note: we user explicit pointers rather than smart pointers to be more
  // explicit about destruction order, and ensure that there is no chance that
  // these observers would be used accidentally after we have begun to tear
  // down.
  chrome_browser_net::ConnectInterceptor* speculative_interceptor_;
  chrome_browser_net::Predictor* predictor_;

  scoped_ptr<net::ProxyConfigService> system_proxy_config_service_;

  scoped_refptr<PrefProxyConfigTracker> pref_proxy_config_tracker_;

  scoped_refptr<net::URLRequestContextGetter>
      system_url_request_context_getter_;

  // Keeps track of all live ChromeURLRequestContextGetters, so the
  // ChromeURLRequestContexts can be released during
  // IOThread::CleanUp().
  std::list<ChromeURLRequestContextGetter*> url_request_context_getters_;

  DISALLOW_COPY_AND_ASSIGN(IOThread);
};

#endif  // CHROME_BROWSER_IO_THREAD_H_
