// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/autocomplete/history_contents_provider.h"

#include "base/callback.h"
#include "base/metrics/histogram.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/autocomplete/autocomplete_match.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/bookmarks/bookmark_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/url_constants.h"
#include "googleurl/src/url_util.h"
#include "net/base/net_util.h"

using base::TimeTicks;

namespace {

// Number of days to search for full text results. The longer this is, the more
// time it will take.
const int kDaysToSearch = 30;

// When processing the results from the history query, this structure points to
// a single result. It allows the results to be sorted and processed without
// modifying the larger and slower results structure.
struct MatchReference {
  MatchReference(const history::URLResult* result, int relevance)
      : result(result),
        relevance(relevance) {
  }

  const history::URLResult* result;
  int relevance;  // Score of relevance computed by CalculateRelevance.
};

// This is a > operator for MatchReference.
bool CompareMatchRelevance(const MatchReference& a, const MatchReference& b) {
  if (a.relevance != b.relevance)
    return a.relevance > b.relevance;

  // Want results in reverse-chronological order all else being equal.
  return a.result->last_visit() > b.result->last_visit();
}

}  // namespace

using history::HistoryDatabase;

HistoryContentsProvider::HistoryContentsProvider(ACProviderListener* listener,
                                                 Profile* profile,
                                                 bool body_only)
    : HistoryProvider(listener, profile, "HistoryContents"),
      star_title_count_(0),
      star_contents_count_(0),
      title_count_(0),
      contents_count_(0),
      input_type_(AutocompleteInput::INVALID),
      body_only_(body_only),
      trim_http_(false),
      have_results_(false) {
}

void HistoryContentsProvider::Start(const AutocompleteInput& input,
                                    bool minimal_changes) {
  matches_.clear();

  if (input.text().empty() || (input.type() == AutocompleteInput::INVALID) ||
      !profile_ ||
      // The history service or bookmark bar model must exist.
      !(profile_->GetHistoryService(Profile::EXPLICIT_ACCESS) ||
        profile_->GetBookmarkModel())) {
    Stop();
    return;
  }

  // TODO(pkasting): http://b/888148 We disallow URL input and "URL-like" input
  // (REQUESTED_URL or UNKNOWN with dots) because we get poor results for it,
  // but we could get better results if we did better tokenizing instead.
  if ((input.type() == AutocompleteInput::URL) ||
      (((input.type() == AutocompleteInput::REQUESTED_URL) ||
        (input.type() == AutocompleteInput::UNKNOWN)) &&
       (input.text().find('.') != string16::npos))) {
    Stop();
    return;
  }

  if (input.matches_requested() == AutocompleteInput::BEST_MATCH) {
    // None of our results are applicable for best match.
    Stop();
    return;
  }

  // Change input type so matches will be marked up properly.
  input_type_ = input.type();
  trim_http_ = !HasHTTPScheme(input.text());

  // Decide what to do about any previous query/results.
  if (!minimal_changes) {
    // Any in-progress request is irrelevant, cancel it.
    Stop();
  } else if (have_results_) {
    // We finished the previous query and still have its results.  Mark them up
    // again for the new input.
    ConvertResults();
    return;
  } else if (!done_) {
    // We're still running the previous query on the HistoryService.  If we're
    // allowed to keep running it, do so, and when it finishes, its results will
    // get marked up for this new input.  In synchronous_only mode, cancel the
    // history query.
    if (input.matches_requested() != AutocompleteInput::ALL_MATCHES) {
      done_ = true;
      request_consumer_.CancelAllRequests();
    }
    ConvertResults();
    return;
  }

  if (!results_.empty()) {
    // Clear the results. We swap in an empty one as the easy way to clear it.
    history::QueryResults empty_results;
    results_.Swap(&empty_results);
  }

  // Querying bookmarks is synchronous, so we always do it.
  QueryBookmarks(input);

  // Convert the bookmark results.
  ConvertResults();

  if (input.matches_requested() == AutocompleteInput::ALL_MATCHES) {
    HistoryService* history =
        profile_->GetHistoryService(Profile::EXPLICIT_ACCESS);
    if (history) {
      done_ = false;

      history::QueryOptions options;
      options.body_only = body_only_;
      options.SetRecentDayRange(kDaysToSearch);
      options.max_count = kMaxMatches;
      history->QueryHistory(input.text(), options,
          &request_consumer_,
          NewCallback(this, &HistoryContentsProvider::QueryComplete));
    }
  }
}

void HistoryContentsProvider::Stop() {
  done_ = true;
  request_consumer_.CancelAllRequests();

  // Clear the results. We swap in an empty one as the easy way to clear it.
  history::QueryResults empty_results;
  results_.Swap(&empty_results);
  have_results_ = false;
}

HistoryContentsProvider::~HistoryContentsProvider() {
}

void HistoryContentsProvider::QueryComplete(HistoryService::Handle handle,
                                            history::QueryResults* results) {
  results_.AppendResultsBySwapping(results, true);
  have_results_ = true;
  ConvertResults();

  done_ = true;
  if (listener_)
    listener_->OnProviderUpdate(!matches_.empty());
}

void HistoryContentsProvider::ConvertResults() {
  // Reset the relevance counters so that result relevance won't vary on
  // subsequent passes of ConvertResults.
  star_title_count_ = star_contents_count_ = title_count_ = contents_count_ = 0;

  // Make the result references and score the results.
  std::vector<MatchReference> result_refs;
  result_refs.reserve(results_.size());

  // Results are sorted in decreasing order so we run the loop backwards so that
  // the relevance increment favors the higher ranked results.
  for (std::vector<history::URLResult*>::const_reverse_iterator i =
       results_.rbegin(); i != results_.rend(); ++i) {
    history::URLResult* result = *i;
    MatchReference ref(result, CalculateRelevance(*result));
    result_refs.push_back(ref);
  }

  // Get the top matches and add them.
  size_t max_for_provider = std::min(kMaxMatches, result_refs.size());
  std::partial_sort(result_refs.begin(), result_refs.begin() + max_for_provider,
                    result_refs.end(), &CompareMatchRelevance);
  matches_.clear();
  for (size_t i = 0; i < max_for_provider; i++) {
    matches_.push_back(ResultToMatch(*result_refs[i].result,
                                     result_refs[i].relevance));
  }
}

// TODO(mrossetti): Remove MatchInTitle once body_only_ becomes permanent.
bool HistoryContentsProvider::MatchInTitle(const history::URLResult& result) {
  return !body_only_ && !result.title_match_positions().empty();
}

AutocompleteMatch HistoryContentsProvider::ResultToMatch(
    const history::URLResult& result,
    int score) {
  AutocompleteMatch match(this, score, true, MatchInTitle(result) ?
      AutocompleteMatch::HISTORY_TITLE : AutocompleteMatch::HISTORY_BODY);
  match.contents = StringForURLDisplay(result.url(), true, trim_http_);
  match.fill_into_edit =
      AutocompleteInput::FormattedStringWithEquivalentMeaning(result.url(),
                                                              match.contents);
  match.destination_url = result.url();
  match.contents_class.push_back(
      ACMatchClassification(0, ACMatchClassification::URL));
  match.description = result.title();
  match.starred =
      (profile_->GetBookmarkModel() &&
       profile_->GetBookmarkModel()->IsBookmarked(result.url()));

  ClassifyDescription(result, &match);
  return match;
}

void HistoryContentsProvider::ClassifyDescription(
    const history::URLResult& result,
    AutocompleteMatch* match) const {
  const Snippet::MatchPositions& title_matches = result.title_match_positions();

  size_t offset = 0;
  if (!title_matches.empty()) {
    // Classify matches in the title.
    for (Snippet::MatchPositions::const_iterator i = title_matches.begin();
         i != title_matches.end(); ++i) {
      if (i->first != offset) {
        match->description_class.push_back(
            ACMatchClassification(offset, ACMatchClassification::NONE));
      }
      match->description_class.push_back(
            ACMatchClassification(i->first, ACMatchClassification::MATCH));
      offset = i->second;
    }
  }
  if (offset != result.title().length()) {
    match->description_class.push_back(
        ACMatchClassification(offset, ACMatchClassification::NONE));
  }
}

int HistoryContentsProvider::CalculateRelevance(
    const history::URLResult& result) {
  const bool in_title = MatchInTitle(result);
  if (!profile_->GetBookmarkModel() ||
      !profile_->GetBookmarkModel()->IsBookmarked(result.url()))
    return in_title ? (700 + title_count_++) : (500 + contents_count_++);
  return in_title ?
      (1000 + star_title_count_++) : (550 + star_contents_count_++);
}

void HistoryContentsProvider::QueryBookmarks(const AutocompleteInput& input) {
  BookmarkModel* bookmark_model = profile_->GetBookmarkModel();
  if (!bookmark_model)
    return;

  DCHECK(results_.empty());

  TimeTicks start_time = TimeTicks::Now();
  std::vector<bookmark_utils::TitleMatch> matches;
  bookmark_model->GetBookmarksWithTitlesMatching(input.text(),
                                                 kMaxMatches, &matches);
  for (size_t i = 0; i < matches.size(); ++i)
    AddBookmarkTitleMatchToResults(matches[i]);
  UMA_HISTOGRAM_TIMES("Omnibox.QueryBookmarksTime",
                      TimeTicks::Now() - start_time);
}

void HistoryContentsProvider::AddBookmarkTitleMatchToResults(
    const bookmark_utils::TitleMatch& match) {
  history::URLResult url_result(match.node->GetURL(), match.match_positions);
  url_result.set_title(match.node->GetTitle());
  results_.AppendURLBySwapping(&url_result);
}
