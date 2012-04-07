// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/sync_promo/sync_promo_ui.h"

#include "base/command_line.h"
#include "base/string_number_conversions.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/first_run/first_run.h"
#include "chrome/browser/google/google_util.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_info_cache.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "chrome/browser/ui/webui/chrome_url_data_manager.h"
#include "chrome/browser/ui/webui/chrome_web_ui_data_source.h"
#include "chrome/browser/ui/webui/options/core_options_handler.h"
#include "chrome/browser/ui/webui/sync_promo/sync_promo_handler.h"
#include "chrome/browser/ui/webui/sync_promo/sync_promo_trial.h"
#include "chrome/browser/ui/webui/theme_source.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "googleurl/src/url_util.h"
#include "grit/browser_resources.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "ui/base/l10n/l10n_util.h"

using content::WebContents;

namespace {

const char kStringsJsFile[] = "strings.js";
const char kSyncPromoJsFile[] = "sync_promo.js";

const char kSyncPromoQueryKeyIsLaunchPage[] = "is_launch_page";
const char kSyncPromoQueryKeyNextPage[] = "next_page";
const char kSyncPromoQueryKeySource[] = "source";

// The maximum number of times we want to show the sync promo at startup.
const int kSyncPromoShowAtStartupMaximum = 10;

// Checks we want to show the sync promo for the given brand.
bool AllowPromoAtStartupForCurrentBrand() {
  std::string brand;
  google_util::GetBrand(&brand);

  if (brand.empty())
    return true;

  if (google_util::IsInternetCafeBrandCode(brand))
    return false;

  if (google_util::IsOrganic(brand))
    return true;

  if (StartsWithASCII(brand, "CH", true))
    return true;

  // Default to disallow for all other brand codes.
  return false;
}

// The Web UI data source for the sync promo page.
class SyncPromoUIHTMLSource : public ChromeWebUIDataSource {
 public:
  explicit SyncPromoUIHTMLSource(content::WebUI* web_ui);

 private:
  ~SyncPromoUIHTMLSource() {}
  DISALLOW_COPY_AND_ASSIGN(SyncPromoUIHTMLSource);
};

SyncPromoUIHTMLSource::SyncPromoUIHTMLSource(content::WebUI* web_ui)
    : ChromeWebUIDataSource(chrome::kChromeUISyncPromoHost) {
  DictionaryValue localized_strings;
  CoreOptionsHandler::GetStaticLocalizedValues(&localized_strings);
  SyncSetupHandler::GetStaticLocalizedValues(&localized_strings, web_ui);
  AddLocalizedStrings(localized_strings);
}

// Looks for |search_key| in the query portion of |url|. Returns true if the
// key is found and sets |out_value| to the value for the key. Returns false if
// the key is not found.
bool GetValueForKeyInQuery(const GURL& url, const std::string& search_key,
                           std::string* out_value) {
  url_parse::Component query = url.parsed_for_possibly_invalid_spec().query;
  url_parse::Component key, value;
  while (url_parse::ExtractQueryKeyValue(
      url.spec().c_str(), &query, &key, &value)) {
    if (key.is_nonempty()) {
      std::string key_string = url.spec().substr(key.begin, key.len);
      if (key_string == search_key) {
        if (value.is_nonempty())
          *out_value = url.spec().substr(value.begin, value.len);
        else
          *out_value = "";
        return true;
      }
    }
  }
  return false;
}

}  // namespace

SyncPromoUI::SyncPromoUI(content::WebUI* web_ui) : WebUIController(web_ui) {
  web_ui->HideURL();

  SyncPromoHandler* handler = new SyncPromoHandler(
      GetSourceForSyncPromoURL(web_ui->GetWebContents()->GetURL()),
      g_browser_process->profile_manager());
  web_ui->AddMessageHandler(handler);

  // Set up the chrome://theme/ source.
  Profile* profile = Profile::FromWebUI(web_ui);
  ThemeSource* theme = new ThemeSource(profile);
  profile->GetChromeURLDataManager()->AddDataSource(theme);

  // Set up the sync promo source.
  SyncPromoUIHTMLSource* html_source = new SyncPromoUIHTMLSource(web_ui);
  html_source->set_json_path(kStringsJsFile);
  html_source->add_resource_path(kSyncPromoJsFile, IDR_SYNC_PROMO_JS);
  html_source->set_default_resource(IDR_SYNC_PROMO_HTML);
  profile->GetChromeURLDataManager()->AddDataSource(html_source);

  sync_promo_trial::RecordUserShownPromo(web_ui);
}

// static
bool SyncPromoUI::HasShownPromoAtStartup(Profile* profile) {
  return profile->GetPrefs()->HasPrefPath(prefs::kSyncPromoStartupCount);
}

// static
bool SyncPromoUI::ShouldShowSyncPromo(Profile* profile) {
#if defined(OS_CHROMEOS)
  // There's no need to show the sync promo on cros since cros users are logged
  // into sync already.
  return false;
#endif

  // Honor the sync policies.
  if (!profile->GetOriginalProfile()->IsSyncAccessible())
    return false;

  // If the user is already signed into sync then don't show the promo.
  ProfileSyncService* service =
      profile->GetOriginalProfile()->GetProfileSyncService();
  if (!service || service->HasSyncSetupCompleted())
    return false;

  // Default to allow the promo.
  return true;
}

// static
void SyncPromoUI::RegisterUserPrefs(PrefService* prefs) {
  prefs->RegisterIntegerPref(
      prefs::kSyncPromoStartupCount, 0, PrefService::UNSYNCABLE_PREF);
  prefs->RegisterBooleanPref(
      prefs::kSyncPromoUserSkipped, false, PrefService::UNSYNCABLE_PREF);
  prefs->RegisterBooleanPref(prefs::kSyncPromoShowOnFirstRunAllowed, true,
      PrefService::UNSYNCABLE_PREF);

  SyncPromoHandler::RegisterUserPrefs(prefs);
}

// static
bool SyncPromoUI::ShouldShowSyncPromoAtStartup(Profile* profile,
                                               bool is_new_profile,
                                               bool* promo_suppressed) {
  DCHECK(profile);
  DCHECK(promo_suppressed);
  *promo_suppressed = false;

  if (!ShouldShowSyncPromo(profile))
    return false;

  const CommandLine& command_line = *CommandLine::ForCurrentProcess();
  if (command_line.HasSwitch(switches::kNoFirstRun))
    is_new_profile = false;

  if (!is_new_profile) {
    if (!HasShownPromoAtStartup(profile))
      return false;
  }

  if (HasUserSkippedSyncPromo(profile))
    return false;

  // For Chinese users skip the sync promo.
  if (g_browser_process->GetApplicationLocale() == "zh-CN")
    return false;

  PrefService* prefs = profile->GetPrefs();
  int show_count = prefs->GetInteger(prefs::kSyncPromoStartupCount);
  if (show_count >= kSyncPromoShowAtStartupMaximum)
    return false;

  // If the current install is part of trial then let the trial determine if we
  // should show the promo or not.
  switch (sync_promo_trial::GetStartupOverrideForCurrentTrial()) {
    case sync_promo_trial::STARTUP_OVERRIDE_NONE:
      // No override so simply continue.
      break;
    case sync_promo_trial::STARTUP_OVERRIDE_SHOW:
      return true;
    case sync_promo_trial::STARTUP_OVERRIDE_HIDE:
      *promo_suppressed = true;
      return false;
  }

  // This pref can be set in the master preferences file to allow or disallow
  // showing the sync promo at startup.
  if (prefs->HasPrefPath(prefs::kSyncPromoShowOnFirstRunAllowed))
    return prefs->GetBoolean(prefs::kSyncPromoShowOnFirstRunAllowed);

  // For now don't show the promo for some brands.
  if (!AllowPromoAtStartupForCurrentBrand())
    return false;

  // Default to show the promo.
  return true;
}

void SyncPromoUI::DidShowSyncPromoAtStartup(Profile* profile) {
  int show_count = profile->GetPrefs()->GetInteger(
      prefs::kSyncPromoStartupCount);
  show_count++;
  profile->GetPrefs()->SetInteger(prefs::kSyncPromoStartupCount, show_count);
}

bool SyncPromoUI::HasUserSkippedSyncPromo(Profile* profile) {
  return profile->GetPrefs()->GetBoolean(prefs::kSyncPromoUserSkipped);
}

void SyncPromoUI::SetUserSkippedSyncPromo(Profile* profile) {
  profile->GetPrefs()->SetBoolean(prefs::kSyncPromoUserSkipped, true);
}

// static
GURL SyncPromoUI::GetSyncPromoURL(const GURL& next_page,
                                  bool show_title,
                                  const std::string& source) {
  std::stringstream stream;
  stream << chrome::kChromeUISyncPromoURL << "?"
         << kSyncPromoQueryKeyIsLaunchPage << "="
         << (show_title ? "true" : "false") << "&"
         << kSyncPromoQueryKeySource << "=" << source;

  if (!next_page.spec().empty()) {
    url_canon::RawCanonOutputT<char> output;
    url_util::EncodeURIComponent(
        next_page.spec().c_str(), next_page.spec().length(), &output);
    std::string escaped_spec(output.data(), output.length());
    stream << "&" << kSyncPromoQueryKeyNextPage << "=" << escaped_spec;
  }

  return GURL(stream.str());
}

// static
bool SyncPromoUI::GetIsLaunchPageForSyncPromoURL(const GURL& url) {
  std::string value;
  // Show the title if the promo is currently the Chrome launch page (and not
  // the page accessed through the NTP).
  if (GetValueForKeyInQuery(url, kSyncPromoQueryKeyIsLaunchPage, &value))
    return value == "true";
  return false;
}

// static
GURL SyncPromoUI::GetNextPageURLForSyncPromoURL(const GURL& url) {
  std::string value;
  if (GetValueForKeyInQuery(url, kSyncPromoQueryKeyNextPage, &value)) {
    url_canon::RawCanonOutputT<char16> output;
    url_util::DecodeURLEscapeSequences(value.c_str(), value.length(), &output);
    std::string url;
    UTF16ToUTF8(output.data(), output.length(), &url);
    return GURL(url);
  }
  return GURL();
}

// static
std::string SyncPromoUI::GetSourceForSyncPromoURL(const GURL& url) {
  std::string value;
  return GetValueForKeyInQuery(url, kSyncPromoQueryKeySource, &value) ?
      value : std::string();
}

// static
bool SyncPromoUI::UserHasSeenSyncPromoAtStartup(Profile* profile) {
  return profile->GetPrefs()->GetInteger(prefs::kSyncPromoStartupCount) > 0;
}

// static
SyncPromoUI::Version SyncPromoUI::GetSyncPromoVersion() {
  int value = 0;
  if (base::StringToInt(CommandLine::ForCurrentProcess()->
      GetSwitchValueASCII(switches::kSyncPromoVersion), &value)) {
    if (value >= VERSION_DEFAULT && value < VERSION_COUNT)
      return static_cast<Version>(value);
  }

  Version version;
  if (sync_promo_trial::GetSyncPromoVersionForCurrentTrial(&version)) {
    // Currently the sync promo dialog has two problems. First, it's not modal
    // so the user can interact with other browser windows. Second, it uses
    // a nested message loop that can cause the sync promo page not to render.
    // To work around these problems the sync promo dialog is only shown for
    // the first profile. TODO(sail): Fix these issues if the sync promo dialog
    // is more widely deployed.
    ProfileInfoCache& cache =
        g_browser_process->profile_manager()->GetProfileInfoCache();
    if (cache.GetNumberOfProfiles() > 1 &&
        version == SyncPromoUI::VERSION_DIALOG) {
      return SyncPromoUI::VERSION_SIMPLE;
    }
    return version;
  }

  return VERSION_DEFAULT;
}
