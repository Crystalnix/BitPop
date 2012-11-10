// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/autocomplete/autocomplete_controller.h"

#include <set>
#include <string>

#include "base/format_macros.h"
#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "base/string_number_conversions.h"
#include "base/stringprintf.h"
#include "base/time.h"
#include "chrome/browser/autocomplete/autocomplete_controller_delegate.h"
#include "chrome/browser/autocomplete/builtin_provider.h"
#include "chrome/browser/autocomplete/extension_app_provider.h"
#include "chrome/browser/autocomplete/history_contents_provider.h"
#include "chrome/browser/autocomplete/history_quick_provider.h"
#include "chrome/browser/autocomplete/history_url_provider.h"
#include "chrome/browser/autocomplete/keyword_provider.h"
#include "chrome/browser/autocomplete/search_provider.h"
#include "chrome/browser/autocomplete/shortcuts_provider.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/common/chrome_notification_types.h"
#include "content/public/browser/notification_service.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "ui/base/l10n/l10n_util.h"

namespace {

// Converts the given type to an integer based on the AQS specification.
// For more details, See http://goto.google.com/binary-clients-logging .
int AutocompleteMatchToAssistedQueryType(const AutocompleteMatch::Type& type) {
  switch (type) {
    case AutocompleteMatch::SEARCH_SUGGEST:        return 0;
    case AutocompleteMatch::NAVSUGGEST:            return 5;
    case AutocompleteMatch::SEARCH_WHAT_YOU_TYPED: return 57;
    case AutocompleteMatch::URL_WHAT_YOU_TYPED:    return 58;
    case AutocompleteMatch::SEARCH_HISTORY:        return 59;
    case AutocompleteMatch::HISTORY_URL:           return 60;
    case AutocompleteMatch::HISTORY_TITLE:         return 61;
    case AutocompleteMatch::HISTORY_BODY:          return 62;
    case AutocompleteMatch::HISTORY_KEYWORD:       return 63;
    default:                                       return 64;
  }
}

// Appends available autocompletion of the given type and number to the existing
// available autocompletions string, encoding according to the spec.
void AppendAvailableAutocompletion(int type,
                                   int count,
                                   std::string* autocompletions) {
  if (!autocompletions->empty())
    autocompletions->append("j");
  base::StringAppendF(autocompletions, "%d", type);
  if (count > 1)
    base::StringAppendF(autocompletions, "l%d", count);
}

// Amount of time (in ms) between when the user stops typing and when we remove
// any copied entries. We do this from the time the user stopped typing as some
// providers (such as SearchProvider) wait for the user to stop typing before
// they initiate a query.
const int kExpireTimeMS = 500;

}  // namespace

const int AutocompleteController::kNoItemSelected = -1;

AutocompleteController::AutocompleteController(
    Profile* profile,
    AutocompleteControllerDelegate* delegate)
    : delegate_(delegate),
      keyword_provider_(NULL),
      done_(true),
      in_start_(false),
      profile_(profile) {
  search_provider_ = new SearchProvider(this, profile);
  providers_.push_back(search_provider_);
#if !defined(OS_ANDROID)
  // History quick provider is enabled on all platforms other than Android.
  bool hqp_enabled = true;
  providers_.push_back(new HistoryQuickProvider(this, profile));
  // Search provider/"tab to search" is enabled on all platforms other than
  // Android.
  keyword_provider_ = new KeywordProvider(this, profile);
  providers_.push_back(keyword_provider_);
#else
  // TODO(mrossetti): Remove the following and permanently modify the
  // HistoryURLProvider to not search titles once HQP is turned on permanently.
  // TODO(jcivelli): Enable the History Quick Provider and figure out why it
  // reports the wrong results for some pages.
  bool hqp_enabled = false;
#endif  // !OS_ANDROID
  providers_.push_back(new HistoryURLProvider(this, profile));
  providers_.push_back(new ShortcutsProvider(this, profile));
  providers_.push_back(new HistoryContentsProvider(this, profile, hqp_enabled));
  providers_.push_back(new BuiltinProvider(this, profile));
  providers_.push_back(new ExtensionAppProvider(this, profile));
  for (ACProviders::iterator i(providers_.begin()); i != providers_.end(); ++i)
    (*i)->AddRef();
}

AutocompleteController::~AutocompleteController() {
  // The providers may have tasks outstanding that hold refs to them.  We need
  // to ensure they won't call us back if they outlive us.  (Practically,
  // calling Stop() should also cancel those tasks and make it so that we hold
  // the only refs.)  We also don't want to bother notifying anyone of our
  // result changes here, because the notification observer is in the midst of
  // shutdown too, so we don't ask Stop() to clear |result_| (and notify).
  result_.Reset();  // Not really necessary.
  Stop(false);

  for (ACProviders::iterator i(providers_.begin()); i != providers_.end(); ++i)
    (*i)->Release();

  providers_.clear();  // Not really necessary.
}

void AutocompleteController::Start(
    const string16& text,
    const string16& desired_tld,
    bool prevent_inline_autocomplete,
    bool prefer_keyword,
    bool allow_exact_keyword_match,
    AutocompleteInput::MatchesRequested matches_requested) {
  const string16 old_input_text(input_.text());
  const AutocompleteInput::MatchesRequested old_matches_requested =
      input_.matches_requested();
  input_ = AutocompleteInput(text, desired_tld, prevent_inline_autocomplete,
      prefer_keyword, allow_exact_keyword_match, matches_requested);

  // See if we can avoid rerunning autocomplete when the query hasn't changed
  // much.  When the user presses or releases the ctrl key, the desired_tld
  // changes, and when the user finishes an IME composition, inline autocomplete
  // may no longer be prevented.  In both these cases the text itself hasn't
  // changed since the last query, and some providers can do much less work (and
  // get matches back more quickly).  Taking advantage of this reduces flicker.
  //
  // NOTE: This comes after constructing |input_| above since that construction
  // can change the text string (e.g. by stripping off a leading '?').
  const bool minimal_changes = (input_.text() == old_input_text) &&
      (input_.matches_requested() == old_matches_requested);

  expire_timer_.Stop();

  // Start the new query.
  in_start_ = true;
  base::TimeTicks start_time = base::TimeTicks::Now();
  for (ACProviders::iterator i(providers_.begin()); i != providers_.end();
       ++i) {
    (*i)->Start(input_, minimal_changes);
    if (matches_requested != AutocompleteInput::ALL_MATCHES)
      DCHECK((*i)->done());
  }
  if (matches_requested == AutocompleteInput::ALL_MATCHES &&
      (text.length() < 6)) {
    base::TimeTicks end_time = base::TimeTicks::Now();
    std::string name = "Omnibox.QueryTime." + base::IntToString(text.length());
    base::Histogram* counter = base::Histogram::FactoryGet(
        name, 1, 1000, 50, base::Histogram::kUmaTargetedHistogramFlag);
    counter->Add(static_cast<int>((end_time - start_time).InMilliseconds()));
  }
  in_start_ = false;
  CheckIfDone();
  UpdateResult(true);

  if (!done_)
    StartExpireTimer();
}

void AutocompleteController::Stop(bool clear_result) {
  for (ACProviders::const_iterator i(providers_.begin()); i != providers_.end();
       ++i) {
    (*i)->Stop(clear_result);
  }

  expire_timer_.Stop();
  done_ = true;
  if (clear_result && !result_.empty()) {
    result_.Reset();
    // NOTE: We pass in false since we're trying to only clear the popup, not
    // touch the edit... this is all a mess and should be cleaned up :(
    NotifyChanged(false);
  }
}

void AutocompleteController::DeleteMatch(const AutocompleteMatch& match) {
  DCHECK(match.deletable);
  match.provider->DeleteMatch(match);  // This may synchronously call back to
                                       // OnProviderUpdate().
  // If DeleteMatch resulted in a callback to OnProviderUpdate and we're
  // not done, we might attempt to redisplay the deleted match. Make sure
  // we aren't displaying it by removing any old entries.
  ExpireCopiedEntries();
}

void AutocompleteController::ExpireCopiedEntries() {
  // Clear out the results. This ensures no results from the previous result set
  // are copied over.
  result_.Reset();
  // We allow matches from the previous result set to starve out matches from
  // the new result set. This means in order to expire matches we have to query
  // the providers again.
  UpdateResult(false);
}

void AutocompleteController::OnProviderUpdate(bool updated_matches) {
  CheckIfDone();
  // Multiple providers may provide synchronous results, so we only update the
  // results if we're not in Start().
  if (!in_start_ && (updated_matches || done_))
    UpdateResult(false);
}

void AutocompleteController::AddProvidersInfo(
    ProvidersInfo* provider_info) const {
  provider_info->clear();
  for (ACProviders::const_iterator i(providers_.begin()); i != providers_.end();
       ++i) {
    // Add per-provider info, if any.
    (*i)->AddProviderInfo(provider_info);

    // This is also a good place to put code to add info that you want to
    // add for every provider.
  }
}

void AutocompleteController::UpdateResult(bool is_synchronous_pass) {
  AutocompleteResult last_result;
  last_result.Swap(&result_);

  for (ACProviders::const_iterator i(providers_.begin()); i != providers_.end();
       ++i)
    result_.AppendMatches((*i)->matches());

  // Sort the matches and trim to a small number of "best" matches.
  result_.SortAndCull(input_);

  // Need to validate before invoking CopyOldMatches as the old matches are not
  // valid against the current input.
#ifndef NDEBUG
  result_.Validate();
#endif

  if (!done_) {
    // This conditional needs to match the conditional in Start that invokes
    // StartExpireTimer.
    result_.CopyOldMatches(input_, last_result);
  }

  UpdateKeywordDescriptions(&result_);
  UpdateAssociatedKeywords(&result_);
  UpdateAssistedQueryStats(&result_);

  bool notify_default_match = is_synchronous_pass;
  if (!is_synchronous_pass) {
    const bool last_default_was_valid =
        last_result.default_match() != last_result.end();
    const bool default_is_valid = result_.default_match() != result_.end();
    // We've gotten async results. Send notification that the default match
    // updated if fill_into_edit differs or associated_keyword differ.  (The
    // latter can change if we've just started Chrome and the keyword database
    // finishes loading while processing this request.) We don't check the URL
    // as that may change for the default match even though the fill into edit
    // hasn't changed (see SearchProvider for one case of this).
    notify_default_match =
        (last_default_was_valid != default_is_valid) ||
        (default_is_valid &&
          ((result_.default_match()->fill_into_edit !=
            last_result.default_match()->fill_into_edit) ||
            (result_.default_match()->associated_keyword.get() !=
              last_result.default_match()->associated_keyword.get())));
  }

  NotifyChanged(notify_default_match);
}

void AutocompleteController::UpdateAssociatedKeywords(
    AutocompleteResult* result) {
  if (!keyword_provider_)
    return;

  std::set<string16> keywords;
  for (ACMatches::iterator match(result->begin()); match != result->end();
       ++match) {
    string16 keyword(match->GetSubstitutingExplicitlyInvokedKeyword(profile_));
    if (!keyword.empty()) {
      keywords.insert(keyword);
    } else {
      string16 keyword = match->associated_keyword.get() ?
          match->associated_keyword->keyword :
          keyword_provider_->GetKeywordForText(match->fill_into_edit);

      // Only add the keyword if the match does not have a duplicate keyword
      // with a more relevant match.
      if (!keyword.empty() && !keywords.count(keyword)) {
        keywords.insert(keyword);

        if (!match->associated_keyword.get())
          match->associated_keyword.reset(new AutocompleteMatch(
              keyword_provider_->CreateAutocompleteMatch(match->fill_into_edit,
                  keyword, input_)));
      } else {
        match->associated_keyword.reset();
      }
    }
  }
}

void AutocompleteController::UpdateAssistedQueryStats(
    AutocompleteResult* result) {
  if (result->empty())
    return;

  // Build the impressions string (the AQS part after ".").
  std::string autocompletions;
  int count = 0;
  int last_type = -1;
  for (ACMatches::iterator match(result->begin()); match != result->end();
       ++match) {
    int type = AutocompleteMatchToAssistedQueryType(match->type);
    if (last_type != -1 && type != last_type) {
      AppendAvailableAutocompletion(last_type, count, &autocompletions);
      count = 1;
    } else {
      count++;
    }
    last_type = type;
  }
  AppendAvailableAutocompletion(last_type, count, &autocompletions);

  // Go over all matches and set AQS if the match supports it.
  for (size_t index = 0; index < result->size(); ++index) {
    AutocompleteMatch* match = result->match_at(index);
    const TemplateURL* template_url = match->GetTemplateURL(profile_);
    if (!template_url || !match->search_terms_args.get())
      continue;
    match->search_terms_args->assisted_query_stats =
        base::StringPrintf("chrome.%" PRIuS ".%s",
                           index,
                           autocompletions.c_str());
    match->destination_url = GURL(template_url->url_ref().ReplaceSearchTerms(
        *match->search_terms_args));
  }
}

void AutocompleteController::UpdateKeywordDescriptions(
    AutocompleteResult* result) {
  string16 last_keyword;
  for (AutocompleteResult::iterator i(result->begin()); i != result->end();
       ++i) {
    if (((i->provider == keyword_provider_) && !i->keyword.empty()) ||
        ((i->provider == search_provider_) &&
         (i->type == AutocompleteMatch::SEARCH_WHAT_YOU_TYPED ||
          i->type == AutocompleteMatch::SEARCH_HISTORY ||
          i->type == AutocompleteMatch::SEARCH_SUGGEST))) {
      i->description.clear();
      i->description_class.clear();
      DCHECK(!i->keyword.empty());
      if (i->keyword != last_keyword) {
        const TemplateURL* template_url = i->GetTemplateURL(profile_);
        if (template_url) {
          i->description = l10n_util::GetStringFUTF16(
              IDS_AUTOCOMPLETE_SEARCH_DESCRIPTION,
              template_url->AdjustedShortNameForLocaleDirection());
          i->description_class.push_back(
              ACMatchClassification(0, ACMatchClassification::DIM));
        }
        last_keyword = i->keyword;
      }
    } else {
      last_keyword.clear();
    }
  }
}

void AutocompleteController::NotifyChanged(bool notify_default_match) {
  if (delegate_)
    delegate_->OnResultChanged(notify_default_match);
  if (done_) {
    content::NotificationService::current()->Notify(
        chrome::NOTIFICATION_AUTOCOMPLETE_CONTROLLER_RESULT_READY,
        content::Source<AutocompleteController>(this),
        content::NotificationService::NoDetails());
  }
}

void AutocompleteController::CheckIfDone() {
  for (ACProviders::const_iterator i(providers_.begin()); i != providers_.end();
       ++i) {
    if (!(*i)->done()) {
      done_ = false;
      return;
    }
  }
  done_ = true;
}

void AutocompleteController::StartExpireTimer() {
  if (result_.HasCopiedMatches())
    expire_timer_.Start(FROM_HERE,
                        base::TimeDelta::FromMilliseconds(kExpireTimeMS),
                        this, &AutocompleteController::ExpireCopiedEntries);
}
