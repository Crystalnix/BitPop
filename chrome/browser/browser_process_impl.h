// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// When each service is created, we set a flag indicating this. At this point,
// the service initialization could fail or succeed. This allows us to remember
// if we tried to create a service, and not try creating it over and over if
// the creation failed.

#ifndef CHROME_BROWSER_BROWSER_PROCESS_IMPL_H_
#define CHROME_BROWSER_BROWSER_PROCESS_IMPL_H_
#pragma once

#include <string>

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "base/threading/non_thread_safe.h"
#include "base/timer.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/prefs/pref_change_registrar.h"
#include "chrome/browser/prefs/pref_member.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "ipc/ipc_message.h"

class BrowserOnlineStateObserver;
class ChromeNetLog;
class ChromeResourceDispatcherHostDelegate;
class CommandLine;
class RemoteDebuggingServer;
class TabCloseableStateWatcher;

namespace policy {
class BrowserPolicyConnector;
};

// Real implementation of BrowserProcess that creates and returns the services.
class BrowserProcessImpl : public BrowserProcess,
                           public base::NonThreadSafe,
                           public content::NotificationObserver {
 public:
  explicit BrowserProcessImpl(const CommandLine& command_line);
  virtual ~BrowserProcessImpl();

  // Called before the browser threads are created.
  void PreCreateThreads();

  // Called after the threads have been created but before the message loops
  // starts running. Allows the browser process to do any initialization that
  // requires all threads running.
  void PreMainMessageLoopRun();

  // Most cleanup is done by these functions, driven from
  // ChromeBrowserMain based on notifications from the content
  // framework, rather than in the destructor, so that we can
  // interleave cleanup with threads being stopped.
  void StartTearDown();
  void PostDestroyThreads();

  // BrowserProcess methods
  virtual void ResourceDispatcherHostCreated() OVERRIDE;
  virtual void EndSession() OVERRIDE;
  virtual MetricsService* metrics_service() OVERRIDE;
  virtual IOThread* io_thread() OVERRIDE;
  virtual WatchDogThread* watchdog_thread() OVERRIDE;
  virtual ProfileManager* profile_manager() OVERRIDE;
  virtual PrefService* local_state() OVERRIDE;
  virtual ui::Clipboard* clipboard() OVERRIDE;
  virtual net::URLRequestContextGetter* system_request_context() OVERRIDE;
#if defined(OS_CHROMEOS)
  virtual browser::OomPriorityManager* oom_priority_manager() OVERRIDE;
#endif  // defined(OS_CHROMEOS)
  virtual ExtensionEventRouterForwarder*
        extension_event_router_forwarder() OVERRIDE;
  virtual NotificationUIManager* notification_ui_manager() OVERRIDE;
  virtual policy::BrowserPolicyConnector* browser_policy_connector() OVERRIDE;
  virtual IconManager* icon_manager() OVERRIDE;
  virtual ThumbnailGenerator* GetThumbnailGenerator() OVERRIDE;
  virtual AutomationProviderList* GetAutomationProviderList() OVERRIDE;
  virtual void InitDevToolsHttpProtocolHandler(
      Profile* profile,
      const std::string& ip,
      int port,
      const std::string& frontend_url) OVERRIDE;
  virtual unsigned int AddRefModule() OVERRIDE;
  virtual unsigned int ReleaseModule() OVERRIDE;
  virtual bool IsShuttingDown() OVERRIDE;
  virtual printing::PrintJobManager* print_job_manager() OVERRIDE;
  virtual printing::PrintPreviewTabController*
      print_preview_tab_controller() OVERRIDE;
  virtual printing::BackgroundPrintingManager*
      background_printing_manager() OVERRIDE;
  virtual GoogleURLTracker* google_url_tracker() OVERRIDE;
  virtual IntranetRedirectDetector* intranet_redirect_detector() OVERRIDE;
  virtual const std::string& GetApplicationLocale() OVERRIDE;
  virtual void SetApplicationLocale(const std::string& locale) OVERRIDE;
  virtual DownloadStatusUpdater* download_status_updater() OVERRIDE;
  virtual DownloadRequestLimiter* download_request_limiter() OVERRIDE;
  virtual TabCloseableStateWatcher* tab_closeable_state_watcher() OVERRIDE;
  virtual BackgroundModeManager* background_mode_manager() OVERRIDE;
  virtual StatusTray* status_tray() OVERRIDE;
  virtual SafeBrowsingService* safe_browsing_service() OVERRIDE;
  virtual safe_browsing::ClientSideDetectionService*
      safe_browsing_detection_service() OVERRIDE;
  virtual bool plugin_finder_disabled() const OVERRIDE;

  // content::NotificationObserver methods
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

#if (defined(OS_WIN) || defined(OS_LINUX)) && !defined(OS_CHROMEOS)
  virtual void StartAutoupdateTimer() OVERRIDE;
#endif

  virtual ChromeNetLog* net_log() OVERRIDE;
  virtual prerender::PrerenderTracker* prerender_tracker() OVERRIDE;
  virtual MHTMLGenerationManager* mhtml_generation_manager() OVERRIDE;
  virtual ComponentUpdateService* component_updater() OVERRIDE;
  virtual CRLSetFetcher* crl_set_fetcher() OVERRIDE;
  virtual AudioManager* audio_manager() OVERRIDE;

 private:
  void CreateMetricsService();
  void CreateWatchdogThread();
#if defined(OS_CHROMEOS)
  void InitializeWebSocketProxyThread();
#endif
  void CreateTemplateURLService();
  void CreateProfileManager();
  void CreateWebDataService();
  void CreateLocalState();
  void CreateViewedPageTracker();
  void CreateIconManager();
  void CreateGoogleURLTracker();
  void CreateIntranetRedirectDetector();
  void CreateNotificationUIManager();
  void CreateStatusTrayManager();
  void CreateTabCloseableStateWatcher();
  void CreatePrintPreviewTabController();
  void CreateBackgroundPrintingManager();
  void CreateSafeBrowsingService();
  void CreateSafeBrowsingDetectionService();
  void CreateStatusTray();
  void CreateBackgroundModeManager();

  void ApplyDisabledSchemesPolicy();
  void ApplyAllowCrossOriginAuthPromptPolicy();
  void ApplyDefaultBrowserPolicy();

  bool created_metrics_service_;
  scoped_ptr<MetricsService> metrics_service_;

  scoped_ptr<IOThread> io_thread_;

  bool created_watchdog_thread_;
  scoped_ptr<WatchDogThread> watchdog_thread_;

  bool created_profile_manager_;
  scoped_ptr<ProfileManager> profile_manager_;

  bool created_local_state_;
  scoped_ptr<PrefService> local_state_;

  bool created_icon_manager_;
  scoped_ptr<IconManager> icon_manager_;

  scoped_refptr<ExtensionEventRouterForwarder>
      extension_event_router_forwarder_;

  scoped_ptr<RemoteDebuggingServer> remote_debugging_server_;

  bool created_browser_policy_connector_;
  scoped_ptr<policy::BrowserPolicyConnector> browser_policy_connector_;

  scoped_refptr<printing::PrintPreviewTabController>
      print_preview_tab_controller_;

  scoped_ptr<printing::BackgroundPrintingManager> background_printing_manager_;

  scoped_ptr<ui::Clipboard> clipboard_;

  // Manager for desktop notification UI.
  bool created_notification_ui_manager_;
  scoped_ptr<NotificationUIManager> notification_ui_manager_;

  scoped_ptr<AutomationProviderList> automation_provider_list_;

  scoped_ptr<GoogleURLTracker> google_url_tracker_;
  scoped_ptr<IntranetRedirectDetector> intranet_redirect_detector_;

  scoped_ptr<TabCloseableStateWatcher> tab_closeable_state_watcher_;

  scoped_ptr<StatusTray> status_tray_;

  scoped_ptr<BackgroundModeManager> background_mode_manager_;

  bool created_safe_browsing_service_;
  scoped_refptr<SafeBrowsingService> safe_browsing_service_;

  unsigned int module_ref_count_;
  bool did_start_;

  // Ensures that all the print jobs are finished before closing the browser.
  scoped_ptr<printing::PrintJobManager> print_job_manager_;

  std::string locale_;

  bool checked_for_new_frames_;
  bool using_new_frames_;

  // This service just sits around and makes thumbnails for tabs. It does
  // nothing in the constructor so we don't have to worry about lazy init.
  scoped_ptr<ThumbnailGenerator> thumbnail_generator_;

  // Download status updates (like a changing application icon on dock/taskbar)
  // are global per-application. DownloadStatusUpdater does no work in the ctor
  // so we don't have to worry about lazy initialization.
  scoped_ptr<DownloadStatusUpdater> download_status_updater_;

  scoped_refptr<DownloadRequestLimiter> download_request_limiter_;

  // Ensures that the observers of plugin/print disable/enable state
  // notifications are properly added and removed.
  PrefChangeRegistrar pref_change_registrar_;

  // Lives here so can safely log events on shutdown.
  scoped_ptr<ChromeNetLog> net_log_;

  // Ordered before resource_dispatcher_host_delegate_ due to destruction
  // ordering.
  scoped_ptr<prerender::PrerenderTracker> prerender_tracker_;

  scoped_ptr<ChromeResourceDispatcherHostDelegate>
      resource_dispatcher_host_delegate_;

  content::NotificationRegistrar notification_registrar_;

  scoped_refptr<MHTMLGenerationManager> mhtml_generation_manager_;

  // Monitors the state of the 'DisablePluginFinder' policy.
  scoped_ptr<BooleanPrefMember> plugin_finder_disabled_pref_;

#if (defined(OS_WIN) || defined(OS_LINUX)) && !defined(OS_CHROMEOS)
  base::RepeatingTimer<BrowserProcessImpl> autoupdate_timer_;

  // Gets called by autoupdate timer to see if browser needs restart and can be
  // restarted, and if that's the case, restarts the browser.
  void OnAutoupdateTimer();
  bool CanAutorestartForUpdate() const;
  void RestartBackgroundInstance();
#endif  // defined(OS_WIN) || defined(OS_LINUX)

#if defined(OS_CHROMEOS)
  scoped_ptr<browser::OomPriorityManager> oom_priority_manager_;
#endif

  // Per-process listener for online state changes.
  scoped_ptr<BrowserOnlineStateObserver> online_state_observer_;

#if !defined(OS_CHROMEOS)
  scoped_ptr<ComponentUpdateService> component_updater_;

  scoped_refptr<CRLSetFetcher> crl_set_fetcher_;
#endif

  scoped_refptr<AudioManager> audio_manager_;

  DISALLOW_COPY_AND_ASSIGN(BrowserProcessImpl);
};

#endif  // CHROME_BROWSER_BROWSER_PROCESS_IMPL_H_
