// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/profiles/profile_io_data.h"

#include <string>

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/stl_util.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/stringprintf.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/content_settings/cookie_settings.h"
#include "chrome/browser/content_settings/host_content_settings_map.h"
#include "chrome/browser/custom_handlers/protocol_handler_registry.h"
#include "chrome/browser/custom_handlers/protocol_handler_registry_factory.h"
#include "chrome/browser/download/download_service.h"
#include "chrome/browser/download/download_service_factory.h"
#include "chrome/browser/extensions/extension_info_map.h"
#include "chrome/browser/extensions/extension_protocols.h"
#include "chrome/browser/extensions/extension_resource_protocols.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/io_thread.h"
#include "chrome/browser/net/about_protocol_handler.h"
#include "chrome/browser/net/chrome_cookie_notification_details.h"
#include "chrome/browser/net/chrome_fraudulent_certificate_reporter.h"
#include "chrome/browser/net/chrome_http_user_agent_settings.h"
#include "chrome/browser/net/chrome_net_log.h"
#include "chrome/browser/net/chrome_network_delegate.h"
#include "chrome/browser/net/load_time_stats.h"
#include "chrome/browser/net/proxy_service_factory.h"
#include "chrome/browser/net/resource_prefetch_predictor_observer.h"
#include "chrome/browser/net/transport_security_persister.h"
#include "chrome/browser/notifications/desktop_notification_service_factory.h"
#include "chrome/browser/policy/url_blacklist_manager.h"
#include "chrome/browser/predictors/resource_prefetch_predictor.h"
#include "chrome/browser/predictors/resource_prefetch_predictor_factory.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/signin/signin_names_io_thread.h"
#include "chrome/browser/ui/webui/chrome_url_data_manager_backend.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/host_zoom_map.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/resource_context.h"
#include "extensions/common/constants.h"
#include "net/base/server_bound_cert_service.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_monster.h"
#include "net/http/http_transaction_factory.h"
#include "net/http/http_util.h"
#include "net/proxy/proxy_config_service_fixed.h"
#include "net/proxy/proxy_script_fetcher_impl.h"
#include "net/proxy/proxy_service.h"
#include "net/url_request/data_protocol_handler.h"
#include "net/url_request/file_protocol_handler.h"
#include "net/url_request/ftp_protocol_handler.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_job_factory_impl.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/drive/drive_protocol_handler.h"
#include "chrome/browser/chromeos/proxy_config_service_impl.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/chromeos/settings/cros_settings_names.h"
#endif  // defined(OS_CHROMEOS)

using content::BrowserContext;
using content::BrowserThread;
using content::ResourceContext;

namespace {

// ----------------------------------------------------------------------------
// CookieMonster::Delegate implementation
// ----------------------------------------------------------------------------
class ChromeCookieMonsterDelegate : public net::CookieMonster::Delegate {
 public:
  explicit ChromeCookieMonsterDelegate(
      const base::Callback<Profile*(void)>& profile_getter)
      : profile_getter_(profile_getter) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  }

  // net::CookieMonster::Delegate implementation.
  virtual void OnCookieChanged(
      const net::CanonicalCookie& cookie,
      bool removed,
      net::CookieMonster::Delegate::ChangeCause cause) {
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(&ChromeCookieMonsterDelegate::OnCookieChangedAsyncHelper,
                   this, cookie, removed, cause));
  }

 private:
  virtual ~ChromeCookieMonsterDelegate() {}

  void OnCookieChangedAsyncHelper(
      const net::CanonicalCookie& cookie,
      bool removed,
      net::CookieMonster::Delegate::ChangeCause cause) {
    Profile* profile = profile_getter_.Run();
    if (profile) {
      ChromeCookieDetails cookie_details(&cookie, removed, cause);
      content::NotificationService::current()->Notify(
          chrome::NOTIFICATION_COOKIE_CHANGED,
          content::Source<Profile>(profile),
          content::Details<ChromeCookieDetails>(&cookie_details));
    }
  }

  const base::Callback<Profile*(void)> profile_getter_;
};

Profile* GetProfileOnUI(ProfileManager* profile_manager, Profile* profile) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(profile);
  if (profile_manager->IsValidProfile(profile))
    return profile;
  return NULL;
}

}  // namespace

void ProfileIOData::InitializeOnUIThread(Profile* profile) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  PrefService* pref_service = profile->GetPrefs();
  PrefService* local_state_pref_service = g_browser_process->local_state();

  scoped_ptr<ProfileParams> params(new ProfileParams);
  params->path = profile->GetPath();

  params->io_thread = g_browser_process->io_thread();

  params->cookie_settings = CookieSettings::Factory::GetForProfile(profile);
  params->ssl_config_service = profile->GetSSLConfigService();
  base::Callback<Profile*(void)> profile_getter =
      base::Bind(&GetProfileOnUI, g_browser_process->profile_manager(),
                 profile);
  params->cookie_monster_delegate =
      new ChromeCookieMonsterDelegate(profile_getter);
  params->extension_info_map =
      extensions::ExtensionSystem::Get(profile)->info_map();

  if (predictors::ResourcePrefetchPredictor* predictor =
          predictors::ResourcePrefetchPredictorFactory::GetForProfile(
              profile)) {
    resource_prefetch_predictor_observer_.reset(
        new chrome_browser_net::ResourcePrefetchPredictorObserver(predictor));
  }

#if defined(ENABLE_NOTIFICATIONS)
  params->notification_service =
      DesktopNotificationServiceFactory::GetForProfile(profile);
#endif

  ProtocolHandlerRegistry* protocol_handler_registry =
      ProtocolHandlerRegistryFactory::GetForProfile(profile);
  DCHECK(protocol_handler_registry);

  // The profile instance is only available here in the InitializeOnUIThread
  // method, so we create the url interceptor here, then save it for
  // later delivery to the job factory in LazyInitialize.
  params->protocol_handler_interceptor.reset(
      protocol_handler_registry->CreateURLInterceptor());

  ChromeProxyConfigService* proxy_config_service =
      ProxyServiceFactory::CreateProxyConfigService(true);
  params->proxy_config_service.reset(proxy_config_service);
  profile->GetProxyConfigTracker()->SetChromeProxyConfigService(
      proxy_config_service);
  params->profile = profile;
  profile_params_.reset(params.release());

  ChromeNetworkDelegate::InitializePrefsOnUIThread(
      &enable_referrers_,
      &enable_do_not_track_,
      &force_safesearch_,
      pref_service);

#if defined(ENABLE_PRINTING)
  printing_enabled_.Init(prefs::kPrintingEnabled, pref_service);
  printing_enabled_.MoveToThread(
      BrowserThread::GetMessageLoopProxyForThread(BrowserThread::IO));
#endif
  chrome_http_user_agent_settings_.reset(
      new ChromeHttpUserAgentSettings(pref_service));

  // These members are used only for one click sign in, which is not enabled
  // in incognito mode.  So no need to initialize them.
  if (!is_incognito()) {
    signin_names_.reset(new SigninNamesOnIOThread());

    google_services_username_.Init(prefs::kGoogleServicesUsername,
                                   pref_service);
    google_services_username_.MoveToThread(
        BrowserThread::GetMessageLoopProxyForThread(BrowserThread::IO));

    google_services_username_pattern_.Init(
        prefs::kGoogleServicesUsernamePattern, local_state_pref_service);
    google_services_username_pattern_.MoveToThread(
        BrowserThread::GetMessageLoopProxyForThread(BrowserThread::IO));

    reverse_autologin_enabled_.Init(
        prefs::kReverseAutologinEnabled, pref_service);
    reverse_autologin_enabled_.MoveToThread(
        BrowserThread::GetMessageLoopProxyForThread(BrowserThread::IO));

    one_click_signin_rejected_email_list_.Init(
        prefs::kReverseAutologinRejectedEmailList, pref_service);
    one_click_signin_rejected_email_list_.MoveToThread(
        BrowserThread::GetMessageLoopProxyForThread(BrowserThread::IO));
  }

  // The URLBlacklistManager has to be created on the UI thread to register
  // observers of |pref_service|, and it also has to clean up on
  // ShutdownOnUIThread to release these observers on the right thread.
  // Don't pass it in |profile_params_| to make sure it is correctly cleaned up,
  // in particular when this ProfileIOData isn't |initialized_| during deletion.
#if defined(ENABLE_CONFIGURATION_POLICY)
  url_blacklist_manager_.reset(new policy::URLBlacklistManager(pref_service));
#endif

  initialized_on_UI_thread_ = true;

  // We need to make sure that content initializes its own data structures that
  // are associated with each ResourceContext because we might post this
  // object to the IO thread after this function.
  BrowserContext::EnsureResourceContextInitialized(profile);
}

ProfileIOData::MediaRequestContext::MediaRequestContext(
    chrome_browser_net::LoadTimeStats* load_time_stats)
    : ChromeURLRequestContext(ChromeURLRequestContext::CONTEXT_TYPE_MEDIA,
                              load_time_stats) {
}

void ProfileIOData::MediaRequestContext::SetHttpTransactionFactory(
    scoped_ptr<net::HttpTransactionFactory> http_factory) {
  http_factory_ = http_factory.Pass();
  set_http_transaction_factory(http_factory_.get());
}

ProfileIOData::MediaRequestContext::~MediaRequestContext() {}

ProfileIOData::AppRequestContext::AppRequestContext(
    chrome_browser_net::LoadTimeStats* load_time_stats)
    : ChromeURLRequestContext(ChromeURLRequestContext::CONTEXT_TYPE_APP,
                              load_time_stats) {
}

void ProfileIOData::AppRequestContext::SetCookieStore(
    net::CookieStore* cookie_store) {
  cookie_store_ = cookie_store;
  set_cookie_store(cookie_store);
}

void ProfileIOData::AppRequestContext::SetHttpTransactionFactory(
    scoped_ptr<net::HttpTransactionFactory> http_factory) {
  http_factory_ = http_factory.Pass();
  set_http_transaction_factory(http_factory_.get());
}

void ProfileIOData::AppRequestContext::SetJobFactory(
    scoped_ptr<net::URLRequestJobFactory> job_factory) {
  job_factory_ = job_factory.Pass();
  set_job_factory(job_factory_.get());
}

ProfileIOData::AppRequestContext::~AppRequestContext() {}

ProfileIOData::ProfileParams::ProfileParams()
    : io_thread(NULL),
#if defined(ENABLE_NOTIFICATIONS)
      notification_service(NULL),
#endif
      profile(NULL) {
}

ProfileIOData::ProfileParams::~ProfileParams() {}

ProfileIOData::ProfileIOData(bool is_incognito)
    : initialized_(false),
      ALLOW_THIS_IN_INITIALIZER_LIST(
          resource_context_(new ResourceContext(this))),
      initialized_on_UI_thread_(false),
      is_incognito_(is_incognito) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
}

ProfileIOData::~ProfileIOData() {
  if (BrowserThread::IsMessageLoopValid(BrowserThread::IO))
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  if (main_request_context_.get())
    main_request_context_->AssertNoURLRequests();
  if (extensions_request_context_.get())
    extensions_request_context_->AssertNoURLRequests();
  for (URLRequestContextMap::iterator it = app_request_context_map_.begin();
       it != app_request_context_map_.end(); ++it) {
    it->second->AssertNoURLRequests();
    delete it->second;
  }
  for (URLRequestContextMap::iterator it =
           isolated_media_request_context_map_.begin();
       it != isolated_media_request_context_map_.end(); ++it) {
    it->second->AssertNoURLRequests();
    delete it->second;
  }
}

// static
ProfileIOData* ProfileIOData::FromResourceContext(
    content::ResourceContext* rc) {
  return (static_cast<ResourceContext*>(rc))->io_data_;
}

// static
bool ProfileIOData::IsHandledProtocol(const std::string& scheme) {
  DCHECK_EQ(scheme, StringToLowerASCII(scheme));
  static const char* const kProtocolList[] = {
    extensions::kExtensionScheme,
    chrome::kChromeUIScheme,
    chrome::kChromeDevToolsScheme,
#if defined(OS_CHROMEOS)
    chrome::kMetadataScheme,
    chrome::kDriveScheme,
#endif  // defined(OS_CHROMEOS)
    chrome::kBlobScheme,
    chrome::kFileSystemScheme,
    chrome::kExtensionResourceScheme,
  };
  for (size_t i = 0; i < arraysize(kProtocolList); ++i) {
    if (scheme == kProtocolList[i])
      return true;
  }
  return net::URLRequest::IsHandledProtocol(scheme);
}

bool ProfileIOData::IsHandledURL(const GURL& url) {
  if (!url.is_valid()) {
    // We handle error cases.
    return true;
  }

  return IsHandledProtocol(url.scheme());
}

content::ResourceContext* ProfileIOData::GetResourceContext() const {
  return resource_context_.get();
}

ChromeURLDataManagerBackend*
ProfileIOData::GetChromeURLDataManagerBackend() const {
  LazyInitialize();
  return chrome_url_data_manager_backend_.get();
}

ChromeURLRequestContext*
ProfileIOData::GetMainRequestContext() const {
  LazyInitialize();
  return main_request_context_.get();
}

ChromeURLRequestContext*
ProfileIOData::GetMediaRequestContext() const {
  LazyInitialize();
  ChromeURLRequestContext* context =
      AcquireMediaRequestContext();
  DCHECK(context);
  return context;
}

ChromeURLRequestContext*
ProfileIOData::GetExtensionsRequestContext() const {
  LazyInitialize();
  return extensions_request_context_.get();
}

ChromeURLRequestContext*
ProfileIOData::GetIsolatedAppRequestContext(
    ChromeURLRequestContext* main_context,
    const StoragePartitionDescriptor& partition_descriptor,
    scoped_ptr<net::URLRequestJobFactory::Interceptor>
        protocol_handler_interceptor) const {
  LazyInitialize();
  ChromeURLRequestContext* context = NULL;
  if (ContainsKey(app_request_context_map_, partition_descriptor)) {
    context = app_request_context_map_[partition_descriptor];
  } else {
    context = AcquireIsolatedAppRequestContext(
        main_context, partition_descriptor,
        protocol_handler_interceptor.Pass());
    app_request_context_map_[partition_descriptor] = context;
  }
  DCHECK(context);
  return context;
}

ChromeURLRequestContext*
ProfileIOData::GetIsolatedMediaRequestContext(
    ChromeURLRequestContext* app_context,
    const StoragePartitionDescriptor& partition_descriptor) const {
  LazyInitialize();
  ChromeURLRequestContext* context = NULL;
  if (ContainsKey(isolated_media_request_context_map_, partition_descriptor)) {
    context = isolated_media_request_context_map_[partition_descriptor];
  } else {
    context = AcquireIsolatedMediaRequestContext(app_context,
                                                 partition_descriptor);
    isolated_media_request_context_map_[partition_descriptor] = context;
  }
  DCHECK(context);
  return context;
}

ExtensionInfoMap* ProfileIOData::GetExtensionInfoMap() const {
  DCHECK(extension_info_map_) << "ExtensionSystem not initialized";
  return extension_info_map_;
}

CookieSettings* ProfileIOData::GetCookieSettings() const {
  return cookie_settings_;
}

#if defined(ENABLE_NOTIFICATIONS)
DesktopNotificationService* ProfileIOData::GetNotificationService() const {
  return notification_service_;
}
#endif

void ProfileIOData::InitializeMetricsEnabledStateOnUIThread() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
#if defined(OS_CHROMEOS)
  // Just fetch the value from ChromeOS' settings while we're on the UI thread.
  // TODO(stevet): For now, this value is only set on profile initialization.
  // We will want to do something similar to the PrefMember method below in the
  // future to more accurately capture this state.
  chromeos::CrosSettings::Get()->GetBoolean(chromeos::kStatsReportingPref,
                                            &enable_metrics_);
#else
  // Prep the PrefMember and send it to the IO thread, since this value will be
  // read from there.
  enable_metrics_.Init(prefs::kMetricsReportingEnabled,
                       g_browser_process->local_state());
  enable_metrics_.MoveToThread(
      BrowserThread::GetMessageLoopProxyForThread(BrowserThread::IO));
#endif  // defined(OS_CHROMEOS)
}

bool ProfileIOData::GetMetricsEnabledStateOnIOThread() const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
#if defined(OS_CHROMEOS)
  return enable_metrics_;
#else
  return enable_metrics_.GetValue();
#endif  // defined(OS_CHROMEOS)
}

net::HttpServerProperties* ProfileIOData::http_server_properties() const {
  return http_server_properties_.get();
}

void ProfileIOData::set_http_server_properties(
    net::HttpServerProperties* http_server_properties) const {
  http_server_properties_.reset(http_server_properties);
}

ProfileIOData::ResourceContext::ResourceContext(ProfileIOData* io_data)
    : io_data_(io_data),
      host_resolver_(NULL),
      request_context_(NULL) {
  DCHECK(io_data);
}

ProfileIOData::ResourceContext::~ResourceContext() {}

void ProfileIOData::ResourceContext::EnsureInitialized() {
  io_data_->LazyInitialize();
}

net::HostResolver* ProfileIOData::ResourceContext::GetHostResolver()  {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  EnsureInitialized();
  return host_resolver_;
}

net::URLRequestContext* ProfileIOData::ResourceContext::GetRequestContext()  {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  EnsureInitialized();
  return request_context_;
}

// static
std::string ProfileIOData::GetSSLSessionCacheShard() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  // The SSL session cache is partitioned by setting a string. This returns a
  // unique string to partition the SSL session cache. Each time we create a
  // new profile, we'll get a fresh SSL session cache which is separate from
  // the other profiles.
  static unsigned ssl_session_cache_instance = 0;
  return StringPrintf("profile/%u", ssl_session_cache_instance++);
}

void ProfileIOData::LazyInitialize() const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  if (initialized_)
    return;

  // TODO(jhawkins): Remove once crbug.com/102004 is fixed.
  CHECK(initialized_on_UI_thread_);

  // TODO(jhawkins): Return to DCHECK once crbug.com/102004 is fixed.
  CHECK(profile_params_.get());

  IOThread* const io_thread = profile_params_->io_thread;
  IOThread::Globals* const io_thread_globals = io_thread->globals();
  const CommandLine& command_line = *CommandLine::ForCurrentProcess();
  load_time_stats_ = GetLoadTimeStats(io_thread_globals);

  // Create the common request contexts.
  main_request_context_.reset(
      new ChromeURLRequestContext(ChromeURLRequestContext::CONTEXT_TYPE_MAIN,
                                  load_time_stats_));
  extensions_request_context_.reset(
      new ChromeURLRequestContext(
          ChromeURLRequestContext::CONTEXT_TYPE_EXTENSIONS,
          load_time_stats_));

  chrome_url_data_manager_backend_.reset(new ChromeURLDataManagerBackend);

  ChromeNetworkDelegate* network_delegate =
      new ChromeNetworkDelegate(
          io_thread_globals->extension_event_router_forwarder.get(),
          &enable_referrers_);
  network_delegate->set_extension_info_map(profile_params_->extension_info_map);
  network_delegate->set_url_blacklist_manager(url_blacklist_manager_.get());
  network_delegate->set_profile(profile_params_->profile);
  network_delegate->set_cookie_settings(profile_params_->cookie_settings);
  network_delegate->set_enable_do_not_track(&enable_do_not_track_);
  network_delegate->set_force_google_safe_search(&force_safesearch_);
  network_delegate->set_load_time_stats(load_time_stats_);
  network_delegate_.reset(network_delegate);

  fraudulent_certificate_reporter_.reset(
      new chrome_browser_net::ChromeFraudulentCertificateReporter(
          main_request_context_.get()));

  proxy_service_.reset(
      ProxyServiceFactory::CreateProxyService(
          io_thread->net_log(),
          io_thread_globals->proxy_script_fetcher_context.get(),
          profile_params_->proxy_config_service.release(),
          command_line));

  transport_security_state_.reset(new net::TransportSecurityState());
  transport_security_persister_.reset(
      new TransportSecurityPersister(transport_security_state_.get(),
                                     profile_params_->path,
                                     is_incognito()));
  const std::string& serialized =
      command_line.GetSwitchValueASCII(switches::kHstsHosts);
  transport_security_persister_.get()->DeserializeFromCommandLine(serialized);

  // Take ownership over these parameters.
  cookie_settings_ = profile_params_->cookie_settings;
#if defined(ENABLE_NOTIFICATIONS)
  notification_service_ = profile_params_->notification_service;
#endif
  extension_info_map_ = profile_params_->extension_info_map;

  resource_context_->host_resolver_ = io_thread_globals->host_resolver.get();
  resource_context_->request_context_ = main_request_context_.get();

  if (profile_params_->resource_prefetch_predictor_observer_.get()) {
    resource_prefetch_predictor_observer_.reset(
        profile_params_->resource_prefetch_predictor_observer_.release());
  }

  LazyInitializeInternal(profile_params_.get());

  profile_params_.reset();
  initialized_ = true;
}

void ProfileIOData::ApplyProfileParamsToContext(
    ChromeURLRequestContext* context) const {
  context->set_is_incognito(is_incognito());
  context->set_http_user_agent_settings(
      chrome_http_user_agent_settings_.get());
  context->set_ssl_config_service(profile_params_->ssl_config_service);
}

void ProfileIOData::SetUpJobFactoryDefaults(
    net::URLRequestJobFactoryImpl* job_factory,
    scoped_ptr<net::URLRequestJobFactory::Interceptor>
        protocol_handler_interceptor,
    net::NetworkDelegate* network_delegate,
    net::FtpTransactionFactory* ftp_transaction_factory,
    net::FtpAuthCache* ftp_auth_cache) const {
  // NOTE(willchan): Keep these protocol handlers in sync with
  // ProfileIOData::IsHandledProtocol().
  bool set_protocol = job_factory->SetProtocolHandler(
      chrome::kFileScheme, new net::FileProtocolHandler());
  DCHECK(set_protocol);

  set_protocol = job_factory->SetProtocolHandler(
      chrome::kChromeDevToolsScheme,
      CreateDevToolsProtocolHandler(chrome_url_data_manager_backend(),
                                    network_delegate));
  DCHECK(set_protocol);

  if (protocol_handler_interceptor.get()) {
    job_factory->AddInterceptor(protocol_handler_interceptor.release());
  }

  set_protocol = job_factory->SetProtocolHandler(
      extensions::kExtensionScheme,
      CreateExtensionProtocolHandler(is_incognito(), GetExtensionInfoMap()));
  DCHECK(set_protocol);
  set_protocol = job_factory->SetProtocolHandler(
      chrome::kExtensionResourceScheme,
      CreateExtensionResourceProtocolHandler());
  DCHECK(set_protocol);
  set_protocol = job_factory->SetProtocolHandler(
      chrome::kChromeUIScheme,
      ChromeURLDataManagerBackend::CreateProtocolHandler(
          chrome_url_data_manager_backend_.get()));
  DCHECK(set_protocol);
  set_protocol = job_factory->SetProtocolHandler(
      chrome::kDataScheme, new net::DataProtocolHandler());
  DCHECK(set_protocol);
#if defined(OS_CHROMEOS)
  if (!is_incognito()) {
    set_protocol = job_factory->SetProtocolHandler(
        chrome::kDriveScheme, new drive::DriveProtocolHandler());
    DCHECK(set_protocol);
  }
#endif  // defined(OS_CHROMEOS)

  job_factory->SetProtocolHandler(
      chrome::kAboutScheme,
      new chrome_browser_net::AboutProtocolHandler());
#if !defined(DISABLE_FTP_SUPPORT)
  DCHECK(ftp_transaction_factory);
  job_factory->SetProtocolHandler(
      chrome::kFtpScheme,
      new net::FtpProtocolHandler(ftp_transaction_factory,
                                  ftp_auth_cache));
#endif  // !defined(DISABLE_FTP_SUPPORT)
}

void ProfileIOData::ShutdownOnUIThread() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (signin_names_)
    signin_names_->ReleaseResourcesOnUIThread();

  google_services_username_.Destroy();
  google_services_username_pattern_.Destroy();
  reverse_autologin_enabled_.Destroy();
  one_click_signin_rejected_email_list_.Destroy();
  enable_referrers_.Destroy();
  enable_do_not_track_.Destroy();
  force_safesearch_.Destroy();
#if !defined(OS_CHROMEOS)
  enable_metrics_.Destroy();
#endif
  safe_browsing_enabled_.Destroy();
  printing_enabled_.Destroy();
  session_startup_pref_.Destroy();
#if defined(ENABLE_CONFIGURATION_POLICY)
  if (url_blacklist_manager_.get())
    url_blacklist_manager_->ShutdownOnUIThread();
#endif
  if (chrome_http_user_agent_settings_.get())
    chrome_http_user_agent_settings_->CleanupOnUIThread();
  bool posted = BrowserThread::DeleteSoon(BrowserThread::IO, FROM_HERE, this);
  if (!posted)
    delete this;
}

void ProfileIOData::set_server_bound_cert_service(
    net::ServerBoundCertService* server_bound_cert_service) const {
  server_bound_cert_service_.reset(server_bound_cert_service);
}

void ProfileIOData::DestroyResourceContext() {
  resource_context_.reset();
}

void ProfileIOData::PopulateNetworkSessionParams(
    const ProfileParams* profile_params,
    net::HttpNetworkSession::Params* params) const {

  ChromeURLRequestContext* context = main_request_context();

  IOThread* const io_thread = profile_params->io_thread;

  io_thread->InitializeNetworkSessionParams(params);

  params->host_resolver = context->host_resolver();
  params->cert_verifier = context->cert_verifier();
  params->server_bound_cert_service = context->server_bound_cert_service();
  params->transport_security_state = context->transport_security_state();
  params->proxy_service = context->proxy_service();
  params->ssl_session_cache_shard = GetSSLSessionCacheShard();
  params->ssl_config_service = context->ssl_config_service();
  params->http_auth_handler_factory = context->http_auth_handler_factory();
  params->network_delegate = context->network_delegate();
  params->http_server_properties = context->http_server_properties();
  params->net_log = context->net_log();
}

void ProfileIOData::SetCookieSettingsForTesting(
    CookieSettings* cookie_settings) {
  DCHECK(!cookie_settings_.get());
  cookie_settings_ = cookie_settings;
}

void ProfileIOData::set_signin_names_for_testing(
    SigninNamesOnIOThread* signin_names) {
  signin_names_.reset(signin_names);
}
