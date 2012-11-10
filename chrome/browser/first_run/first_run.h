// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_FIRST_RUN_FIRST_RUN_H_
#define CHROME_BROWSER_FIRST_RUN_FIRST_RUN_H_

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/gtest_prod_util.h"
#include "base/memory/ref_counted.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "ui/gfx/native_widget_types.h"

class CommandLine;
class FilePath;
class GURL;
class PrefService;
class Profile;
class ProcessSingleton;

// This namespace contains the chrome first-run installation actions needed to
// fully test the custom installer. It also contains the opposite actions to
// execute during uninstall. When the first run UI is ready we won't
// do the actions unconditionally. Currently the only action is to create a
// desktop shortcut.
//
// The way we detect first-run is by looking at a 'sentinel' file.
// If it does not exist we understand that we need to do the first time
// install work for this user. After that the sentinel file is created.
namespace first_run {

enum FirstRunBubbleMetric {
  FIRST_RUN_BUBBLE_SHOWN = 0,       // The search engine bubble was shown.
  FIRST_RUN_BUBBLE_CHANGE_INVOKED,  // The bubble's "Change" was invoked.
  NUM_FIRST_RUN_BUBBLE_METRICS
};

// See ProcessMasterPreferences for more info about this structure.
struct MasterPrefs {
  MasterPrefs();
  ~MasterPrefs();

  int ping_delay;
  bool homepage_defined;
  int do_import_items;
  int dont_import_items;
  bool make_chrome_default;
  bool suppress_first_run_default_browser_prompt;
  std::vector<GURL> new_tabs;
  std::vector<GURL> bookmarks;
};

// Returns true if this is the first time chrome is run for this user.
bool IsChromeFirstRun();

// Creates the sentinel file that signals that chrome has been configured.
bool CreateSentinel();

// Get RLZ ping delay pref name.
std::string GetPingDelayPrefName();

// Register user preferences used by the MasterPrefs structure.
void RegisterUserPrefs(PrefService* prefs);

// Removes the sentinel file created in ConfigDone(). Returns false if the
// sentinel file could not be removed.
bool RemoveSentinel();

// Sets the kShouldShowFirstRunBubble local state pref so that the browser
// shows the bubble once the main message loop gets going (or refrains from
// showing the bubble, if |show_bubble| is false). Returns false if the pref
// could not be set. This function can be called multiple times, but only the
// initial call will actually set the preference.
bool SetShowFirstRunBubblePref(bool show_bubble);

// Sets the kShouldShowWelcomePage local state pref so that the browser
// loads the welcome tab once the message loop gets going. Returns false
// if the pref could not be set.
bool SetShowWelcomePagePref();

// Sets the kAutofillPersonalDataManagerFirstRun local state pref so that the
// browser loads PersonalDataManager once the main message loop gets going.
// Returns false if the pref could not be set.
bool SetPersonalDataManagerFirstRunPref();

// Log a metric for the "FirstRun.SearchEngineBubble" histogram.
void LogFirstRunMetric(FirstRunBubbleMetric metric);

// -- Platform-specific functions --

// Automatically import history and home page (and search engine, if
// ShouldShowSearchEngineDialog is true).
void AutoImport(Profile* profile,
                bool homepage_defined,
                int import_items,
                int dont_import_items,
                bool make_chrome_default,
                ProcessSingleton* process_singleton);

// Imports bookmarks and/or browser items (depending on platform support)
// in this process. This function is paired with first_run::ImportSettings().
// This function might or might not show a visible UI depending on the
// cmdline parameters.
int ImportNow(Profile* profile, const CommandLine& cmdline);

// Returns the path for the master preferences file.
FilePath MasterPrefsPath();

// The master preferences is a JSON file with the same entries as the
// 'Default\Preferences' file. This function locates this file from a standard
// location and processes it so it becomes the default preferences in the
// profile pointed to by |user_data_dir|. After processing the file, the
// function returns true if and only if showing the first run dialog is
// needed. The detailed settings in the preference file are reported via
// |preference_details|.
//
// This function destroys any existing prefs file and it is meant to be
// invoked only on first run.
//
// See chrome/installer/util/master_preferences.h for a description of
// 'master_preferences' file.
bool ProcessMasterPreferences(const FilePath& user_data_dir,
                              MasterPrefs* out_prefs);

// Show the first run search engine bubble at the first appropriate opportunity.
// This bubble may be delayed by other UI, like global errors and sync promos.
class FirstRunBubbleLauncher : public content::NotificationObserver {
 public:
  // Show the bubble at the first appropriate opportunity. This function
  // instantiates a FirstRunBubbleLauncher, which manages its own lifetime.
  static void ShowFirstRunBubbleSoon();

 private:
  FirstRunBubbleLauncher();
  virtual ~FirstRunBubbleLauncher();

  // content::NotificationObserver override:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  content::NotificationRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(FirstRunBubbleLauncher);
};

}  // namespace first_run

#endif  // CHROME_BROWSER_FIRST_RUN_FIRST_RUN_H_
