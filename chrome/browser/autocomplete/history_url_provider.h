// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_AUTOCOMPLETE_HISTORY_URL_PROVIDER_H_
#define CHROME_BROWSER_AUTOCOMPLETE_HISTORY_URL_PROVIDER_H_
#pragma once

#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "base/synchronization/cancellation_flag.h"
#include "chrome/browser/autocomplete/history_provider.h"
#include "chrome/browser/autocomplete/history_provider_util.h"

class MessageLoop;
class Profile;

namespace history {

class HistoryBackend;
class URLDatabase;

}  // namespace history

// How history autocomplete works
// ==============================
//
// Read down this diagram for temporal ordering.
//
//   Main thread                History thread
//   -----------                --------------
//   AutocompleteController::Start
//     -> HistoryURLProvider::Start
//       -> RunAutocompletePasses
//         -> SuggestExactInput
//         [params_ allocated]
//         -> DoAutocomplete (for inline autocomplete)
//           -> URLDatabase::AutocompleteForPrefix (on in-memory DB)
//         -> HistoryService::ScheduleAutocomplete
//         (return to controller) ----
//                                   /
//                              HistoryBackend::ScheduleAutocomplete
//                                -> HistoryURLProvider::ExecuteWithDB
//                                  -> DoAutocomplete
//                                    -> URLDatabase::AutocompleteForPrefix
//                                /
//   HistoryService::QueryComplete
//     [params_ destroyed]
//     -> AutocompleteProvider::Listener::OnProviderUpdate
//
// The autocomplete controller calls us, and must be called back, on the main
// thread.  When called, we run two autocomplete passes.  The first pass runs
// synchronously on the main thread and queries the in-memory URL database.
// This pass promotes matches for inline autocomplete if applicable.  We do
// this synchronously so that users get consistent behavior when they type
// quickly and hit enter, no matter how loaded the main history database is.
// Doing this synchronously also prevents inline autocomplete from being
// "flickery" in the AutocompleteEdit.  Because the in-memory DB does not have
// redirect data, results other than the top match might change between the
// two passes, so we can't just decide to use this pass' matches as the final
// results.
//
// The second autocomplete pass uses the full history database, which must be
// queried on the history thread.  Start() asks the history service schedule to
// callback on the history thread with a pointer to the main database.  When we
// are done doing queries, we schedule a task on the main thread that notifies
// the AutocompleteController that we're done.
//
// The communication between these threads is done using a
// HistoryURLProviderParams object.  This is allocated in the main thread, and
// normally deleted in QueryComplete().  So that both autocomplete passes can
// use the same code, we also use this to hold results during the first
// autocomplete pass.
//
// While the second pass is running, the AutocompleteController may cancel the
// request.  This can happen frequently when the user is typing quickly.  In
// this case, the main thread sets params_->cancel, which the background thread
// checks periodically.  If it finds the flag set, it stops what it's doing
// immediately and calls back to the main thread.  (We don't delete the params
// on the history thread, because we should only do that when we can safely
// NULL out params_, and that must be done on the main thread.)

// Used to communicate autocomplete parameters between threads via the history
// service.
struct HistoryURLProviderParams {
  HistoryURLProviderParams(const AutocompleteInput& input,
                           bool trim_http,
                           const std::string& languages);
  ~HistoryURLProviderParams();

  MessageLoop* message_loop;

  // A copy of the autocomplete input. We need the copy since this object will
  // live beyond the original query while it runs on the history thread.
  AutocompleteInput input;

  // Should inline autocompletion be disabled? This is initalized from
  // |input.prevent_inline_autocomplete()|, but set to false is the input
  // contains trailing white space.
  bool prevent_inline_autocomplete;

  // Set when "http://" should be trimmed from the beginning of the URLs.
  bool trim_http;

  // Set by the main thread to cancel this request.  If this flag is set when
  // the query runs, the query will be abandoned.  This allows us to avoid
  // running queries that are no longer needed.  Since we don't care if we run
  // the extra queries, the lack of signaling is not a problem.
  base::CancellationFlag cancel_flag;

  // Set by ExecuteWithDB() on the history thread when the query could not be
  // performed because the history system failed to properly init the database.
  // If this is set when the main thread is called back, it avoids changing
  // |matches_| at all, so it won't delete the default match
  // RunAutocompletePasses() creates.
  bool failed;

  // List of matches written by the history thread.  We keep this separate list
  // to avoid having the main thread read the provider's matches while the
  // history thread is manipulating them.  The provider copies this list back
  // to matches_ on the main thread in QueryComplete().
  ACMatches matches;

  // Languages we should pass to gfx::GetCleanStringFromUrl.
  std::string languages;

  // When true, we should avoid calling SuggestExactInput().
  bool dont_suggest_exact_input;

 private:
  DISALLOW_COPY_AND_ASSIGN(HistoryURLProviderParams);
};

// This class is an autocomplete provider and is also a pseudo-internal
// component of the history system.  See comments above.
class HistoryURLProvider : public HistoryProvider {
 public:
  HistoryURLProvider(ACProviderListener* listener, Profile* profile);

#ifdef UNIT_TEST
  HistoryURLProvider(ACProviderListener* listener,
                     Profile* profile,
                     const std::string& languages)
    : HistoryProvider(listener, profile, "History"),
      prefixes_(GetPrefixes()),
      params_(NULL),
      enable_aggressive_scoring_(false),
      languages_(languages) {}
#endif

  // AutocompleteProvider
  virtual void Start(const AutocompleteInput& input,
                     bool minimal_changes) OVERRIDE;
  virtual void Stop() OVERRIDE;

  // Runs the history query on the history thread, called by the history
  // system. The history database MAY BE NULL in which case it is not
  // available and we should return no data. Also schedules returning the
  // results to the main thread
  void ExecuteWithDB(history::HistoryBackend* backend,
                     history::URLDatabase* db,
                     HistoryURLProviderParams* params);

  // Actually runs the autocomplete job on the given database, which is
  // guaranteed not to be NULL.
  void DoAutocomplete(history::HistoryBackend* backend,
                      history::URLDatabase* db,
                      HistoryURLProviderParams* params);

  // Dispatches the results to the autocomplete controller. Called on the
  // main thread by ExecuteWithDB when the results are available.
  // Frees params_gets_deleted on exit.
  void QueryComplete(HistoryURLProviderParams* params_gets_deleted);

 private:
  enum MatchType {
    NORMAL,
    WHAT_YOU_TYPED,
    INLINE_AUTOCOMPLETE,
    UNVISITED_INTRANET,  // An intranet site that has never been visited.
  };
  class VisitClassifier;

  ~HistoryURLProvider();

  // Returns the set of prefixes to use for prefixes_.
  static history::Prefixes GetPrefixes();

  // Determines the relevance for a match, given its type.  Behavior
  // depends on enable_aggressive_scoring_.  If |match_type| is
  // NORMAL, |match_number| is a number [0, kMaxSuggestions)
  // indicating the relevance of the match (higher == more relevant).
  // For other values of |match_type|, |match_number| is ignored.
  // Only called some of the time; for some matches, relevancy scores
  // are assigned consecutively decreasing (1416, 1415, 1414, ...).
  int CalculateRelevance(MatchType match_type,
                         size_t match_number) const;

  // Helper function that actually launches the two autocomplete passes.
  void RunAutocompletePasses(const AutocompleteInput& input,
                             bool fixup_input_and_run_pass_1);

  // Returns the best prefix that begins |text|.  "Best" means "greatest number
  // of components".  This may return NULL if no prefix begins |text|.
  //
  // |prefix_suffix| (which may be empty) is appended to every attempted
  // prefix.  This is useful when you need to figure out the innermost match
  // for some user input in a URL.
  const history::Prefix* BestPrefix(const GURL& text,
                                    const string16& prefix_suffix) const;

  // Returns a match corresponding to exactly what the user has typed.
  AutocompleteMatch SuggestExactInput(const AutocompleteInput& input,
                                      bool trim_http);

  // Given a |match| containing the "what you typed" suggestion created by
  // SuggestExactInput(), looks up its info in the DB.  If found, fills in the
  // title from the DB, promotes the match's priority to that of an inline
  // autocomplete match (maybe it should be slightly better?), and places it on
  // the front of |matches| (so we pick the right matches to throw away
  // when culling redirects to/from it).  Returns whether a match was promoted.
  bool FixupExactSuggestion(history::URLDatabase* db,
                            const AutocompleteInput& input,
                            const VisitClassifier& classifier,
                            AutocompleteMatch* match,
                            history::HistoryMatches* matches) const;

  // Helper function for FixupExactSuggestion, this returns true if the input
  // corresponds to some intranet URL where the user has previously visited the
  // host in question.  In this case the input should be treated as a URL.
  bool CanFindIntranetURL(history::URLDatabase* db,
                          const AutocompleteInput& input) const;

  // Determines if |match| is suitable for inline autocomplete, and promotes it
  // if so.
  bool PromoteMatchForInlineAutocomplete(
      HistoryURLProviderParams* params,
      const history::HistoryMatch& match,
      const history::HistoryMatches& history_matches);

  // Sorts the given list of matches.
  void SortMatches(history::HistoryMatches* matches) const;

  // Removes results that have been rarely typed or visited, and not any time
  // recently.  The exact parameters for this heuristic can be found in the
  // function body.
  void CullPoorMatches(history::HistoryMatches* matches) const;

  // Removes results that redirect to each other, leaving at most |max_results|
  // results.
  void CullRedirects(history::HistoryBackend* backend,
                     history::HistoryMatches* matches,
                     size_t max_results) const;

  // Helper function for CullRedirects, this removes all but the first
  // occurance of [any of the set of strings in |remove|] from the |matches|
  // list.
  //
  // The return value is the index of the item that is after the item in the
  // input identified by |source_index|. If |source_index| or an item before
  // is removed, the next item will be shifted, and this allows the caller to
  // pick up on the next one when this happens.
  size_t RemoveSubsequentMatchesOf(history::HistoryMatches* matches,
                                   size_t source_index,
                                   const std::vector<GURL>& remove) const;

  // Converts a line from the database into an autocomplete match for display.
  AutocompleteMatch HistoryMatchToACMatch(
      HistoryURLProviderParams* params,
      const history::HistoryMatch& history_match,
      MatchType match_type,
      int relevance);

  // Prefixes to try appending to user input when looking for a match.
  const history::Prefixes prefixes_;

  // Params for the current query.  The provider should not free this directly;
  // instead, it is passed as a parameter through the history backend, and the
  // parameter itself is freed once it's no longer needed.  The only reason we
  // keep this member is so we can set the cancel bit on it.
  HistoryURLProviderParams* params_;

  // Command line flag omnibox-aggressive-with-history-urls.
  // We examine and cache the value in the constructor.
  bool enable_aggressive_scoring_;

  // Only used by unittests; if non-empty, overrides accept-languages in the
  // profile's pref system.
  std::string languages_;
};

#endif  // CHROME_BROWSER_AUTOCOMPLETE_HISTORY_URL_PROVIDER_H_
