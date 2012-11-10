// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/default_apps.h"

#include "base/command_line.h"
#include "base/metrics/field_trial.h"
#include "chrome/browser/browser_process.h"
#if !defined(OS_ANDROID)
#include "chrome/browser/first_run/first_run.h"
#endif
#include "chrome/browser/extensions/default_apps_trial.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/chrome_version_info.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/pref_names.h"
#include "ui/base/l10n/l10n_util.h"

static bool ShouldInstallInProfile(Profile* profile) {
  // We decide to install or not install default apps based on the following
  // criteria, from highest priority to lowest priority:
  //
  // - If this instance of chrome is participating in the default apps
  //   field trial, then install apps based on the group.
  // - The command line option.  Tests use this option to disable installation
  //   of default apps in some cases.
  // - If the locale is not compatible with the defaults, don't install them.
  // - If the profile says to either always install or never install default
  //   apps, obey.
  // - The kDefaultApps preferences value in the profile.  This value is
  //   usually set in the master_preferences file.
  bool install_apps =
      profile->GetPrefs()->GetString(prefs::kDefaultApps) == "install";

  default_apps::InstallState state =
      static_cast<default_apps::InstallState>(profile->GetPrefs()->GetInteger(
          prefs::kDefaultAppsInstallState));
  switch (state) {
    case default_apps::kUnknown: {
      // Only new installations and profiles get default apps. In theory the
      // new profile checks should catch new installations, but that is not
      // always the case (http:/crbug.com/145351).
      chrome::VersionInfo version_info;
      bool is_new_profile =
          profile->WasCreatedByVersionOrLater(version_info.Version().c_str());
      // Android excludes most of the first run code, so it can't determine
      // if this is a first run. That's OK though, because Android doesn't
      // use default apps in general.
#if defined(OS_ANDROID)
      bool is_first_run = false;
#else
      bool is_first_run = first_run::IsChromeFirstRun();
#endif
      if (!is_first_run && !is_new_profile)
        install_apps = false;
      break;
    }
    case default_apps::kAlwaysProvideDefaultApps:
      install_apps = true;
      break;
    case default_apps::kNeverProvideDefaultApps:
      install_apps = false;
      break;
    default:
      NOTREACHED();
  }

  if (install_apps) {
    // Don't bother installing default apps in locales where it is known that
    // they don't work.
    // TODO(rogerta): Do this check dynamically once the webstore can expose
    // an API. See http://crbug.com/101357
    const std::string& locale = g_browser_process->GetApplicationLocale();
    static const char* unsupported_locales[] = {"CN", "TR", "IR"};
    for (size_t i = 0; i < arraysize(unsupported_locales); ++i) {
      if (EndsWith(locale, unsupported_locales[i], false)) {
        install_apps = false;
        break;
      }
    }
  }

  if (CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kDisableDefaultApps)) {
    install_apps = false;
  }

  if (base::FieldTrialList::TrialExists(kDefaultAppsTrialName)) {
    install_apps = base::FieldTrialList::Find(
        kDefaultAppsTrialName)->group_name() != kDefaultAppsTrialNoAppsGroup;
  }

  // Save the state if needed.  Once it is decided whether we are installing
  // default apps or not, we want to always respond with same value.  Therefore
  // on first run of this feature (i.e. the current state is kUnknown) the
  // state is updated to remember the choice that was made at this time.  The
  // next time chrome runs it will use the same decision.
  //
  // The reason for responding with the same value is that once an external
  // extenson provider has provided apps for a given profile, it must continue
  // to provide those extensions on each subsequent run.  Otherwise the
  // extension manager will automatically uninstall the apps.  The extension
  // manager is smart enough to know not to reinstall the apps on all
  // subsequent runs of chrome.
  if (state == default_apps::kUnknown) {
    if (install_apps) {
      profile->GetPrefs()->SetInteger(prefs::kDefaultAppsInstallState,
                                      default_apps::kAlwaysProvideDefaultApps);
    } else {
      profile->GetPrefs()->SetInteger(prefs::kDefaultAppsInstallState,
                                      default_apps::kNeverProvideDefaultApps);
    }
  }

  return install_apps;
}

namespace default_apps {

void RegisterUserPrefs(PrefService* prefs) {
  prefs->RegisterIntegerPref(prefs::kDefaultAppsInstallState, kUnknown,
                             PrefService::UNSYNCABLE_PREF);
}

Provider::Provider(Profile* profile,
                   VisitorInterface* service,
                   extensions::ExternalLoader* loader,
                   extensions::Extension::Location crx_location,
                   extensions::Extension::Location download_location,
                   int creation_flags)
    : extensions::ExternalProviderImpl(service, loader, crx_location,
                                       download_location, creation_flags),
      profile_(profile) {
  DCHECK(profile);
  set_auto_acknowledge(true);
}

void Provider::VisitRegisteredExtension() {
  if (!profile_ || !ShouldInstallInProfile(profile_)) {
    base::DictionaryValue* prefs = new base::DictionaryValue;
    SetPrefs(prefs);
    return;
  }

  extensions::ExternalProviderImpl::VisitRegisteredExtension();
}

}  // namespace default_apps
