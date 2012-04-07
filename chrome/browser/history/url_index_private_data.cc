// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/history/url_index_private_data.h"

#include <algorithm>
#include <functional>
#include <iterator>
#include <limits>
#include <numeric>

#include "base/file_util.h"
#include "base/i18n/case_conversion.h"
#include "base/metrics/histogram.h"
#include "base/string_util.h"
#include "base/threading/thread_restrictions.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/autocomplete/autocomplete.h"
#include "chrome/browser/history/url_database.h"
#include "chrome/common/url_constants.h"
#include "net/base/net_util.h"
#include "third_party/protobuf/src/google/protobuf/repeated_field.h"

using google::protobuf::RepeatedField;
using google::protobuf::RepeatedPtrField;
using in_memory_url_index::InMemoryURLIndexCacheItem;

namespace history {

typedef imui::InMemoryURLIndexCacheItem_WordListItem WordListItem;
typedef imui::InMemoryURLIndexCacheItem_WordMapItem_WordMapEntry WordMapEntry;
typedef imui::InMemoryURLIndexCacheItem_WordMapItem WordMapItem;
typedef imui::InMemoryURLIndexCacheItem_CharWordMapItem CharWordMapItem;
typedef imui::InMemoryURLIndexCacheItem_CharWordMapItem_CharWordMapEntry
    CharWordMapEntry;
typedef imui::InMemoryURLIndexCacheItem_WordIDHistoryMapItem
    WordIDHistoryMapItem;
typedef imui::
    InMemoryURLIndexCacheItem_WordIDHistoryMapItem_WordIDHistoryMapEntry
    WordIDHistoryMapEntry;
typedef imui::InMemoryURLIndexCacheItem_HistoryInfoMapItem HistoryInfoMapItem;
typedef imui::InMemoryURLIndexCacheItem_HistoryInfoMapItem_HistoryInfoMapEntry
    HistoryInfoMapEntry;

// The maximum score any candidate result can achieve.
const int kMaxTotalScore = 1425;

// Score ranges used to get a 'base' score for each of the scoring factors
// (such as recency of last visit, times visited, times the URL was typed,
// and the quality of the string match). There is a matching value range for
// each of these scores for each factor. Note that the top score is greater
// than |kMaxTotalScore|. The score for each candidate will be capped in the
// final calculation.
const int kScoreRank[] = { 1450, 1200, 900, 400 };

// SearchTermCacheItem ---------------------------------------------------------

URLIndexPrivateData::SearchTermCacheItem::SearchTermCacheItem(
    const WordIDSet& word_id_set,
    const HistoryIDSet& history_id_set)
    : word_id_set_(word_id_set),
      history_id_set_(history_id_set),
      used_(true) {}

URLIndexPrivateData::SearchTermCacheItem::SearchTermCacheItem()
    : used_(true) {}

URLIndexPrivateData::SearchTermCacheItem::~SearchTermCacheItem() {}

// Algorithm Functions ---------------------------------------------------------

// Comparison function for sorting search terms by descending length.
bool LengthGreater(const string16& string_a, const string16& string_b) {
  return string_a.length() > string_b.length();
}

// std::accumulate helper function to add up TermMatches' lengths.
int AccumulateMatchLength(int total, const TermMatch& match) {
  return total + match.length;
}

// Converts a raw value for some particular scoring factor into a score
// component for that factor.  The conversion function is piecewise linear, with
// input values provided in |value_ranks| and resulting output scores from
// |kScoreRank| (mathematically, f(value_rank[i]) = kScoreRank[i]).  A score
// cannot be higher than kScoreRank[0], and drops directly to 0 if lower than
// kScoreRank[3].
//
// For example, take |value| == 70 and |value_ranks| == { 100, 50, 30, 10 }.
// Because 70 falls between ranks 0 (100) and 1 (50), the score is given by the
// linear function:
//   score = m * value + b, where
//   m = (kScoreRank[0] - kScoreRank[1]) / (value_ranks[0] - value_ranks[1])
//   b = value_ranks[1]
// Any value higher than 100 would be scored as if it were 100, and any value
// lower than 10 scored 0.
int ScoreForValue(int value, const int* value_ranks) {
  int i = 0;
  int rank_count = arraysize(kScoreRank);
  while ((i < rank_count) && ((value_ranks[0] < value_ranks[1]) ?
         (value > value_ranks[i]) : (value < value_ranks[i])))
    ++i;
  if (i >= rank_count)
    return 0;
  int score = kScoreRank[i];
  if (i > 0) {
    score += (value - value_ranks[i]) *
        (kScoreRank[i - 1] - kScoreRank[i]) /
        (value_ranks[i - 1] - value_ranks[i]);
  }
  return score;
}

// InMemoryURLIndex's Private Data ---------------------------------------------

URLIndexPrivateData::URLIndexPrivateData()
    : pre_filter_item_count_(0),
      post_filter_item_count_(0),
      post_scoring_item_count_(0) {
  URLIndexPrivateData::InitializeSchemeWhitelist(&scheme_whitelist_);
}

URLIndexPrivateData::~URLIndexPrivateData() {}

void URLIndexPrivateData::Clear() {
  word_list_.clear();
  available_words_.clear();
  word_map_.clear();
  char_word_map_.clear();
  word_id_history_map_.clear();
  history_id_word_map_.clear();
  history_info_map_.clear();
}

// Cache Updating --------------------------------------------------------------

void URLIndexPrivateData::IndexRow(const URLRow& row) {
  const GURL& gurl(row.url());

  // Index only URLs with a whitelisted scheme.
  if (!URLIndexPrivateData::URLSchemeIsWhitelisted(gurl))
    return;

  URLID row_id = row.id();
  // Strip out username and password before saving and indexing.
  string16 url(net::FormatUrl(gurl, languages_,
      net::kFormatUrlOmitUsernamePassword,
      net::UnescapeRule::SPACES | net::UnescapeRule::URL_SPECIAL_CHARS,
      NULL, NULL, NULL));

  HistoryID history_id = static_cast<HistoryID>(row_id);
  DCHECK_LT(history_id, std::numeric_limits<HistoryID>::max());

  // Add the row for quick lookup in the history info store.
  URLRow new_row(GURL(url), row_id);
  new_row.set_visit_count(row.visit_count());
  new_row.set_typed_count(row.typed_count());
  new_row.set_last_visit(row.last_visit());
  new_row.set_title(row.title());
  history_info_map_[history_id] = new_row;

  // Index the words contained in the URL and title of the row.
  AddRowWordsToIndex(new_row);
  return;
}

void URLIndexPrivateData::AddRowWordsToIndex(const URLRow& row) {
  HistoryID history_id = static_cast<HistoryID>(row.id());
  // Split URL into individual, unique words then add in the title words.
  const GURL& gurl(row.url());
  string16 url(net::FormatUrl(gurl, languages_,
      net::kFormatUrlOmitUsernamePassword,
      net::UnescapeRule::SPACES | net::UnescapeRule::URL_SPECIAL_CHARS,
      NULL, NULL, NULL));
  url = base::i18n::ToLower(url);
  String16Set url_words = String16SetFromString16(url);
  String16Set title_words = String16SetFromString16(row.title());
  String16Set words;
  std::set_union(url_words.begin(), url_words.end(),
                 title_words.begin(), title_words.end(),
                 std::insert_iterator<String16Set>(words, words.begin()));
  for (String16Set::iterator word_iter = words.begin();
       word_iter != words.end(); ++word_iter)
    AddWordToIndex(*word_iter, history_id);

  search_term_cache_.clear();  // Invalidate the term cache.
}

void URLIndexPrivateData::AddWordToIndex(const string16& term,
                                         HistoryID history_id) {
  WordMap::iterator word_pos = word_map_.find(term);
  if (word_pos != word_map_.end())
    UpdateWordHistory(word_pos->second, history_id);
  else
    AddWordHistory(term, history_id);
}

void URLIndexPrivateData::UpdateWordHistory(WordID word_id,
                                            HistoryID history_id) {
  WordIDHistoryMap::iterator history_pos = word_id_history_map_.find(word_id);
  DCHECK(history_pos != word_id_history_map_.end());
  HistoryIDSet& history_id_set(history_pos->second);
  history_id_set.insert(history_id);
  AddToHistoryIDWordMap(history_id, word_id);
}

void URLIndexPrivateData::AddWordHistory(const string16& term,
                                         HistoryID history_id) {
  WordID word_id = word_list_.size();
  if (available_words_.empty()) {
    word_list_.push_back(term);
  } else {
    word_id = *(available_words_.begin());
    word_list_[word_id] = term;
    available_words_.erase(word_id);
  }
  word_map_[term] = word_id;

  HistoryIDSet history_id_set;
  history_id_set.insert(history_id);
  word_id_history_map_[word_id] = history_id_set;
  AddToHistoryIDWordMap(history_id, word_id);

  // For each character in the newly added word (i.e. a word that is not
  // already in the word index), add the word to the character index.
  Char16Set characters = Char16SetFromString16(term);
  for (Char16Set::iterator uni_char_iter = characters.begin();
       uni_char_iter != characters.end(); ++uni_char_iter) {
    char16 uni_char = *uni_char_iter;
    CharWordIDMap::iterator char_iter = char_word_map_.find(uni_char);
    if (char_iter != char_word_map_.end()) {
      // Update existing entry in the char/word index.
      WordIDSet& word_id_set(char_iter->second);
      word_id_set.insert(word_id);
    } else {
      // Create a new entry in the char/word index.
      WordIDSet word_id_set;
      word_id_set.insert(word_id);
      char_word_map_[uni_char] = word_id_set;
    }
  }
}

void URLIndexPrivateData::RemoveRowFromIndex(const URLRow& row) {
  RemoveRowWordsFromIndex(row);
  HistoryID history_id = static_cast<HistoryID>(row.id());
  history_info_map_.erase(history_id);
}

void URLIndexPrivateData::RemoveRowWordsFromIndex(const URLRow& row) {
  // Remove the entries in history_id_word_map_ and word_id_history_map_ for
  // this row.
  HistoryID history_id = static_cast<HistoryID>(row.id());
  WordIDSet word_id_set = history_id_word_map_[history_id];
  history_id_word_map_.erase(history_id);

  // Reconcile any changes to word usage.
  for (WordIDSet::iterator word_id_iter = word_id_set.begin();
       word_id_iter != word_id_set.end(); ++word_id_iter) {
    WordID word_id = *word_id_iter;
    word_id_history_map_[word_id].erase(history_id);
    if (!word_id_history_map_[word_id].empty())
      continue;  // The word is still in use.

    // The word is no longer in use. Reconcile any changes to character usage.
    string16 word = word_list_[word_id];
    Char16Set characters = Char16SetFromString16(word);
    for (Char16Set::iterator uni_char_iter = characters.begin();
         uni_char_iter != characters.end(); ++uni_char_iter) {
      char16 uni_char = *uni_char_iter;
      char_word_map_[uni_char].erase(word_id);
      if (char_word_map_[uni_char].empty())
        char_word_map_.erase(uni_char);  // No longer in use.
    }

    // Complete the removal of references to the word.
    word_id_history_map_.erase(word_id);
    word_map_.erase(word);
    word_list_[word_id] = string16();
    available_words_.insert(word_id);
  }
}

void URLIndexPrivateData::AddToHistoryIDWordMap(HistoryID history_id,
                                                WordID word_id) {
  HistoryIDWordMap::iterator iter = history_id_word_map_.find(history_id);
  if (iter != history_id_word_map_.end()) {
    WordIDSet& word_id_set(iter->second);
    word_id_set.insert(word_id);
  } else {
    WordIDSet word_id_set;
    word_id_set.insert(word_id);
    history_id_word_map_[history_id] = word_id_set;
  }
}

void URLIndexPrivateData::UpdateURL(URLID row_id, const URLRow& row) {
  // The row may or may not already be in our index. If it is not already
  // indexed and it qualifies then it gets indexed. If it is already
  // indexed and still qualifies then it gets updated, otherwise it
  // is deleted from the index.
  HistoryInfoMap::iterator row_pos = history_info_map_.find(row_id);
  if (row_pos == history_info_map_.end()) {
    // This new row should be indexed if it qualifies.
    URLRow new_row(row);
    new_row.set_id(row_id);
    if (RowQualifiesAsSignificant(new_row, base::Time()))
      IndexRow(new_row);
  } else if (RowQualifiesAsSignificant(row, base::Time())) {
    // This indexed row still qualifies and will be re-indexed.
    // The url won't have changed but the title, visit count, etc.
    // might have changed.
    URLRow& updated_row = row_pos->second;
    updated_row.set_visit_count(row.visit_count());
    updated_row.set_typed_count(row.typed_count());
    updated_row.set_last_visit(row.last_visit());
    // While the URL is guaranteed to remain stable, the title may have changed.
    // If so, then we need to update the index with the changed words.
    if (updated_row.title() != row.title()) {
      // Clear all words associated with this row and re-index both the
      // URL and title.
      RemoveRowWordsFromIndex(updated_row);
      updated_row.set_title(row.title());
      AddRowWordsToIndex(updated_row);
    }
  } else {
    // This indexed row no longer qualifies and will be de-indexed by
    // clearing all words associated with this row.
    URLRow& removed_row = row_pos->second;
    RemoveRowFromIndex(removed_row);
  }
  // This invalidates the cache.
  search_term_cache_.clear();
}

void URLIndexPrivateData::DeleteURL(URLID row_id) {
  // Note that this does not remove any reference to this row from the
  // word_id_history_map_. That map will continue to contain (and return)
  // hits against this row until that map is rebuilt, but since the
  // history_info_map_ no longer references the row no erroneous results
  // will propagate to the user.
  history_info_map_.erase(row_id);
  search_term_cache_.clear();  // This invalidates the word cache.
}

bool URLIndexPrivateData::URLSchemeIsWhitelisted(const GURL& gurl) const {
  return scheme_whitelist_.find(gurl.scheme()) != scheme_whitelist_.end();
}

// URLIndexPrivateData::HistoryItemFactorGreater -------------------------------

URLIndexPrivateData::HistoryItemFactorGreater::HistoryItemFactorGreater(
    const HistoryInfoMap& history_info_map)
    : history_info_map_(history_info_map) {
}

URLIndexPrivateData::HistoryItemFactorGreater::~HistoryItemFactorGreater() {}

bool URLIndexPrivateData::HistoryItemFactorGreater::operator()(
    const HistoryID h1,
    const HistoryID h2) {
  HistoryInfoMap::const_iterator entry1(history_info_map_.find(h1));
  if (entry1 == history_info_map_.end())
    return false;
  HistoryInfoMap::const_iterator entry2(history_info_map_.find(h2));
  if (entry2 == history_info_map_.end())
    return true;
  const URLRow& r1(entry1->second);
  const URLRow& r2(entry2->second);
  // First cut: typed count, visit count, recency.
  // TODO(mrossetti): This is too simplistic. Consider an approach which ranks
  // recently visited (within the last 12/24 hours) as highly important. Get
  // input from mpearson.
  if (r1.typed_count() != r2.typed_count())
    return (r1.typed_count() > r2.typed_count());
  if (r1.visit_count() != r2.visit_count())
    return (r1.visit_count() > r2.visit_count());
  return (r1.last_visit() > r2.last_visit());
}

// Cache Searching -------------------------------------------------------------

// NOTE: This is the main public search function.
ScoredHistoryMatches URLIndexPrivateData::HistoryItemsForTerms(
    const string16& term_string) {
  pre_filter_item_count_ = 0;
  post_filter_item_count_ = 0;
  post_scoring_item_count_ = 0;
  string16 clean_string = net::UnescapeURLComponent(term_string,
      net::UnescapeRule::SPACES | net::UnescapeRule::URL_SPECIAL_CHARS);
  string16 lower_string(base::i18n::ToLower(clean_string));
  String16Vector terms(
      history::String16VectorFromString16(lower_string, false));
  ScoredHistoryMatches scored_items;

  // Do nothing if we have indexed no words (probably because we've not been
  // initialized yet) or the search string has no words.
  if (word_list_.empty() || terms.empty()) {
    search_term_cache_.clear();  // Invalidate the term cache.
    return scored_items;
  }

  // Reset used_ flags for search_term_cache_. We use a basic mark-and-sweep
  // approach.
  ResetSearchTermCache();

  HistoryIDSet history_id_set = HistoryIDSetFromWords(terms);

  // Trim the candidate pool if it is large. Note that we do not filter out
  // items that do not contain the search terms as proper substrings -- doing
  // so is the performance-costly operation we are trying to avoid in order
  // to maintain omnibox responsiveness.
  const size_t kItemsToScoreLimit = 500;
  pre_filter_item_count_ = history_id_set.size();
  // If we trim the results set we do not want to cache the results for next
  // time as the user's ultimately desired result could easily be eliminated
  // in this early rough filter.
  bool was_trimmed = (pre_filter_item_count_ > kItemsToScoreLimit);
  if (was_trimmed) {
    HistoryIDVector history_ids;
    std::copy(history_id_set.begin(), history_id_set.end(),
              std::back_inserter(history_ids));
    // Trim down the set by sorting by typed-count, visit-count, and last
    // visit.
    HistoryItemFactorGreater
        item_factor_functor(history_info_map_);
    std::partial_sort(history_ids.begin(),
                      history_ids.begin() + kItemsToScoreLimit,
                      history_ids.end(),
                      item_factor_functor);
    history_id_set.clear();
    std::copy(history_ids.begin(), history_ids.begin() + kItemsToScoreLimit,
              std::inserter(history_id_set, history_id_set.end()));
    post_filter_item_count_ = history_id_set.size();
  }

  // Pass over all of the candidates filtering out any without a proper
  // substring match, inserting those which pass in order by score.
  history::String16Vector lower_words;
  Tokenize(lower_string, kWhitespaceUTF16, &lower_words);
  scored_items = std::for_each(history_id_set.begin(), history_id_set.end(),
      AddHistoryMatch(*this, lower_string, lower_words)).ScoredMatches();

  // Select and sort only the top kMaxMatches results.
  if (scored_items.size() > AutocompleteProvider::kMaxMatches) {
    std::partial_sort(scored_items.begin(),
                      scored_items.begin() +
                          AutocompleteProvider::kMaxMatches,
                      scored_items.end(),
                      ScoredHistoryMatch::MatchScoreGreater);
      scored_items.resize(AutocompleteProvider::kMaxMatches);
  } else {
    std::sort(scored_items.begin(), scored_items.end(),
              ScoredHistoryMatch::MatchScoreGreater);
  }
  post_scoring_item_count_ = scored_items.size();

  if (was_trimmed) {
    search_term_cache_.clear();  // Invalidate the term cache.
  } else {
    // Remove any stale SearchTermCacheItems.
    for (SearchTermCacheMap::iterator cache_iter = search_term_cache_.begin();
         cache_iter != search_term_cache_.end(); ) {
      if (!cache_iter->second.used_)
        search_term_cache_.erase(cache_iter++);
      else
        ++cache_iter;
    }
  }

  return scored_items;
}

// URLIndexPrivateData::AddHistoryMatch ----------------------------------------

URLIndexPrivateData::AddHistoryMatch::AddHistoryMatch(
    const URLIndexPrivateData& private_data,
    const string16& lower_string,
    const String16Vector& lower_terms)
  : private_data_(private_data),
    lower_string_(lower_string),
    lower_terms_(lower_terms) {}

URLIndexPrivateData::AddHistoryMatch::~AddHistoryMatch() {}

void URLIndexPrivateData::AddHistoryMatch::operator()(
    const HistoryID history_id) {
  HistoryInfoMap::const_iterator hist_pos =
      private_data_.history_info_map_.find(history_id);
  // Note that a history_id may be present in the word_id_history_map_ yet not
  // be found in the history_info_map_. This occurs when an item has been
  // deleted by the user or the item no longer qualifies as a quick result.
  if (hist_pos != private_data_.history_info_map_.end()) {
    const URLRow& hist_item = hist_pos->second;
    ScoredHistoryMatch match(
        ScoredMatchForURL(hist_item, lower_string_, lower_terms_));
    if (match.raw_score > 0)
      scored_matches_.push_back(match);
  }
}

// static
// TODO(mrossetti): This can be made a ctor for ScoredHistoryMatch.
ScoredHistoryMatch URLIndexPrivateData::ScoredMatchForURL(
    const URLRow& row,
    const string16& lower_string,
    const String16Vector& terms) {
  ScoredHistoryMatch match(row);
  GURL gurl = row.url();
  if (!gurl.is_valid())
    return match;

  // Figure out where each search term appears in the URL and/or page title
  // so that we can score as well as provide autocomplete highlighting.
  string16 url = base::i18n::ToLower(UTF8ToUTF16(gurl.spec()));
  string16 title = base::i18n::ToLower(row.title());
  int term_num = 0;
  for (String16Vector::const_iterator iter = terms.begin(); iter != terms.end();
       ++iter, ++term_num) {
    string16 term = *iter;
    TermMatches url_term_matches = MatchTermInString(term, url, term_num);
    TermMatches title_term_matches = MatchTermInString(term, title, term_num);
    if (url_term_matches.empty() && title_term_matches.empty())
      return match;  // A term was not found in either URL or title - reject.
    match.url_matches.insert(match.url_matches.end(), url_term_matches.begin(),
                             url_term_matches.end());
    match.title_matches.insert(match.title_matches.end(),
                               title_term_matches.begin(),
                               title_term_matches.end());
  }

  // Sort matches by offset and eliminate any which overlap.
  match.url_matches = SortAndDeoverlapMatches(match.url_matches);
  match.title_matches = SortAndDeoverlapMatches(match.title_matches);

  // We can inline autocomplete a result if:
  //  1) there is only one search term
  //  2) AND EITHER:
  //    2a) the first match starts at the beginning of the candidate URL, OR
  //    2b) the candidate URL starts with one of the standard URL prefixes with
  //        the URL match immediately following that prefix.
  //  3) AND the search string does not end in whitespace (making it look to
  //     the IMUI as though there is a single search term when actually there
  //     is a second, empty term).
  match.can_inline = !match.url_matches.empty() &&
      terms.size() == 1 &&
      (match.url_matches[0].offset == 0 ||
       IsInlineablePrefix(url.substr(0, match.url_matches[0].offset))) &&
      !IsWhitespace(*(lower_string.rbegin()));
  match.match_in_scheme = match.can_inline && match.url_matches[0].offset == 0;

  // Get partial scores based on term matching. Note that the score for
  // each of the URL and title are adjusted by the fraction of the
  // terms appearing in each.
  int url_score = ScoreComponentForMatches(match.url_matches, url.length()) *
      std::min(match.url_matches.size(), terms.size()) / terms.size();
  int title_score =
      ScoreComponentForMatches(match.title_matches, title.length()) *
      std::min(match.title_matches.size(), terms.size()) / terms.size();
  // Arbitrarily pick the best.
  // TODO(mrossetti): It might make sense that a term which appears in both the
  // URL and the Title should boost the score a bit.
  int term_score = std::max(url_score, title_score);
  if (term_score == 0)
    return match;

  // Determine scoring factors for the recency of visit, visit count and typed
  // count attributes of the URLRow.
  const int kDaysAgoLevel[] = { 1, 10, 20, 30 };
  int days_ago_value = ScoreForValue((base::Time::Now() -
      row.last_visit()).InDays(), kDaysAgoLevel);
  const int kVisitCountLevel[] = { 50, 30, 10, 5 };
  int visit_count_value = ScoreForValue(row.visit_count(), kVisitCountLevel);
  const int kTypedCountLevel[] = { 50, 30, 10, 5 };
  int typed_count_value = ScoreForValue(row.typed_count(), kTypedCountLevel);

  // The final raw score is calculated by:
  //   - multiplying each factor by a 'relevance'
  //   - calculating the average.
  // Note that visit_count is reduced by typed_count because both are bumped
  // when a typed URL is recorded thus giving visit_count too much weight.
  const int kTermScoreRelevance = 4;
  const int kDaysAgoRelevance = 2;
  const int kVisitCountRelevance = 2;
  const int kTypedCountRelevance = 5;
  int effective_visit_count_value =
      std::max(0, visit_count_value - typed_count_value);
  match.raw_score = term_score * kTermScoreRelevance +
                    days_ago_value * kDaysAgoRelevance +
                    effective_visit_count_value * kVisitCountRelevance +
                    typed_count_value * kTypedCountRelevance;
  match.raw_score /= (kTermScoreRelevance + kDaysAgoRelevance +
                      kVisitCountRelevance + kTypedCountRelevance);
  match.raw_score = std::min(kMaxTotalScore, match.raw_score);

  return match;
}

int URLIndexPrivateData::ScoreComponentForMatches(const TermMatches& matches,
                                                  size_t max_length) {
  if (matches.empty())
    return 0;

  // Score component for whether the input terms (if more than one) were found
  // in the same order in the match.  Start with kOrderMaxValue points divided
  // equally among (number of terms - 1); then discount each of those terms that
  // is out-of-order in the match.
  const int kOrderMaxValue = 1000;
  int order_value = kOrderMaxValue;
  if (matches.size() > 1) {
    int max_possible_out_of_order = matches.size() - 1;
    int out_of_order = 0;
    for (size_t i = 1; i < matches.size(); ++i) {
      if (matches[i - 1].term_num > matches[i].term_num)
        ++out_of_order;
    }
    order_value = (max_possible_out_of_order - out_of_order) * kOrderMaxValue /
        max_possible_out_of_order;
  }

  // Score component for how early in the match string the first search term
  // appears.  Start with kStartMaxValue points and discount by
  // kStartMaxValue/kMaxSignificantStart points for each character later than
  // the first at which the term begins. No points are earned if the start of
  // the match occurs at or after kMaxSignificantStart.
  const size_t kMaxSignificantStart = 50;
  const int kStartMaxValue = 1000;
  int start_value = (kMaxSignificantStart -
      std::min(kMaxSignificantStart, matches[0].offset)) * kStartMaxValue /
      kMaxSignificantStart;

  // Score component for how much of the matched string the input terms cover.
  // kCompleteMaxValue points times the fraction of the URL/page title string
  // that was matched.
  size_t term_length_total = std::accumulate(matches.begin(), matches.end(),
                                             0, AccumulateMatchLength);
  const size_t kMaxSignificantLength = 50;
  size_t max_significant_length =
      std::min(max_length, std::max(term_length_total, kMaxSignificantLength));
  const int kCompleteMaxValue = 1000;
  int complete_value =
      term_length_total * kCompleteMaxValue / max_significant_length;

  const int kOrderRelevance = 1;
  const int kStartRelevance = 6;
  const int kCompleteRelevance = 3;
  int raw_score = order_value * kOrderRelevance +
                  start_value * kStartRelevance +
                  complete_value * kCompleteRelevance;
  raw_score /= (kOrderRelevance + kStartRelevance + kCompleteRelevance);

  // Scale the raw score into a single score component in the same manner as
  // used in ScoredMatchForURL().
  const int kTermScoreLevel[] = { 1000, 750, 500, 200 };
  return ScoreForValue(raw_score, kTermScoreLevel);
}

void URLIndexPrivateData::ResetSearchTermCache() {
  for (SearchTermCacheMap::iterator iter = search_term_cache_.begin();
       iter != search_term_cache_.end(); ++iter)
    iter->second.used_ = false;
}

HistoryIDSet URLIndexPrivateData::HistoryIDSetFromWords(
    const String16Vector& unsorted_words) {
  // Break the terms down into individual terms (words), get the candidate
  // set for each term, and intersect each to get a final candidate list.
  // Note that a single 'term' from the user's perspective might be
  // a string like "http://www.somewebsite.com" which, from our perspective,
  // is four words: 'http', 'www', 'somewebsite', and 'com'.
  HistoryIDSet history_id_set;
  String16Vector words(unsorted_words);
  // Sort the words into the longest first as such are likely to narrow down
  // the results quicker. Also, single character words are the most expensive
  // to process so save them for last.
  std::sort(words.begin(), words.end(), LengthGreater);
  for (String16Vector::iterator iter = words.begin(); iter != words.end();
       ++iter) {
    string16 uni_word = *iter;
    HistoryIDSet term_history_set = HistoryIDsForTerm(uni_word);
    if (term_history_set.empty()) {
      history_id_set.clear();
      break;
    }
    if (iter == words.begin()) {
      history_id_set.swap(term_history_set);
    } else {
      HistoryIDSet new_history_id_set;
      std::set_intersection(history_id_set.begin(), history_id_set.end(),
                            term_history_set.begin(), term_history_set.end(),
                            std::inserter(new_history_id_set,
                                          new_history_id_set.begin()));
      history_id_set.swap(new_history_id_set);
    }
  }
  return history_id_set;
}

HistoryIDSet URLIndexPrivateData::HistoryIDsForTerm(
    const string16& term) {
  if (term.empty())
    return HistoryIDSet();

  // TODO(mrossetti): Consider optimizing for very common terms such as
  // 'http[s]', 'www', 'com', etc. Or collect the top 100 more frequently
  // occuring words in the user's searches.

  size_t term_length = term.length();
  WordIDSet word_id_set;
  if (term_length > 1) {
    // See if this term or a prefix thereof is present in the cache.
    SearchTermCacheMap::iterator best_prefix(search_term_cache_.end());
    for (SearchTermCacheMap::iterator cache_iter = search_term_cache_.begin();
         cache_iter != search_term_cache_.end(); ++cache_iter) {
      if (StartsWith(term, cache_iter->first, false) &&
          (best_prefix == search_term_cache_.end() ||
           cache_iter->first.length() > best_prefix->first.length()))
        best_prefix = cache_iter;
    }

    // If a prefix was found then determine the leftover characters to be used
    // for further refining the results from that prefix.
    Char16Set prefix_chars;
    string16 leftovers(term);
    if (best_prefix != search_term_cache_.end()) {
      // If the prefix is an exact match for the term then grab the cached
      // results and we're done.
      size_t prefix_length = best_prefix->first.length();
      if (prefix_length == term_length) {
        best_prefix->second.used_ = true;
        return best_prefix->second.history_id_set_;
      }

      // Otherwise we have a handy starting point.
      // If there are no history results for this prefix then we can bail early
      // as there will be no history results for the full term.
      if (best_prefix->second.history_id_set_.empty()) {
        search_term_cache_[term] = SearchTermCacheItem();
        return HistoryIDSet();
      }
      word_id_set = best_prefix->second.word_id_set_;
      prefix_chars = Char16SetFromString16(best_prefix->first);
      leftovers = term.substr(prefix_length);
    }

    // Filter for each remaining, unique character in the term.
    Char16Set leftover_chars = Char16SetFromString16(leftovers);
    Char16Set unique_chars;
    std::set_difference(leftover_chars.begin(), leftover_chars.end(),
                        prefix_chars.begin(), prefix_chars.end(),
                        std::inserter(unique_chars, unique_chars.begin()));

    // Reduce the word set with any leftover, unprocessed characters.
    if (!unique_chars.empty()) {
      WordIDSet leftover_set(WordIDSetForTermChars(unique_chars));
      // We might come up empty on the leftovers.
      if (leftover_set.empty()) {
        search_term_cache_[term] = SearchTermCacheItem();
        return HistoryIDSet();
      }
      // Or there may not have been a prefix from which to start.
      if (prefix_chars.empty()) {
        word_id_set.swap(leftover_set);
      } else {
        WordIDSet new_word_id_set;
        std::set_intersection(word_id_set.begin(), word_id_set.end(),
                              leftover_set.begin(), leftover_set.end(),
                              std::inserter(new_word_id_set,
                                            new_word_id_set.begin()));
        word_id_set.swap(new_word_id_set);
      }
    }

    // We must filter the word list because the resulting word set surely
    // contains words which do not have the search term as a proper subset.
    for (WordIDSet::iterator word_set_iter = word_id_set.begin();
         word_set_iter != word_id_set.end(); ) {
      if (word_list_[*word_set_iter].find(term) == string16::npos)
        word_id_set.erase(word_set_iter++);
      else
        ++word_set_iter;
    }
  } else {
    word_id_set = WordIDSetForTermChars(Char16SetFromString16(term));
  }

  // If any words resulted then we can compose a set of history IDs by unioning
  // the sets from each word.
  HistoryIDSet history_id_set;
  if (!word_id_set.empty()) {
    for (WordIDSet::iterator word_id_iter = word_id_set.begin();
         word_id_iter != word_id_set.end(); ++word_id_iter) {
      WordID word_id = *word_id_iter;
      WordIDHistoryMap::iterator word_iter = word_id_history_map_.find(word_id);
      if (word_iter != word_id_history_map_.end()) {
        HistoryIDSet& word_history_id_set(word_iter->second);
        history_id_set.insert(word_history_id_set.begin(),
                              word_history_id_set.end());
      }
    }
  }

  // Record a new cache entry for this word if the term is longer than
  // a single character.
  if (term_length > 1)
    search_term_cache_[term] = SearchTermCacheItem(word_id_set, history_id_set);

  return history_id_set;
}

WordIDSet URLIndexPrivateData::WordIDSetForTermChars(
    const Char16Set& term_chars) {
  WordIDSet word_id_set;
  for (Char16Set::const_iterator c_iter = term_chars.begin();
       c_iter != term_chars.end(); ++c_iter) {
    CharWordIDMap::iterator char_iter = char_word_map_.find(*c_iter);
    if (char_iter == char_word_map_.end()) {
      // A character was not found so there are no matching results: bail.
      word_id_set.clear();
      break;
    }
    WordIDSet& char_word_id_set(char_iter->second);
    // It is possible for there to no longer be any words associated with
    // a particular character. Give up in that case.
    if (char_word_id_set.empty()) {
      word_id_set.clear();
      break;
    }

    if (c_iter == term_chars.begin()) {
      // First character results becomes base set of results.
      word_id_set = char_word_id_set;
    } else {
      // Subsequent character results get intersected in.
      WordIDSet new_word_id_set;
      std::set_intersection(word_id_set.begin(), word_id_set.end(),
                            char_word_id_set.begin(), char_word_id_set.end(),
                            std::inserter(new_word_id_set,
                                          new_word_id_set.begin()));
      word_id_set.swap(new_word_id_set);
    }
  }
  return word_id_set;
}

// static
void URLIndexPrivateData::InitializeSchemeWhitelist(
    std::set<std::string>* whitelist) {
  DCHECK(whitelist);
  whitelist->insert(std::string(chrome::kAboutScheme));
  whitelist->insert(std::string(chrome::kChromeUIScheme));
  whitelist->insert(std::string(chrome::kFileScheme));
  whitelist->insert(std::string(chrome::kFtpScheme));
  whitelist->insert(std::string(chrome::kHttpScheme));
  whitelist->insert(std::string(chrome::kHttpsScheme));
  whitelist->insert(std::string(chrome::kMailToScheme));
}

// Cache Saving ----------------------------------------------------------------

bool URLIndexPrivateData::SaveToFile(const FilePath& file_path) {
  // TODO(mrossetti): Move File IO to another thread.
  base::ThreadRestrictions::ScopedAllowIO allow_io;
  base::TimeTicks beginning_time = base::TimeTicks::Now();
  InMemoryURLIndexCacheItem index_cache;
  SavePrivateData(&index_cache);
  std::string data;
  if (!index_cache.SerializeToString(&data)) {
    LOG(WARNING) << "Failed to serialize the InMemoryURLIndex cache.";
    return false;
  }

  int size = data.size();
  if (file_util::WriteFile(file_path, data.c_str(), size) != size) {
    LOG(WARNING) << "Failed to write " << file_path.value();
    return false;
  }
  UMA_HISTOGRAM_TIMES("History.InMemoryURLIndexSaveCacheTime",
                      base::TimeTicks::Now() - beginning_time);
  return true;
}

void URLIndexPrivateData::SavePrivateData(
    InMemoryURLIndexCacheItem* cache) const {
  DCHECK(cache);
  cache->set_timestamp(base::Time::Now().ToInternalValue());
  // history_item_count_ is no longer used but rather than change the protobuf
  // definition use a placeholder. This will go away with the switch to SQLite.
  cache->set_history_item_count(0);
  SaveWordList(cache);
  SaveWordMap(cache);
  SaveCharWordMap(cache);
  SaveWordIDHistoryMap(cache);
  SaveHistoryInfoMap(cache);
}

void URLIndexPrivateData::SaveWordList(InMemoryURLIndexCacheItem* cache) const {
  if (word_list_.empty())
    return;
  WordListItem* list_item = cache->mutable_word_list();
  list_item->set_word_count(word_list_.size());
  for (String16Vector::const_iterator iter = word_list_.begin();
       iter != word_list_.end(); ++iter)
    list_item->add_word(UTF16ToUTF8(*iter));
}

void URLIndexPrivateData::SaveWordMap(InMemoryURLIndexCacheItem* cache) const {
  if (word_map_.empty())
    return;
  WordMapItem* map_item = cache->mutable_word_map();
  map_item->set_item_count(word_map_.size());
  for (WordMap::const_iterator iter = word_map_.begin();
       iter != word_map_.end(); ++iter) {
    WordMapEntry* map_entry = map_item->add_word_map_entry();
    map_entry->set_word(UTF16ToUTF8(iter->first));
    map_entry->set_word_id(iter->second);
  }
}

void URLIndexPrivateData::SaveCharWordMap(
    InMemoryURLIndexCacheItem* cache) const {
  if (char_word_map_.empty())
    return;
  CharWordMapItem* map_item = cache->mutable_char_word_map();
  map_item->set_item_count(char_word_map_.size());
  for (CharWordIDMap::const_iterator iter = char_word_map_.begin();
       iter != char_word_map_.end(); ++iter) {
    CharWordMapEntry* map_entry = map_item->add_char_word_map_entry();
    map_entry->set_char_16(iter->first);
    const WordIDSet& word_id_set(iter->second);
    map_entry->set_item_count(word_id_set.size());
    for (WordIDSet::const_iterator set_iter = word_id_set.begin();
         set_iter != word_id_set.end(); ++set_iter)
      map_entry->add_word_id(*set_iter);
  }
}

void URLIndexPrivateData::SaveWordIDHistoryMap(
    InMemoryURLIndexCacheItem* cache) const {
  if (word_id_history_map_.empty())
    return;
  WordIDHistoryMapItem* map_item = cache->mutable_word_id_history_map();
  map_item->set_item_count(word_id_history_map_.size());
  for (WordIDHistoryMap::const_iterator iter = word_id_history_map_.begin();
       iter != word_id_history_map_.end(); ++iter) {
    WordIDHistoryMapEntry* map_entry =
        map_item->add_word_id_history_map_entry();
    map_entry->set_word_id(iter->first);
    const HistoryIDSet& history_id_set(iter->second);
    map_entry->set_item_count(history_id_set.size());
    for (HistoryIDSet::const_iterator set_iter = history_id_set.begin();
         set_iter != history_id_set.end(); ++set_iter)
      map_entry->add_history_id(*set_iter);
  }
}

void URLIndexPrivateData::SaveHistoryInfoMap(
    InMemoryURLIndexCacheItem* cache) const {
  if (history_info_map_.empty())
    return;
  HistoryInfoMapItem* map_item = cache->mutable_history_info_map();
  map_item->set_item_count(history_info_map_.size());
  for (HistoryInfoMap::const_iterator iter = history_info_map_.begin();
       iter != history_info_map_.end(); ++iter) {
    HistoryInfoMapEntry* map_entry = map_item->add_history_info_map_entry();
    map_entry->set_history_id(iter->first);
    const URLRow& url_row(iter->second);
    // Note: We only save information that contributes to the index so there
    // is no need to save search_term_cache_ (not persistent),
    // languages_, etc.
    map_entry->set_visit_count(url_row.visit_count());
    map_entry->set_typed_count(url_row.typed_count());
    map_entry->set_last_visit(url_row.last_visit().ToInternalValue());
    map_entry->set_url(url_row.url().spec());
    map_entry->set_title(UTF16ToUTF8(url_row.title()));
  }
}

// Cache Restoring -------------------------------------------------------------

bool URLIndexPrivateData::ReloadFromHistory(history::URLDatabase* history_db) {
  Clear();

  if (!history_db)
    return false;

  base::TimeTicks beginning_time = base::TimeTicks::Now();
  URLDatabase::URLEnumerator history_enum;
  if (!history_db->InitURLEnumeratorForSignificant(&history_enum))
    return false;
  URLRow row;
  while (history_enum.GetNextURL(&row))
    IndexRow(row);
  UMA_HISTOGRAM_TIMES("History.InMemoryURLIndexingTime",
                      base::TimeTicks::Now() - beginning_time);
  return true;
}

bool URLIndexPrivateData::RestoreFromFile(const FilePath& file_path) {
  // TODO(mrossetti): Figure out how to determine if the cache is up-to-date.
  // That is: ensure that the database has not been modified since the cache
  // was last saved. DB file modification date is inadequate. There are no
  // SQLite table checksums automatically stored.
  // FIXME(mrossetti): Move File IO to another thread.
  base::ThreadRestrictions::ScopedAllowIO allow_io;
  base::TimeTicks beginning_time = base::TimeTicks::Now();
  std::string data;
  // If there is no cache file then simply give up. This will cause us to
  // attempt to rebuild from the history database.
  if (!file_util::ReadFileToString(file_path, &data))
    return false;

  InMemoryURLIndexCacheItem index_cache;
  if (!index_cache.ParseFromArray(data.c_str(), data.size())) {
    LOG(WARNING) << "Failed to parse InMemoryURLIndex cache data read from "
                 << file_path.value();
    return false;
  }

  if (!RestorePrivateData(index_cache)) {
    Clear();  // Back to square one -- must build from scratch.
    return false;
  }

  UMA_HISTOGRAM_TIMES("History.InMemoryURLIndexRestoreCacheTime",
                      base::TimeTicks::Now() - beginning_time);
  UMA_HISTOGRAM_COUNTS("History.InMemoryURLHistoryItems",
                       history_id_word_map_.size());
  UMA_HISTOGRAM_COUNTS("History.InMemoryURLCacheSize", data.size());
  UMA_HISTOGRAM_COUNTS_10000("History.InMemoryURLWords", word_map_.size());
  UMA_HISTOGRAM_COUNTS_10000("History.InMemoryURLChars", char_word_map_.size());
  return true;
}

bool URLIndexPrivateData::RestorePrivateData(
    const InMemoryURLIndexCacheItem& cache) {
  return RestoreWordList(cache) && RestoreWordMap(cache) &&
      RestoreCharWordMap(cache) && RestoreWordIDHistoryMap(cache) &&
      RestoreHistoryInfoMap(cache);
}

bool URLIndexPrivateData::RestoreWordList(
    const InMemoryURLIndexCacheItem& cache) {
  if (!cache.has_word_list())
    return false;
  const WordListItem& list_item(cache.word_list());
  uint32 expected_item_count = list_item.word_count();
  uint32 actual_item_count = list_item.word_size();
  if (actual_item_count == 0 || actual_item_count != expected_item_count)
    return false;
  const RepeatedPtrField<std::string>& words(list_item.word());
  for (RepeatedPtrField<std::string>::const_iterator iter = words.begin();
       iter != words.end(); ++iter)
    word_list_.push_back(UTF8ToUTF16(*iter));
  return true;
}

bool URLIndexPrivateData::RestoreWordMap(
    const InMemoryURLIndexCacheItem& cache) {
  if (!cache.has_word_map())
    return false;
  const WordMapItem& list_item(cache.word_map());
  uint32 expected_item_count = list_item.item_count();
  uint32 actual_item_count = list_item.word_map_entry_size();
  if (actual_item_count == 0 || actual_item_count != expected_item_count)
    return false;
  const RepeatedPtrField<WordMapEntry>& entries(list_item.word_map_entry());
  for (RepeatedPtrField<WordMapEntry>::const_iterator iter = entries.begin();
       iter != entries.end(); ++iter)
    word_map_[UTF8ToUTF16(iter->word())] = iter->word_id();
  return true;
}

bool URLIndexPrivateData::RestoreCharWordMap(
    const InMemoryURLIndexCacheItem& cache) {
  if (!cache.has_char_word_map())
    return false;
  const CharWordMapItem& list_item(cache.char_word_map());
  uint32 expected_item_count = list_item.item_count();
  uint32 actual_item_count = list_item.char_word_map_entry_size();
  if (actual_item_count == 0 || actual_item_count != expected_item_count)
    return false;
  const RepeatedPtrField<CharWordMapEntry>&
      entries(list_item.char_word_map_entry());
  for (RepeatedPtrField<CharWordMapEntry>::const_iterator iter =
       entries.begin(); iter != entries.end(); ++iter) {
    expected_item_count = iter->item_count();
    actual_item_count = iter->word_id_size();
    if (actual_item_count == 0 || actual_item_count != expected_item_count)
      return false;
    char16 uni_char = static_cast<char16>(iter->char_16());
    WordIDSet word_id_set;
    const RepeatedField<int32>& word_ids(iter->word_id());
    for (RepeatedField<int32>::const_iterator jiter = word_ids.begin();
         jiter != word_ids.end(); ++jiter)
      word_id_set.insert(*jiter);
    char_word_map_[uni_char] = word_id_set;
  }
  return true;
}

bool URLIndexPrivateData::RestoreWordIDHistoryMap(
    const InMemoryURLIndexCacheItem& cache) {
  if (!cache.has_word_id_history_map())
    return false;
  const WordIDHistoryMapItem& list_item(cache.word_id_history_map());
  uint32 expected_item_count = list_item.item_count();
  uint32 actual_item_count = list_item.word_id_history_map_entry_size();
  if (actual_item_count == 0 || actual_item_count != expected_item_count)
    return false;
  const RepeatedPtrField<WordIDHistoryMapEntry>&
      entries(list_item.word_id_history_map_entry());
  for (RepeatedPtrField<WordIDHistoryMapEntry>::const_iterator iter =
       entries.begin(); iter != entries.end(); ++iter) {
    expected_item_count = iter->item_count();
    actual_item_count = iter->history_id_size();
    if (actual_item_count == 0 || actual_item_count != expected_item_count)
      return false;
    WordID word_id = iter->word_id();
    HistoryIDSet history_id_set;
    const RepeatedField<int64>& history_ids(iter->history_id());
    for (RepeatedField<int64>::const_iterator jiter = history_ids.begin();
         jiter != history_ids.end(); ++jiter) {
      history_id_set.insert(*jiter);
      AddToHistoryIDWordMap(*jiter, word_id);
    }
    word_id_history_map_[word_id] = history_id_set;
  }
  return true;
}

bool URLIndexPrivateData::RestoreHistoryInfoMap(
    const InMemoryURLIndexCacheItem& cache) {
  if (!cache.has_history_info_map())
    return false;
  const HistoryInfoMapItem& list_item(cache.history_info_map());
  uint32 expected_item_count = list_item.item_count();
  uint32 actual_item_count = list_item.history_info_map_entry_size();
  if (actual_item_count == 0 || actual_item_count != expected_item_count)
    return false;
  const RepeatedPtrField<HistoryInfoMapEntry>&
      entries(list_item.history_info_map_entry());
  for (RepeatedPtrField<HistoryInfoMapEntry>::const_iterator iter =
       entries.begin(); iter != entries.end(); ++iter) {
    HistoryID history_id = iter->history_id();
    GURL url(iter->url());
    URLRow url_row(url, history_id);
    url_row.set_visit_count(iter->visit_count());
    url_row.set_typed_count(iter->typed_count());
    url_row.set_last_visit(base::Time::FromInternalValue(iter->last_visit()));
    if (iter->has_title()) {
      string16 title(UTF8ToUTF16(iter->title()));
      url_row.set_title(title);
    }
    history_info_map_[history_id] = url_row;
  }
  return true;
}

}  // namespace history
