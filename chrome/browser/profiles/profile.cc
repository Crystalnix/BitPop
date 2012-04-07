// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/profiles/profile.h"

#include <string>

#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/memory/scoped_ptr.h"
#include "base/path_service.h"
#include "base/string_util.h"
#include "build/build_config.h"
#include "chrome/browser/background/background_contents_service_factory.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/content_settings/host_content_settings_map.h"
#include "chrome/browser/download/chrome_download_manager_delegate.h"
#include "chrome/browser/extensions/api/webrequest/webrequest_api.h"
#include "chrome/browser/extensions/extension_info_map.h"
#include "chrome/browser/extensions/extension_message_service.h"
#include "chrome/browser/extensions/extension_pref_store.h"
#include "chrome/browser/extensions/extension_process_manager.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_special_storage_policy.h"
#include "chrome/browser/prefs/incognito_mode_prefs.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/off_the_record_profile_io_data.h"
#include "chrome/browser/profiles/profile_dependency_manager.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "chrome/browser/sync/sync_prefs.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/find_bar/find_bar_state.h"
#include "chrome/browser/ui/webui/chrome_url_data_manager.h"
#include "chrome/browser/ui/webui/extensions/extension_icon_source.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/json_pref_store.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/render_messages.h"
#include "content/browser/appcache/chrome_appcache_service.h"
#include "content/browser/chrome_blob_storage_context.h"
#include "content/browser/file_system/browser_file_system_helper.h"
#include "content/browser/in_process_webkit/webkit_context.h"
#include "content/browser/ssl/ssl_host_state.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "ui/base/resource/resource_bundle.h"
#include "webkit/database/database_tracker.h"
#include "webkit/quota/quota_manager.h"

#if defined(TOOLKIT_USES_GTK)
#include "chrome/browser/ui/gtk/gtk_theme_service.h"
#endif

using base::Time;
using base::TimeDelta;

// A pointer to the request context for the default profile.  See comments on
// Profile::GetDefaultRequestContext.
net::URLRequestContextGetter* Profile::default_request_context_;

namespace {

}  // namespace

Profile::Profile()
    : restored_last_session_(false),
      accessibility_pause_level_(0) {
}

// static
Profile* Profile::FromBrowserContext(content::BrowserContext* browser_context) {
  // This is safe; this is the only implementation of the browser context.
  return static_cast<Profile*>(browser_context);
}

// static
Profile* Profile::FromWebUI(content::WebUI* web_ui) {
  // TODO(dhollowa): Crash diagnosis http://crbug.com/97802
  CHECK(web_ui);
  CHECK(web_ui->GetWebContents());
  CHECK(web_ui->GetWebContents()->GetBrowserContext());

  return FromBrowserContext(web_ui->GetWebContents()->GetBrowserContext());
}

TestingProfile* Profile::AsTestingProfile() {
  return NULL;
}

// static
const char* const Profile::kProfileKey = "__PROFILE__";

#if !defined(OS_MACOSX) && !defined(OS_CHROMEOS) && defined(OS_POSIX)
// static
const LocalProfileId Profile::kInvalidLocalProfileId =
    static_cast<LocalProfileId>(0);
#endif

// static
void Profile::RegisterUserPrefs(PrefService* prefs) {
  prefs->RegisterBooleanPref(prefs::kSearchSuggestEnabled,
                             true,
                             PrefService::SYNCABLE_PREF);
  prefs->RegisterBooleanPref(prefs::kSessionExitedCleanly,
                             true,
                             PrefService::UNSYNCABLE_PREF);
  prefs->RegisterBooleanPref(prefs::kSafeBrowsingEnabled,
                             true,
                             PrefService::SYNCABLE_PREF);
  prefs->RegisterBooleanPref(prefs::kSafeBrowsingReportingEnabled,
                             false,
                             PrefService::UNSYNCABLE_PREF);
  prefs->RegisterBooleanPref(prefs::kSpeechInputFilterProfanities,
                             true,
                             PrefService::UNSYNCABLE_PREF);
  prefs->RegisterBooleanPref(prefs::kSpeechInputTrayNotificationShown,
                             false,
                             PrefService::UNSYNCABLE_PREF);
#if defined(TOOLKIT_USES_GTK)
  prefs->RegisterBooleanPref(prefs::kUsesSystemTheme,
                             GtkThemeService::DefaultUsesSystemTheme(),
                             PrefService::UNSYNCABLE_PREF);
#endif
  prefs->RegisterFilePathPref(prefs::kCurrentThemePackFilename,
                              FilePath(),
                              PrefService::UNSYNCABLE_PREF);
  prefs->RegisterStringPref(prefs::kCurrentThemeID,
                            ThemeService::kDefaultThemeID,
                            PrefService::UNSYNCABLE_PREF);
  prefs->RegisterDictionaryPref(prefs::kCurrentThemeImages,
                                PrefService::UNSYNCABLE_PREF);
  prefs->RegisterDictionaryPref(prefs::kCurrentThemeColors,
                                PrefService::UNSYNCABLE_PREF);
  prefs->RegisterDictionaryPref(prefs::kCurrentThemeTints,
                                PrefService::UNSYNCABLE_PREF);
  prefs->RegisterDictionaryPref(prefs::kCurrentThemeDisplayProperties,
                                PrefService::UNSYNCABLE_PREF);
  prefs->RegisterBooleanPref(prefs::kDisableExtensions,
                             false,
                             PrefService::UNSYNCABLE_PREF);
  prefs->RegisterBooleanPref(prefs::kExtensionAlertsInitializedPref,
                             false, PrefService::UNSYNCABLE_PREF);
  prefs->RegisterStringPref(prefs::kSelectFileLastDirectory,
                            "",
                            PrefService::UNSYNCABLE_PREF);
  prefs->RegisterDoublePref(prefs::kDefaultZoomLevel,
                            0.0,
                            PrefService::UNSYNCABLE_PREF);
  prefs->RegisterDictionaryPref(prefs::kPerHostZoomLevels,
                                PrefService::UNSYNCABLE_PREF);
  prefs->RegisterStringPref(prefs::kDefaultApps,
                            "install",
                            PrefService::UNSYNCABLE_PREF);
#if defined(OS_CHROMEOS)
  // TODO(dilmah): For OS_CHROMEOS we maintain kApplicationLocale in both
  // local state and user's profile.  For other platforms we maintain
  // kApplicationLocale only in local state.
  // In the future we may want to maintain kApplicationLocale
  // in user's profile for other platforms as well.
  prefs->RegisterStringPref(prefs::kApplicationLocale,
                            "",
                            PrefService::SYNCABLE_PREF);
  prefs->RegisterStringPref(prefs::kApplicationLocaleBackup,
                            "",
                            PrefService::UNSYNCABLE_PREF);
  prefs->RegisterStringPref(prefs::kApplicationLocaleAccepted,
                            "",
                            PrefService::UNSYNCABLE_PREF);
#endif
}

// static
net::URLRequestContextGetter* Profile::GetDefaultRequestContext() {
  return default_request_context_;
}

std::string Profile::GetDebugName() {
  std::string name = GetPath().BaseName().MaybeAsASCII();
  if (name.empty()) {
    name = "UnknownProfile";
  }
  return name;
}

// static
bool Profile::IsGuestSession() {
#if defined(OS_CHROMEOS)
  static bool is_guest_session =
      CommandLine::ForCurrentProcess()->HasSwitch(switches::kGuestSession);
  return is_guest_session;
#else
  return false;
#endif
}

bool Profile::IsSyncAccessible() {
  browser_sync::SyncPrefs prefs(GetPrefs());
  return ProfileSyncService::IsSyncEnabled() && !prefs.IsManaged();
}
