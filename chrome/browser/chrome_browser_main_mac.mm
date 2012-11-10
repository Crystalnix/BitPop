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
#include "chrome/browser/mac/install_from_dmg.h"
#include "chrome/browser/mac/keychain_reauthorize.h"
#import "chrome/browser/mac/keystone_glue.h"
#include "chrome/browser/metrics/metrics_service.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "content/public/common/main_function_params.h"
#include "content/public/common/result_codes.h"
#include "ui/base/l10n/l10n_util_mac.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/resource/resource_handle.h"

namespace {

// This preference is used to track whether the KeychainReauthorize operation
// has occurred at launch. This operation only makes sense while the
// application continues to be signed by the old certificate.
NSString* const kKeychainReauthorizeAtLaunchPref =
    @"KeychainReauthorizeInAppMay2012";
const int kKeychainReauthorizeAtLaunchMaxTries = 2;

// Some users rarely restart Chrome, so they might never get a chance to run
// the at-launch KeychainReauthorize. To account for them, there's also an
// at-update KeychainReauthorize option, which runs from .keystone_install for
// users on a user Keystone ticket. This operation may make sense for a period
// of time after the application switches to being signed by the new
// certificate, as long as the at-update stub executable is still signed by
// the old one.
NSString* const kKeychainReauthorizeAtUpdatePref =
    @"KeychainReauthorizeAtUpdateMay2012";
const int kKeychainReauthorizeAtUpdateMaxTries = 3;

}  // namespace

void RecordBreakpadStatusUMA(MetricsService* metrics) {
  metrics->RecordBreakpadRegistration(IsCrashReporterEnabled());
  metrics->RecordBreakpadHasDebugger(base::debug::BeingDebugged());
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
  if (parsed_command_line().HasSwitch(switches::kKeychainReauthorize)) {
    if (base::mac::AmIBundled()) {
      LOG(FATAL) << "Inappropriate process type for Keychain reauthorization";
    }

    // Do Keychain reauthorization at the time of update installation. This
    // gets three chances to run. If the first or second try doesn't complete
    // successfully (crashes or is interrupted for any reason), there will be
    // another chance. Once this step completes successfully, it should never
    // have to run again.
    //
    // This is kicked off by a special stub executable during an automatic
    // update. See chrome/installer/mac/keychain_reauthorize_main.cc.
    chrome::browser::mac::KeychainReauthorizeIfNeeded(
        kKeychainReauthorizeAtUpdatePref, kKeychainReauthorizeAtUpdateMaxTries);

    exit(0);
  }

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
        ResourceBundle::InitSharedInstanceWithLocale(std::string(), NULL);
    CHECK(!loaded_locale.empty()) << "Default locale could not be found";

    FilePath resources_pack_path;
    PathService::Get(chrome::FILE_RESOURCES_PACK, &resources_pack_path);
    ResourceBundle::GetSharedInstance().AddDataPackFromPath(
        resources_pack_path, ui::SCALE_FACTOR_100P);
  }

  // This is a no-op if the KeystoneRegistration framework is not present.
  // The framework is only distributed with branded Google Chrome builds.
  [[KeystoneGlue defaultKeystoneGlue] registerWithKeystone];

  // Disk image installation is sort of a first-run task, so it shares the
  // kNoFirstRun switch.
  //
  // This needs to be done after the resource bundle is initialized (for
  // access to localizations in the UI) and after Keystone is initialized
  // (because the installation may need to promote Keystone) but before the
  // app controller is set up (and thus before MainMenu.nib is loaded, because
  // the app controller assumes that a browser has been set up and will crash
  // upon receipt of certain notifications if no browser exists), before
  // anyone tries doing anything silly like firing off an import job, and
  // before anything creating preferences like Local State in order for the
  // relaunched installed application to still consider itself as first-run.
  if (!parsed_command_line().HasSwitch(switches::kNoFirstRun)) {
    if (MaybeInstallFromDiskImage()) {
      // The application was installed and the installed copy has been
      // launched.  This process is now obsolete.  Exit.
      exit(0);
    }
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

  // Prevent Cocoa from turning command-line arguments into
  // |-application:openFiles:|, since we already handle them directly.
  [[NSUserDefaults standardUserDefaults]
      setObject:@"NO" forKey:@"NSTreatUnknownArgumentsAsOpen"];
}

void ChromeBrowserMainPartsMac::DidEndMainMessageLoop() {
  AppController* appController = [NSApp delegate];
  [appController didEndMainMessageLoop];
}
