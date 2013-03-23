// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/search_engines/search_terms_data.h"

#include "base/logging.h"
#include "base/metrics/field_trial.h"
#include "base/string_number_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/google/google_url_tracker.h"
#include "chrome/browser/google/google_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_instant_controller.h"
#include "chrome/browser/ui/search/search.h"
#include "content/public/browser/browser_thread.h"
#include "googleurl/src/gurl.h"

#if defined(ENABLE_RLZ)
#include "chrome/browser/rlz/rlz.h"
#endif

using content::BrowserThread;

SearchTermsData::SearchTermsData() {
}

SearchTermsData::~SearchTermsData() {
}

std::string SearchTermsData::GoogleBaseURLValue() const {
  return GoogleURLTracker::kDefaultGoogleHomepage;
}

std::string SearchTermsData::GoogleBaseSuggestURLValue() const {
  // Start with the Google base URL.
  const GURL base_url(GoogleBaseURLValue());
  DCHECK(base_url.is_valid());

  GURL::Replacements repl;

  // Replace any existing path with "/complete/".
  // SetPathStr() requires its argument to stay in scope as long as |repl| is,
  // so "/complete/" can't be passed to SetPathStr() directly, it needs to be in
  // a variable.
  const std::string suggest_path("/complete/");
  repl.SetPathStr(suggest_path);

  // Clear the query and ref.
  repl.ClearQuery();
  repl.ClearRef();
  return base_url.ReplaceComponents(repl).spec();
}

std::string SearchTermsData::GetApplicationLocale() const {
  return "en";
}

string16 SearchTermsData::GetRlzParameterValue() const {
  return string16();
}

std::string SearchTermsData::GetSearchClient() const {
  return std::string();
}

std::string SearchTermsData::InstantEnabledParam() const {
  return std::string();
}

std::string SearchTermsData::InstantExtendedEnabledParam() const {
  return std::string();
}

// static
std::string* UIThreadSearchTermsData::google_base_url_ = NULL;

UIThreadSearchTermsData::UIThreadSearchTermsData(Profile* profile)
    : profile_(profile) {
  DCHECK(!BrowserThread::IsWellKnownThread(BrowserThread::UI) ||
      BrowserThread::CurrentlyOn(BrowserThread::UI));
}

std::string UIThreadSearchTermsData::GoogleBaseURLValue() const {
  DCHECK(!BrowserThread::IsWellKnownThread(BrowserThread::UI) ||
      BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (google_base_url_)
    return *google_base_url_;
  return profile_ ? GoogleURLTracker::GoogleURL(profile_).spec() :
      SearchTermsData::GoogleBaseURLValue();
}

std::string UIThreadSearchTermsData::GetApplicationLocale() const {
  DCHECK(!BrowserThread::IsWellKnownThread(BrowserThread::UI) ||
      BrowserThread::CurrentlyOn(BrowserThread::UI));
  return g_browser_process->GetApplicationLocale();
}

// Android implementations are located in search_terms_data_android.cc.
#if !defined(OS_ANDROID)
string16 UIThreadSearchTermsData::GetRlzParameterValue() const {
  DCHECK(!BrowserThread::IsWellKnownThread(BrowserThread::UI) ||
      BrowserThread::CurrentlyOn(BrowserThread::UI));
  string16 rlz_string;
#if defined(ENABLE_RLZ)
  // For organic brandcodes do not use rlz at all. Empty brandcode usually
  // means a chromium install. This is ok.
  std::string brand;
  if (google_util::GetBrand(&brand) && !brand.empty() &&
      !google_util::IsOrganic(brand)) {
    // This call will return false the first time(s) it is called until the
    // value has been cached. This normally would mean that at most one omnibox
    // search might not send the RLZ data but this is not really a problem.
    RLZTracker::GetAccessPointRlz(RLZTracker::CHROME_OMNIBOX, &rlz_string);
  }
#endif
  return rlz_string;
}

// We can enable this on non-Android if other platforms ever want a non-empty
// search client string.  There is already a unit test in place for Android
// called TemplateURLTest::SearchClient.
std::string UIThreadSearchTermsData::GetSearchClient() const {
  DCHECK(!BrowserThread::IsWellKnownThread(BrowserThread::UI) ||
      BrowserThread::CurrentlyOn(BrowserThread::UI));
  return std::string();
}
#endif

std::string UIThreadSearchTermsData::InstantEnabledParam() const {
  DCHECK(!BrowserThread::IsWellKnownThread(BrowserThread::UI) ||
         BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (profile_) {
    uint32 instant_extended_api_version =
        chrome::search::EmbeddedSearchPageVersion(profile_);
    if (instant_extended_api_version == 0 &&
        chrome::BrowserInstantController::IsInstantEnabled(profile_))
      return "ion=1&";
  }
  return std::string();
}

std::string UIThreadSearchTermsData::InstantExtendedEnabledParam() const {
  DCHECK(!BrowserThread::IsWellKnownThread(BrowserThread::UI) ||
         BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (profile_) {
    uint32 instant_extended_api_version =
        chrome::search::EmbeddedSearchPageVersion(profile_);
    if (instant_extended_api_version != 0) {
      return std::string(google_util::kInstantExtendedAPIParam) + "=" +
          base::Uint64ToString(instant_extended_api_version) + "&";
    }
  }
  return std::string();
}

// static
void UIThreadSearchTermsData::SetGoogleBaseURL(const std::string& base_url) {
  delete google_base_url_;
  google_base_url_ = base_url.empty() ? NULL : new std::string(base_url);
}
