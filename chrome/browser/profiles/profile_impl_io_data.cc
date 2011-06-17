// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/profiles/profile_impl_io_data.h"

#include "base/command_line.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/stl_util-inl.h"
#include "chrome/browser/io_thread.h"
#include "chrome/browser/net/chrome_net_log.h"
#include "chrome/browser/net/chrome_network_delegate.h"
#include "chrome/browser/net/sqlite_persistent_cookie_store.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "content/browser/browser_thread.h"
#include "content/browser/resource_context.h"
#include "net/ftp/ftp_network_layer.h"
#include "net/http/http_cache.h"

ProfileImplIOData::Handle::Handle(Profile* profile)
    : io_data_(new ProfileImplIOData),
      profile_(profile),
      initialized_(false) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(profile);
}

ProfileImplIOData::Handle::~Handle() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (main_request_context_getter_)
    main_request_context_getter_->CleanupOnUIThread();
  if (media_request_context_getter_)
    media_request_context_getter_->CleanupOnUIThread();
  if (extensions_request_context_getter_)
    extensions_request_context_getter_->CleanupOnUIThread();

  // Clean up all isolated app request contexts.
  for (ChromeURLRequestContextGetterMap::iterator iter =
           app_request_context_getter_map_.begin();
       iter != app_request_context_getter_map_.end();
       ++iter) {
    iter->second->CleanupOnUIThread();
  }

  io_data_->ShutdownOnUIThread();
}

void ProfileImplIOData::Handle::Init(const FilePath& cookie_path,
                                     const FilePath& cache_path,
                                     int cache_max_size,
                                     const FilePath& media_cache_path,
                                     int media_cache_max_size,
                                     const FilePath& extensions_cookie_path,
                                     const FilePath& app_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!io_data_->lazy_params_.get());
  LazyParams* lazy_params = new LazyParams;

  lazy_params->cookie_path = cookie_path;
  lazy_params->cache_path = cache_path;
  lazy_params->cache_max_size = cache_max_size;
  lazy_params->media_cache_path = media_cache_path;
  lazy_params->media_cache_max_size = media_cache_max_size;
  lazy_params->extensions_cookie_path = extensions_cookie_path;

  io_data_->lazy_params_.reset(lazy_params);

  // Keep track of isolated app path separately so we can use it on demand.
  io_data_->app_path_ = app_path;
}

const content::ResourceContext&
ProfileImplIOData::Handle::GetResourceContext() const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  LazyInitialize();
  return io_data_->GetResourceContext();
}

scoped_refptr<ChromeURLRequestContextGetter>
ProfileImplIOData::Handle::GetMainRequestContextGetter() const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  LazyInitialize();
  if (!main_request_context_getter_) {
    main_request_context_getter_ =
        ChromeURLRequestContextGetter::CreateOriginal(
            profile_, io_data_);
  }
  return main_request_context_getter_;
}

scoped_refptr<ChromeURLRequestContextGetter>
ProfileImplIOData::Handle::GetMediaRequestContextGetter() const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  LazyInitialize();
  if (!media_request_context_getter_) {
    media_request_context_getter_ =
        ChromeURLRequestContextGetter::CreateOriginalForMedia(
            profile_, io_data_);
  }
  return media_request_context_getter_;
}

scoped_refptr<ChromeURLRequestContextGetter>
ProfileImplIOData::Handle::GetExtensionsRequestContextGetter() const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  LazyInitialize();
  if (!extensions_request_context_getter_) {
    extensions_request_context_getter_ =
        ChromeURLRequestContextGetter::CreateOriginalForExtensions(
            profile_, io_data_);
  }
  return extensions_request_context_getter_;
}

scoped_refptr<ChromeURLRequestContextGetter>
ProfileImplIOData::Handle::GetIsolatedAppRequestContextGetter(
    const std::string& app_id) const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!app_id.empty());
  LazyInitialize();

  // Keep a map of request context getters, one per requested app ID.
  ChromeURLRequestContextGetterMap::iterator iter =
      app_request_context_getter_map_.find(app_id);
  if (iter != app_request_context_getter_map_.end())
    return iter->second;

  ChromeURLRequestContextGetter* context =
      ChromeURLRequestContextGetter::CreateOriginalForIsolatedApp(
          profile_, io_data_, app_id);
  app_request_context_getter_map_[app_id] = context;

  return context;
}

void ProfileImplIOData::Handle::LazyInitialize() const {
  if (!initialized_) {
    io_data_->InitializeProfileParams(profile_);
    ChromeNetworkDelegate::InitializeReferrersEnabled(
        io_data_->enable_referrers(), profile_->GetPrefs());
    initialized_ = true;
  }
}

ProfileImplIOData::LazyParams::LazyParams()
    : cache_max_size(0),
      media_cache_max_size(0) {}
ProfileImplIOData::LazyParams::~LazyParams() {}

ProfileImplIOData::ProfileImplIOData()
    : ProfileIOData(false),
      clear_local_state_on_exit_(false) {}
ProfileImplIOData::~ProfileImplIOData() {
  STLDeleteValues(&app_http_factory_map_);
}

void ProfileImplIOData::LazyInitializeInternal(
    ProfileParams* profile_params) const {
  // Keep track of clear_local_state_on_exit for isolated apps.
  clear_local_state_on_exit_ = profile_params->clear_local_state_on_exit;

  ChromeURLRequestContext* main_context = main_request_context();
  ChromeURLRequestContext* extensions_context = extensions_request_context();
  media_request_context_ = new RequestContext;

  IOThread* const io_thread = profile_params->io_thread;
  IOThread::Globals* const io_thread_globals = io_thread->globals();
  const CommandLine& command_line = *CommandLine::ForCurrentProcess();
  bool record_mode = chrome::kRecordModeEnabled &&
                     command_line.HasSwitch(switches::kRecordMode);
  bool playback_mode = command_line.HasSwitch(switches::kPlaybackMode);

  // Initialize context members.

  ApplyProfileParamsToContext(main_context);
  ApplyProfileParamsToContext(media_request_context_);
  ApplyProfileParamsToContext(extensions_context);

  main_context->set_cookie_policy(cookie_policy());
  media_request_context_->set_cookie_policy(cookie_policy());
  extensions_context->set_cookie_policy(cookie_policy());

  main_context->set_net_log(io_thread->net_log());
  media_request_context_->set_net_log(io_thread->net_log());
  extensions_context->set_net_log(io_thread->net_log());

  main_context->set_network_delegate(network_delegate());
  media_request_context_->set_network_delegate(network_delegate());

  main_context->set_host_resolver(
      io_thread_globals->host_resolver.get());
  media_request_context_->set_host_resolver(
      io_thread_globals->host_resolver.get());
  main_context->set_cert_verifier(
      io_thread_globals->cert_verifier.get());
  media_request_context_->set_cert_verifier(
      io_thread_globals->cert_verifier.get());
  main_context->set_dnsrr_resolver(
      io_thread_globals->dnsrr_resolver.get());
  media_request_context_->set_dnsrr_resolver(
      io_thread_globals->dnsrr_resolver.get());
  main_context->set_http_auth_handler_factory(
      io_thread_globals->http_auth_handler_factory.get());
  media_request_context_->set_http_auth_handler_factory(
      io_thread_globals->http_auth_handler_factory.get());

  main_context->set_dns_cert_checker(dns_cert_checker());
  media_request_context_->set_dns_cert_checker(dns_cert_checker());

  main_context->set_proxy_service(proxy_service());
  media_request_context_->set_proxy_service(proxy_service());

  net::HttpCache::DefaultBackend* main_backend =
      new net::HttpCache::DefaultBackend(
          net::DISK_CACHE,
          lazy_params_->cache_path,
          lazy_params_->cache_max_size,
          BrowserThread::GetMessageLoopProxyForThread(BrowserThread::CACHE));
  net::HttpCache* main_cache = new net::HttpCache(
      main_context->host_resolver(),
      main_context->cert_verifier(),
      main_context->dnsrr_resolver(),
      main_context->dns_cert_checker(),
      main_context->proxy_service(),
      main_context->ssl_config_service(),
      main_context->http_auth_handler_factory(),
      main_context->network_delegate(),
      main_context->net_log(),
      main_backend);

  net::HttpCache::DefaultBackend* media_backend =
      new net::HttpCache::DefaultBackend(
          net::MEDIA_CACHE, lazy_params_->media_cache_path,
          lazy_params_->media_cache_max_size,
          BrowserThread::GetMessageLoopProxyForThread(BrowserThread::CACHE));
  net::HttpNetworkSession* main_network_session = main_cache->GetSession();
  net::HttpCache* media_cache =
      new net::HttpCache(main_network_session, media_backend);

  scoped_refptr<net::CookieStore> cookie_store = NULL;
  if (record_mode || playback_mode) {
    // Don't use existing cookies and use an in-memory store.
    cookie_store = new net::CookieMonster(
        NULL, profile_params->cookie_monster_delegate);
    main_cache->set_mode(
        record_mode ? net::HttpCache::RECORD : net::HttpCache::PLAYBACK);
  }

  // setup cookie store
  if (!cookie_store) {
    DCHECK(!lazy_params_->cookie_path.empty());

    scoped_refptr<SQLitePersistentCookieStore> cookie_db =
        new SQLitePersistentCookieStore(lazy_params_->cookie_path);
    cookie_db->SetClearLocalStateOnExit(
        profile_params->clear_local_state_on_exit);
    cookie_store =
        new net::CookieMonster(cookie_db.get(),
                               profile_params->cookie_monster_delegate);
  }

  net::CookieMonster* extensions_cookie_store =
      new net::CookieMonster(
          new SQLitePersistentCookieStore(
              lazy_params_->extensions_cookie_path), NULL);
  // Enable cookies for devtools and extension URLs.
  const char* schemes[] = {chrome::kChromeDevToolsScheme,
                           chrome::kExtensionScheme};
  extensions_cookie_store->SetCookieableSchemes(schemes, 2);

  main_context->set_cookie_store(cookie_store);
  media_request_context_->set_cookie_store(cookie_store);
  extensions_context->set_cookie_store(
      extensions_cookie_store);

  main_http_factory_.reset(main_cache);
  media_http_factory_.reset(media_cache);
  main_context->set_http_transaction_factory(main_cache);
  media_request_context_->set_http_transaction_factory(media_cache);

  main_context->set_ftp_transaction_factory(
      new net::FtpNetworkLayer(io_thread_globals->host_resolver.get()));

  lazy_params_.reset();
}

scoped_refptr<ProfileIOData::RequestContext>
ProfileImplIOData::InitializeAppRequestContext(
    scoped_refptr<ChromeURLRequestContext> main_context,
    const std::string& app_id) const {
  scoped_refptr<ProfileIOData::RequestContext> context = new RequestContext;

  // Copy most state from the main context.
  context->CopyFrom(main_context);

  FilePath app_path = app_path_.AppendASCII(app_id);
  FilePath cookie_path = app_path.Append(chrome::kCookieFilename);
  FilePath cache_path = app_path.Append(chrome::kCacheDirname);
  // TODO(creis): Determine correct cache size.
  int cache_max_size = 0;

  const CommandLine& command_line = *CommandLine::ForCurrentProcess();
  bool record_mode = chrome::kRecordModeEnabled &&
                     command_line.HasSwitch(switches::kRecordMode);
  bool playback_mode = command_line.HasSwitch(switches::kPlaybackMode);

  // Use a separate HTTP disk cache for isolated apps.
  net::HttpCache::DefaultBackend* app_backend =
      new net::HttpCache::DefaultBackend(
          net::DISK_CACHE,
          cache_path,
          cache_max_size,
          BrowserThread::GetMessageLoopProxyForThread(BrowserThread::CACHE));
  net::HttpNetworkSession* main_network_session =
      main_http_factory_->GetSession();
  net::HttpCache* app_http_cache =
      new net::HttpCache(main_network_session, app_backend);

  scoped_refptr<net::CookieStore> cookie_store = NULL;
  if (record_mode || playback_mode) {
    // Don't use existing cookies and use an in-memory store.
    // TODO(creis): We should have a cookie delegate for notifying the cookie
    // extensions API, but we need to update it to understand isolated apps
    // first.
    cookie_store = new net::CookieMonster(NULL, NULL);
    app_http_cache->set_mode(
        record_mode ? net::HttpCache::RECORD : net::HttpCache::PLAYBACK);
  }

  // Use an app-specific cookie store.
  if (!cookie_store) {
    DCHECK(!cookie_path.empty());

    scoped_refptr<SQLitePersistentCookieStore> cookie_db =
        new SQLitePersistentCookieStore(cookie_path);
    cookie_db->SetClearLocalStateOnExit(clear_local_state_on_exit_);
    // TODO(creis): We should have a cookie delegate for notifying the cookie
    // extensions API, but we need to update it to understand isolated apps
    // first.
    cookie_store = new net::CookieMonster(cookie_db.get(), NULL);
  }

  context->set_cookie_store(cookie_store);

  // Keep track of app_http_cache to delete it when we go away.
  DCHECK(!app_http_factory_map_[app_id]);
  app_http_factory_map_[app_id] = app_http_cache;
  context->set_http_transaction_factory(app_http_cache);

  return context;
}

scoped_refptr<ChromeURLRequestContext>
ProfileImplIOData::AcquireMediaRequestContext() const {
  DCHECK(media_request_context_);
  scoped_refptr<ChromeURLRequestContext> context = media_request_context_;
  media_request_context_->set_profile_io_data(this);
  media_request_context_ = NULL;
  return context;
}

scoped_refptr<ChromeURLRequestContext>
ProfileImplIOData::AcquireIsolatedAppRequestContext(
    scoped_refptr<ChromeURLRequestContext> main_context,
    const std::string& app_id) const {
  // We create per-app contexts on demand, unlike the others above.
  scoped_refptr<RequestContext> app_request_context =
      InitializeAppRequestContext(main_context, app_id);
  DCHECK(app_request_context);
  app_request_context->set_profile_io_data(this);
  return app_request_context;
}
