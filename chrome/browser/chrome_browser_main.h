// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROME_BROWSER_MAIN_H_
#define CHROME_BROWSER_CHROME_BROWSER_MAIN_H_
#pragma once

#include "base/basictypes.h"
#include "base/gtest_prod_util.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/scoped_vector.h"
#include "base/metrics/field_trial.h"
#include "base/tracked_objects.h"
#include "chrome/browser/first_run/first_run.h"
#include "chrome/browser/process_singleton.h"
#include "chrome/browser/ui/browser_init.h"
#include "content/public/browser/browser_main_parts.h"
#include "content/public/browser/browser_thread.h"

class BrowserInit;
class BrowserProcessImpl;
class ChromeBrowserMainExtraParts;
class FieldTrialSynchronizer;
class HistogramSynchronizer;
class MetricsService;
class PrefService;
class Profile;
class StartupTimeBomb;
class ShutdownWatcherHelper;
class TranslateManager;

namespace chrome_browser {
// For use by ShowMissingLocaleMessageBox.
extern const char kMissingLocaleDataTitle[];
extern const char kMissingLocaleDataMessage[];
}

namespace chrome_browser_metrics {
class TrackingSynchronizer;
}

namespace content {
struct MainFunctionParams;
}

class ChromeBrowserMainParts : public content::BrowserMainParts {
 public:
  virtual ~ChromeBrowserMainParts();

  // Add additional ChromeBrowserMainExtraParts.
  virtual void AddParts(ChromeBrowserMainExtraParts* parts);

 protected:
  explicit ChromeBrowserMainParts(
      const content::MainFunctionParams& parameters);

  // content::BrowserMainParts overrides.
  // These are called in-order by content::BrowserMainLoop.
  // Each stage calls the same stages in any ChromeBrowserMainExtraParts added
  // with AddParts() from ChromeContentBrowserClient::CreateBrowserMainParts.
  virtual void PreEarlyInitialization() OVERRIDE;
  virtual void PostEarlyInitialization() OVERRIDE;
  virtual void ToolkitInitialized() OVERRIDE;
  virtual void PreMainMessageLoopStart() OVERRIDE;
  virtual void PostMainMessageLoopStart() OVERRIDE;
  virtual int PreCreateThreads() OVERRIDE;
  virtual void PreMainMessageLoopRun() OVERRIDE;
  virtual bool MainMessageLoopRun(int* result_code) OVERRIDE;
  virtual void PostMainMessageLoopRun() OVERRIDE;
  virtual void PostDestroyThreads() OVERRIDE;

  // Additional stages for ChromeBrowserMainExtraParts. These stages are called
  // in order from PreMainMessageLoopStart(). See implementation for details.
  virtual void PreProfileInit();
  virtual void PostProfileInit();
  virtual void PreBrowserStart();
  virtual void PostBrowserStart();

  // Displays a warning message that we can't find any locale data files.
  virtual void ShowMissingLocaleMessageBox() = 0;

  const content::MainFunctionParams& parameters() const {
    return parameters_;
  }
  const CommandLine& parsed_command_line() const {
    return parsed_command_line_;
  }

  Profile* profile() { return profile_; }

 private:
  // Methods for |EarlyInitialization()| ---------------------------------------

  // A/B test for the maximum number of persistent connections per host.
  void ConnectionFieldTrial();

  // A/B test for determining a value for unused socket timeout.
  void SocketTimeoutFieldTrial();

  // A/B test for the maximum number of connections per proxy server.
  void ProxyConnectionsFieldTrial();

  // A/B test for spdy when --use-spdy not set.
  void SpdyFieldTrial();

  // A/B test for warmest socket vs. most recently used socket.
  void WarmConnectionFieldTrial();

  // A/B test for automatically establishing a backup TCP connection when a
  // specified timeout value is reached.
  void ConnectBackupJobsFieldTrial();

  // Field trial to see what disabling DNS pre-resolution does to
  // latency of page loads.
  void PredictorFieldTrial();

  // Field trial to see what effect installing defaults in the NTP apps pane
  // has on retention and general apps/webstore usage.
  void DefaultAppsFieldTrial();

  // A field trial to see what effects launching Chrome automatically on
  // computer startup has on retention and usage of Chrome.
  void AutoLaunchChromeFieldTrial();

  // Methods for |SetupMetricsAndFieldTrials()| --------------------------------

  // Constructs metrics service and does related initialization, including
  // creation of field trials. Call only after labs have been converted to
  // switches.
  void SetupMetricsAndFieldTrials();

  // Add an invocation of your field trial init function to this method.
  void SetupFieldTrials(bool metrics_recording_enabled,
                        bool proxy_policy_is_set);

  // Starts recording of metrics. This can only be called after we have a file
  // thread.
  void StartMetricsRecording();

  // Returns true if the user opted in to sending metric reports.
  bool IsMetricsReportingEnabled();

  // Methods for Main Message Loop -------------------------------------------

  int PreCreateThreadsImpl();
  int PreMainMessageLoopRunImpl();

  // Members initialized on construction ---------------------------------------

  const content::MainFunctionParams& parameters_;
  const CommandLine& parsed_command_line_;
  int result_code_;

  // Create StartupTimeBomb object for watching jank during startup.
  scoped_ptr<StartupTimeBomb> startup_watcher_;

  // Create ShutdownWatcherHelper object for watching jank during shutdown.
  // Please keep |shutdown_watcher| as the first object constructed, and hence
  // it is destroyed last.
  scoped_ptr<ShutdownWatcherHelper> shutdown_watcher_;

  // Creating this object starts tracking the creation and deletion of Task
  // instance. This MUST be done before main_message_loop, so that it is
  // destroyed after the main_message_loop.
  tracked_objects::AutoTracking tracking_objects_;

  // Statistical testing infrastructure for the entire browser. NULL until
  // SetupMetricsAndFieldTrials is called.
  scoped_ptr<base::FieldTrialList> field_trial_list_;

  // Vector of additional ChromeBrowserMainExtraParts.
  // Parts are deleted in the inverse order they are added.
  std::vector<ChromeBrowserMainExtraParts*> chrome_extra_parts_;

  // Members initialized after / released before main_message_loop_ ------------

  scoped_ptr<BrowserInit> browser_init_;
  scoped_ptr<BrowserProcessImpl> browser_process_;
  scoped_refptr<HistogramSynchronizer> histogram_synchronizer_;
  scoped_refptr<chrome_browser_metrics::TrackingSynchronizer>
      tracking_synchronizer_;
  scoped_ptr<ProcessSingleton> process_singleton_;
  scoped_ptr<first_run::MasterPrefs> master_prefs_;
  bool record_search_engine_;
  TranslateManager* translate_manager_;
  Profile* profile_;
  bool run_message_loop_;
  ProcessSingleton::NotifyResult notify_result_;

  // Initialized in SetupMetricsAndFieldTrials.
  scoped_refptr<FieldTrialSynchronizer> field_trial_synchronizer_;

  // Members initialized in PreMainMessageLoopRun, needed in
  // PreMainMessageLoopRunThreadsCreated.
  bool is_first_run_;
  bool first_run_ui_bypass_;
  PrefService* local_state_;
  FilePath user_data_dir_;

  // Members needed across shutdown methods.
  bool restart_last_session_;

  FRIEND_TEST_ALL_PREFIXES(BrowserMainTest,
                           WarmConnectionFieldTrial_WarmestSocket);
  FRIEND_TEST_ALL_PREFIXES(BrowserMainTest, WarmConnectionFieldTrial_Random);
  FRIEND_TEST_ALL_PREFIXES(BrowserMainTest, WarmConnectionFieldTrial_Invalid);
  DISALLOW_COPY_AND_ASSIGN(ChromeBrowserMainParts);
};

// Records the conditions that can prevent Breakpad from generating and
// sending crash reports.  The presence of a Breakpad handler (after
// attempting to initialize crash reporting) and the presence of a debugger
// are registered with the UMA metrics service.
void RecordBreakpadStatusUMA(MetricsService* metrics);

// Displays a warning message if some minimum level of OS support is not
// present on the current platform.
void WarnAboutMinimumSystemRequirements();

// Records the time from our process' startup to the present time in
// the UMA histogram |metric_name|.
void RecordBrowserStartupTime();

// Records a time value to an UMA histogram in the context of the
// PreReadExperiment field-trial. This also reports to the appropriate
// sub-histogram (_PreRead(Enabled|Disabled)).
void RecordPreReadExperimentTime(const char* name, base::TimeDelta time);

#endif  // CHROME_BROWSER_CHROME_BROWSER_MAIN_H_
