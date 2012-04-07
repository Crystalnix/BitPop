// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chrome_browser_main_mac.h"

#import <Cocoa/Cocoa.h>

#include "base/command_line.h"
#include "base/debug/debugger.h"
#include "base/file_path.h"
#include "base/mac/bundle_locations.h"
#include "base/mac/mac_util.h"
#include "base/memory/scoped_nsobject.h"
#include "base/path_service.h"
#include "chrome/app/breakpad_mac.h"
#import "chrome/browser/app_controller_mac.h"
#import "chrome/browser/chrome_browser_application_mac.h"
#import "chrome/browser/mac/keystone_glue.h"
#include "chrome/browser/metrics/metrics_service.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "content/public/common/main_function_params.h"
#include "content/public/common/result_codes.h"
#include "ui/base/l10n/l10n_util_mac.h"
#include "ui/base/resource/resource_bundle.h"

void RecordBreakpadStatusUMA(MetricsService* metrics) {
  metrics->RecordBreakpadRegistration(IsCrashReporterEnabled());
  metrics->RecordBreakpadHasDebugger(base::debug::BeingDebugged());
}

void RecordBrowserStartupTime() {
  // Not implemented on Mac for now.
}

void WarnAboutMinimumSystemRequirements() {
  // Nothing to check for on Mac right now.
}

// From browser_main_win.h, stubs until we figure out the right thing...

int DoUninstallTasks(bool chrome_still_running) {
  return content::RESULT_CODE_NORMAL_EXIT;
}

// ChromeBrowserMainPartsMac ---------------------------------------------------

ChromeBrowserMainPartsMac::ChromeBrowserMainPartsMac(
    const content::MainFunctionParams& parameters)
    : ChromeBrowserMainPartsPosix(parameters) {
}

void ChromeBrowserMainPartsMac::PreEarlyInitialization() {
  ChromeBrowserMainPartsPosix::PreEarlyInitialization();

  if (base::mac::WasLaunchedAsHiddenLoginItem()) {
    CommandLine* singleton_command_line = CommandLine::ForCurrentProcess();
    singleton_command_line->AppendSwitch(switches::kNoStartupWindow);
  }
}

void ChromeBrowserMainPartsMac::PreMainMessageLoopStart() {
  ChromeBrowserMainPartsPosix::PreMainMessageLoopStart();

  // Tell Cooca to finish its initialization, which we want to do manually
  // instead of calling NSApplicationMain(). The primary reason is that NSAM()
  // never returns, which would leave all the objects currently on the stack
  // in scoped_ptrs hanging and never cleaned up. We then load the main nib
  // directly. The main event loop is run from common code using the
  // MessageLoop API, which works out ok for us because it's a wrapper around
  // CFRunLoop.

  // Initialize NSApplication using the custom subclass.
  chrome_browser_application_mac::RegisterBrowserCrApp();

  // If ui_task is not NULL, the app is actually a browser_test, so startup is
  // handled outside of BrowserMain (which is what called this).
  if (!parameters().ui_task) {
    // The browser process only wants to support the language Cocoa will use,
    // so force the app locale to be overriden with that value.
    l10n_util::OverrideLocaleWithCocoaLocale();

    // Before we load the nib, we need to start up the resource bundle so we
    // have the strings avaiable for localization.
    // TODO(markusheintz): Read preference pref::kApplicationLocale in order
    // to enforce the application locale.
    const std::string loaded_locale =
        ResourceBundle::InitSharedInstanceWithLocale(std::string());
    CHECK(!loaded_locale.empty()) << "Default locale could not be found";

    FilePath resources_pack_path;
    PathService::Get(chrome::FILE_RESOURCES_PACK, &resources_pack_path);
    ResourceBundle::AddDataPackToSharedInstance(resources_pack_path);
  }

  // Now load the nib (from the right bundle).
  scoped_nsobject<NSNib>
      nib([[NSNib alloc] initWithNibNamed:@"MainMenu"
                                   bundle:base::mac::FrameworkBundle()]);
  // TODO(viettrungluu): crbug.com/20504 - This currently leaks, so if you
  // change this, you'll probably need to change the Valgrind suppression.
  [nib instantiateNibWithOwner:NSApp topLevelObjects:nil];
  // Make sure the app controller has been created.
  DCHECK([NSApp delegate]);

  // This is a no-op if the KeystoneRegistration framework is not present.
  // The framework is only distributed with branded Google Chrome builds.
  [[KeystoneGlue defaultKeystoneGlue] registerWithKeystone];

  // Prevent Cocoa from turning command-line arguments into
  // |-application:openFiles:|, since we already handle them directly.
  [[NSUserDefaults standardUserDefaults]
      setObject:@"NO" forKey:@"NSTreatUnknownArgumentsAsOpen"];
}

void ChromeBrowserMainPartsMac::ShowMissingLocaleMessageBox() {
  // Not called on Mac because we load the locale files differently.
  NOTREACHED();
}

void ChromeBrowserMainPartsMac::DidEndMainMessageLoop() {
  AppController* appController = [NSApp delegate];
  [appController didEndMainMessageLoop];
}
