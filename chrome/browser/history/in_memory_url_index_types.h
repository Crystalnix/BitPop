// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_HISTORY_IN_MEMORY_URL_INDEX_TYPES_H_
#define CHROME_BROWSER_HISTORY_IN_MEMORY_URL_INDEX_TYPES_H_
#pragma once

#include <map>
#include <set>
#include <vector>

#include "base/string16.h"
#include "chrome/browser/history/history_types.h"
#include "chrome/browser/autocomplete/history_provider_util.h"

namespace history {

// Matches within URL and Title Strings ----------------------------------------

// Specifies where an omnibox term occurs within a string. Used for specifying
// highlights in AutocompleteMatches (ACMatchClassifications) and to assist in
// scoring a result.
struct TermMatch {
  TermMatch() : term_num(0), offset(0), length(0) {}
  TermMatch(int term_num, size_t offset, size_t length)
      : term_num(term_num),
        offset(offset),
        length(length) {}

  int term_num;  // The index of the term in the original search string.
  size_t offset;  // The starting offset of the substring match.
  size_t length;  // The length of the substring match.
};
typedef std::vector<TermMatch> TermMatches;

// Returns a TermMatches which has an entry for each occurrence of the string
// |term| found in the string |string|. Mark each match with |term_num| so
// that the resulting TermMatches can be merged with other TermMatches for
// other terms. Note that only the first 2,048 characters of |string| are
// considered during the match operation.
TermMatches MatchTermInString(const string16& term,
                              const string16& string,
                              int term_num);

// Sorts and removes overlapping substring matches from |matches| and
// returns the cleaned up matches.
TermMatches SortAndDeoverlapMatches(const TermMatches& matches);

// Extracts and returns the offsets from |matches|.
std::vector<size_t> OffsetsFromTermMatches(const TermMatches& matches);

// Replaces the offsets in |matches| with those given in |offsets|, deleting
// any which are npos, and returns the updated list of matches.
TermMatches ReplaceOffsetsInTermMatches(const TermMatches& matches,
                                        const std::vector<size_t>& offsets);

// Used for intermediate history result operations -----------------------------

struct ScoredHistoryMatch : public history::HistoryMatch {
  ScoredHistoryMatch();  // Required by STL.
  explicit ScoredHistoryMatch(const history::URLRow& url_info);
  ~ScoredHistoryMatch();

  static bool MatchScoreGreater(const ScoredHistoryMatch& m1,
                                const ScoredHistoryMatch& m2);

  // An interim score taking into consideration location and completeness
  // of the match.
  int raw_score;
  TermMatches url_matches;  // Term matches within the URL.
  TermMatches title_matches;  // Term matches within the page title.
  bool can_inline;  // True if this is a candidate for in-line autocompletion.
};
typedef std::vector<ScoredHistoryMatch> ScoredHistoryMatches;

// Convenience Types -----------------------------------------------------------

typedef std::vector<string16> String16Vector;
typedef std::set<string16> String16Set;
typedef std::set<char16> Char16Set;
typedef std::vector<char16> Char16Vector;

// Utility Functions -----------------------------------------------------------

// Breaks a string down into individual words.
String16Set String16SetFromString16(const string16& uni_string);

// Breaks the |uni_string| string down into individual words and return
// a vector with the individual words in their original order. If
// |break_on_space| is false then the resulting list will contain only words
// containing alpha-numeric characters. If |break_on_space| is true then the
// resulting list will contain strings broken at whitespace. (|break_on_space|
// indicates that the BreakIterator::BREAK_SPACE (equivalent to BREAK_LINE)
// approach is to be used. For a complete description of this algorithm
// refer to the comments in base/i18n/break_iterator.h.)
//
// Example:
//   Given: |uni_string|: "http://www.google.com/ harry the rabbit."
//   With |break_on_space| false the returned list will contain:
//    "http", "www", "google", "com", "harry", "the", "rabbit"
//   With |break_on_space| true the returned list will contain:
//    "http://", "www.google.com/", "harry", "the", "rabbit."
String16Vector String16VectorFromString16(const string16& uni_string,
                                          bool break_on_space);

// Breaks the |uni_word| string down into its individual characters.
// Note that this is temporarily intended to work on a single word, but
// _will_ work on a string of words, perhaps with unexpected results.
// TODO(mrossetti): Lots of optimizations possible here for not restarting
// a search if the user is just typing along. Also, change this to uniString
// and properly handle substring matches, scoring and sorting the results
// by score. Also, provide the metrics for where the matches occur so that
// the UI can highlight the matched sections.
Char16Set Char16SetFromString16(const string16& uni_word);

// Determine if |prefix| is any of the standard 'ftp' or 'http[s]' prefixes.
bool IsInlineablePrefix(const string16& prefix);

// Support for InMemoryURLIndex Private Data -----------------------------------

// An index into a list of all of the words we have indexed.
typedef size_t WordID;

// A map allowing a WordID to be determined given a word.
typedef std::map<string16, WordID> WordMap;

// A map from character to the word_ids of words containing that character.
typedef std::set<WordID> WordIDSet;  // An index into the WordList.
typedef std::map<char16, WordIDSet> CharWordIDMap;

// A map from word (by word_id) to history items containing that word.
typedef history::URLID HistoryID;
typedef std::set<HistoryID> HistoryIDSet;
typedef std::vector<HistoryID> HistoryIDVector;
typedef std::map<WordID, HistoryIDSet> WordIDHistoryMap;
typedef std::map<HistoryID, WordIDSet> HistoryIDWordMap;

// A map from history_id to the history's URL and title.
typedef std::map<HistoryID, URLRow> HistoryInfoMap;

// TODO(rohitrao): Probably replace this with QueryResults.
typedef std::vector<history::URLRow> URLRowVector;

}  // namespace history

#endif  // CHROME_BROWSER_HISTORY_IN_MEMORY_URL_INDEX_TYPES_H_
