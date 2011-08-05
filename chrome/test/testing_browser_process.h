// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// An implementation of BrowserProcess for unit tests that fails for most
// services. By preventing creation of services, we reduce dependencies and
// keep the profile clean. Clients of this class must handle the NULL return
// value, however.

#ifndef CHROME_TEST_TESTING_BROWSER_PROCESS_H_
#define CHROME_TEST_TESTING_BROWSER_PROCESS_H_
#pragma once

#include <string>

#include "base/memory/scoped_ptr.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "content/common/notification_service.h"

class BackgroundModeManager;
class IOThread;
class GoogleURLTracker;
class NotificationUIManager;
class PrefService;
class WatchDogThread;

namespace base {
class WaitableEvent;
}

namespace policy {
class BrowserPolicyConnector;
}

namespace prerender {
class PrerenderTracker;
}

namespace ui {
class Clipboard;
}

class TestingBrowserProcess : public BrowserProcess {
 public:
  TestingBrowserProcess();
  virtual ~TestingBrowserProcess();

  virtual void EndSession();

  virtual ResourceDispatcherHost* resource_dispatcher_host();

  virtual MetricsService* metrics_service();

  virtual IOThread* io_thread();

#if defined(OS_LINUX)
  virtual base::Thread* background_x11_thread();
#endif

  virtual base::Thread* file_thread();

  virtual base::Thread* db_thread();

  virtual base::Thread* cache_thread();

  virtual WatchDogThread* watchdog_thread();

#if defined(OS_CHROMEOS)
  virtual base::Thread* web_socket_proxy_thread();
#endif

  virtual ProfileManager* profile_manager();

  virtual PrefService* local_state();

  virtual policy::BrowserPolicyConnector* browser_policy_connector();

  virtual IconManager* icon_manager();

  virtual ThumbnailGenerator* GetThumbnailGenerator();

  virtual DevToolsManager* devtools_manager();

  virtual SidebarManager* sidebar_manager();

  virtual TabCloseableStateWatcher* tab_closeable_state_watcher();

  virtual BackgroundModeManager* background_mode_manager();

  virtual StatusTray* status_tray();

  virtual safe_browsing::ClientSideDetectionService*
      safe_browsing_detection_service();

  virtual net::URLRequestContextGetter* system_request_context();

#if defined(OS_CHROMEOS)
  virtual chromeos::ProxyConfigServiceImpl*
      chromeos_proxy_config_service_impl();
#endif  // defined(OS_CHROMEOS)

  virtual ui::Clipboard* clipboard();

  virtual ExtensionEventRouterForwarder* extension_event_router_forwarder();

  virtual NotificationUIManager* notification_ui_manager();

  virtual GoogleURLTracker* google_url_tracker();

  virtual IntranetRedirectDetector* intranet_redirect_detector();

  virtual AutomationProviderList* InitAutomationProviderList();

  virtual void InitDevToolsHttpProtocolHandler(
      const std::string& ip,
      int port,
      const std::string& frontend_url);

  virtual void InitDevToolsLegacyProtocolHandler(int port);

  virtual unsigned int AddRefModule();
  virtual unsigned int ReleaseModule();

  virtual bool IsShuttingDown();

  virtual printing::PrintJobManager* print_job_manager();

  virtual printing::PrintPreviewTabController* print_preview_tab_controller();

  virtual printing::BackgroundPrintingManager* background_printing_manager();

  virtual const std::string& GetApplicationLocale();

  virtual void SetApplicationLocale(const std::string& app_locale);

  virtual DownloadStatusUpdater* download_status_updater();

  virtual base::WaitableEvent* shutdown_event();

  virtual bool plugin_finder_disabled() const;

  virtual void CheckForInspectorFiles() {}

#if (defined(OS_WIN) || defined(OS_LINUX)) && !defined(OS_CHROMEOS)
  virtual void StartAutoupdateTimer() {}
#endif

  virtual ChromeNetLog* net_log();

  virtual prerender::PrerenderTracker* prerender_tracker();

#if defined(IPC_MESSAGE_LOG_ENABLED)
  virtual void SetIPCLoggingEnabled(bool enable) {}
#endif

  // Set the local state for tests. Consumer is responsible for cleaning it up
  // afterwards (using ScopedTestingLocalState, for example).
  void SetLocalState(PrefService* local_state);
  void SetGoogleURLTracker(GoogleURLTracker* google_url_tracker);
  void SetProfileManager(ProfileManager* profile_manager);

 private:
  NotificationService notification_service_;
  scoped_ptr<base::WaitableEvent> shutdown_event_;
  unsigned int module_ref_count_;
  scoped_ptr<ui::Clipboard> clipboard_;
  std::string app_locale_;

  // Weak pointer.
  PrefService* local_state_;
  scoped_ptr<policy::BrowserPolicyConnector> browser_policy_connector_;
  scoped_ptr<GoogleURLTracker> google_url_tracker_;
  scoped_ptr<ProfileManager> profile_manager_;
  scoped_ptr<NotificationUIManager> notification_ui_manager_;
  scoped_ptr<printing::BackgroundPrintingManager> background_printing_manager_;
  scoped_ptr<prerender::PrerenderTracker> prerender_tracker_;

  DISALLOW_COPY_AND_ASSIGN(TestingBrowserProcess);
};

// Scoper to put a TestingBrowserProcess in |g_browser_process|.
class ScopedTestingBrowserProcess {
 public:
  ScopedTestingBrowserProcess();
  ~ScopedTestingBrowserProcess();

  TestingBrowserProcess* get() {
    return browser_process_.get();
  }

 private:
  // TODO(phajdan.jr): Temporary, for http://crbug.com/61062.
  // After the transition is over, we should just stack-allocate it.
  scoped_ptr<TestingBrowserProcess> browser_process_;

  DISALLOW_COPY_AND_ASSIGN(ScopedTestingBrowserProcess);
};

#endif  // CHROME_TEST_TESTING_BROWSER_PROCESS_H_
