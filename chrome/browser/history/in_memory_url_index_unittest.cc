// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>

#include "base/file_path.h"
#include "base/file_util.h"
#include "base/path_service.h"
#include "base/string16.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/autocomplete/autocomplete.h"
#include "chrome/browser/history/in_memory_database.h"
#include "chrome/browser/history/in_memory_url_index.h"
#include "chrome/browser/history/in_memory_url_index_types.h"
#include "chrome/common/chrome_paths.h"
#include "sql/transaction.h"
#include "testing/gtest/include/gtest/gtest.h"

// The test version of the history url database table ('url') is contained in
// a database file created from a text file('url_history_provider_test.db.txt').
// The only difference between this table and a live 'urls' table from a
// profile is that the last_visit_time column in the test table contains a
// number specifying the number of days relative to 'today' to which the
// absolute time should be set during the test setup stage.
//
// The format of the test database text file is of a SQLite .dump file.
// Note that only lines whose first character is an upper-case letter are
// processed when creating the test database.

namespace history {

class InMemoryURLIndexTest : public testing::Test,
                             public InMemoryDatabase {
 public:
  InMemoryURLIndexTest() { InitFromScratch(); }

 protected:
  // Test setup.
  virtual void SetUp();

  // Allows the database containing the test data to be customized by
  // subclasses.
  virtual FilePath::StringType TestDBName() const;

  // Convenience function to create a URLRow with basic data for |url|, |title|,
  // |visit_count|, and |typed_count|. |last_visit_ago| gives the number of
  // days from now to set the URL's last_visit.
  URLRow MakeURLRow(const char* url,
                    const char* title,
                    int visit_count,
                    int last_visit_ago,
                    int typed_count);

  // Convenience functions for easily creating vectors of search terms.
  String16Vector Make1Term(const char* term) const;
  String16Vector Make2Terms(const char* term_1, const char* term_2) const;

  // Validates that the given |term| is contained in |cache| and that it is
  // marked as in-use.
  void CheckTerm(const URLIndexPrivateData::SearchTermCacheMap& cache,
                 string16 term) const;

  scoped_ptr<InMemoryURLIndex> url_index_;
};

void InMemoryURLIndexTest::SetUp() {
  // Create and populate a working copy of the URL history database.
  FilePath history_proto_path;
  PathService::Get(chrome::DIR_TEST_DATA, &history_proto_path);
  history_proto_path = history_proto_path.Append(
      FILE_PATH_LITERAL("History"));
  history_proto_path = history_proto_path.Append(TestDBName());
  EXPECT_TRUE(file_util::PathExists(history_proto_path));

  std::ifstream proto_file(history_proto_path.value().c_str());
  static const size_t kCommandBufferMaxSize = 2048;
  char sql_cmd_line[kCommandBufferMaxSize];

  sql::Connection& db(GetDB());
  {
    sql::Transaction transaction(&db);
    transaction.Begin();
    while (!proto_file.eof()) {
      proto_file.getline(sql_cmd_line, kCommandBufferMaxSize);
      if (!proto_file.eof()) {
        // We only process lines which begin with a upper-case letter.
        // TODO(mrossetti): Can iswupper() be used here?
        if (sql_cmd_line[0] >= 'A' && sql_cmd_line[0] <= 'Z') {
          std::string sql_cmd(sql_cmd_line);
          sql::Statement sql_stmt(db.GetUniqueStatement(sql_cmd_line));
          EXPECT_TRUE(sql_stmt.Run());
        }
      }
    }
    transaction.Commit();
  }
  proto_file.close();

  // Update the last_visit_time table column
  // such that it represents a time relative to 'now'.
  sql::Statement statement(db.GetUniqueStatement(
      "SELECT" HISTORY_URL_ROW_FIELDS "FROM urls;"));
  ASSERT_TRUE(statement.is_valid());
  base::Time time_right_now = base::Time::NowFromSystemTime();
  base::TimeDelta day_delta = base::TimeDelta::FromDays(1);
  {
    sql::Transaction transaction(&db);
    transaction.Begin();
    while (statement.Step()) {
      URLRow row;
      FillURLRow(statement, &row);
      base::Time last_visit = time_right_now;
      for (int64 i = row.last_visit().ToInternalValue(); i > 0; --i)
        last_visit -= day_delta;
      row.set_last_visit(last_visit);
      UpdateURLRow(row.id(), row);
    }
    transaction.Commit();
  }
}

FilePath::StringType InMemoryURLIndexTest::TestDBName() const {
    return FILE_PATH_LITERAL("url_history_provider_test.db.txt");
}

URLRow InMemoryURLIndexTest::MakeURLRow(const char* url,
                  const char* title,
                  int visit_count,
                  int last_visit_ago,
                  int typed_count) {
  URLRow row(GURL(url), 0);
  row.set_title(UTF8ToUTF16(title));
  row.set_visit_count(visit_count);
  row.set_typed_count(typed_count);
  row.set_last_visit(base::Time::NowFromSystemTime() -
                     base::TimeDelta::FromDays(last_visit_ago));
  return row;
}

String16Vector InMemoryURLIndexTest::Make1Term(const char* term) const {
  String16Vector original_terms;
  original_terms.push_back(UTF8ToUTF16(term));
  return original_terms;
}

String16Vector InMemoryURLIndexTest::Make2Terms(const char* term_1,
                                                const char* term_2) const {
  String16Vector original_terms;
  original_terms.push_back(UTF8ToUTF16(term_1));
  original_terms.push_back(UTF8ToUTF16(term_2));
  return original_terms;
}

void InMemoryURLIndexTest::CheckTerm(
    const URLIndexPrivateData::SearchTermCacheMap& cache,
    string16 term) const {
  URLIndexPrivateData::SearchTermCacheMap::const_iterator cache_iter(
      cache.find(term));
  ASSERT_NE(cache.end(), cache_iter)
      << "Cache does not contain '" << term << "' but should.";
  URLIndexPrivateData::SearchTermCacheItem cache_item = cache_iter->second;
  EXPECT_TRUE(cache_item.used_)
      << "Cache item '" << term << "' should be marked as being in use.";
}

// Helper function which compares two maps for equivalence. The maps' values
// are associative containers and their contents are compared as well.
template<typename T>
void ExpectMapOfContainersIdentical(const T& expected, const T& actual) {
  ASSERT_EQ(expected.size(), actual.size());
  for (typename T::const_iterator expected_iter = expected.begin();
       expected_iter != expected.end(); ++expected_iter) {
    typename T::const_iterator actual_iter = actual.find(expected_iter->first);
    ASSERT_NE(actual.end(), actual_iter);
    typename T::mapped_type const& expected_values(expected_iter->second);
    typename T::mapped_type const& actual_values(actual_iter->second);
    ASSERT_EQ(expected_values.size(), actual_values.size());
    for (typename T::mapped_type::const_iterator set_iter =
         expected_values.begin(); set_iter != expected_values.end(); ++set_iter)
      EXPECT_EQ(actual_values.count(*set_iter),
                expected_values.count(*set_iter));
  }
}

//------------------------------------------------------------------------------

class LimitedInMemoryURLIndexTest : public InMemoryURLIndexTest {
 protected:
  FilePath::StringType TestDBName() const;
};

FilePath::StringType LimitedInMemoryURLIndexTest::TestDBName() const {
  return FILE_PATH_LITERAL("url_history_provider_test_limited.db.txt");
}

TEST_F(InMemoryURLIndexTest, Construction) {
  url_index_.reset(new InMemoryURLIndex(FilePath()));
  EXPECT_TRUE(url_index_.get());
}

TEST_F(LimitedInMemoryURLIndexTest, Initialization) {
  // Verify that the database contains the expected number of items, which
  // is the pre-filtered count, i.e. all of the items.
  sql::Statement statement(GetDB().GetUniqueStatement("SELECT * FROM urls;"));
  ASSERT_TRUE(statement.is_valid());
  uint64 row_count = 0;
  while (statement.Step()) ++row_count;
  EXPECT_EQ(1U, row_count);
  url_index_.reset(new InMemoryURLIndex(FilePath()));
  url_index_->Init(this, "en,ja,hi,zh");
  URLIndexPrivateData& private_data(*(url_index_->private_data_));

  // history_info_map_ should have the same number of items as were filtered.
  EXPECT_EQ(1U, private_data.history_info_map_.size());
  EXPECT_EQ(35U, private_data.char_word_map_.size());
  EXPECT_EQ(17U, private_data.word_map_.size());
}

TEST_F(InMemoryURLIndexTest, Retrieval) {
  url_index_.reset(new InMemoryURLIndex(FilePath()));
  url_index_->Init(this, "en,ja,hi,zh");
  // The term will be lowercased by the search.

  // See if a very specific term gives a single result.
  ScoredHistoryMatches matches =
      url_index_->HistoryItemsForTerms(ASCIIToUTF16("DrudgeReport"));
  ASSERT_EQ(1U, matches.size());

  // Verify that we got back the result we expected.
  EXPECT_EQ(5, matches[0].url_info.id());
  EXPECT_EQ("http://drudgereport.com/", matches[0].url_info.url().spec());
  EXPECT_EQ(ASCIIToUTF16("DRUDGE REPORT 2010"), matches[0].url_info.title());
  EXPECT_TRUE(matches[0].can_inline);

  // Make sure a trailing space prevents inline-ability but still results
  // in the expected result.
  matches = url_index_->HistoryItemsForTerms(ASCIIToUTF16("DrudgeReport "));
  ASSERT_EQ(1U, matches.size());
  EXPECT_EQ(5, matches[0].url_info.id());
  EXPECT_EQ("http://drudgereport.com/", matches[0].url_info.url().spec());
  EXPECT_EQ(ASCIIToUTF16("DRUDGE REPORT 2010"), matches[0].url_info.title());
  EXPECT_FALSE(matches[0].can_inline);

  // Search which should result in multiple results.
  matches = url_index_->HistoryItemsForTerms(ASCIIToUTF16("drudge"));
  ASSERT_EQ(2U, matches.size());
  // The results should be in descending score order.
  EXPECT_GE(matches[0].raw_score, matches[1].raw_score);

  // Search which should result in nearly perfect result.
  matches = url_index_->HistoryItemsForTerms(
      ASCIIToUTF16("https NearlyPerfectResult"));
  ASSERT_EQ(1U, matches.size());
  // The results should have a very high score.
  EXPECT_GT(matches[0].raw_score, 900);
  EXPECT_EQ(32, matches[0].url_info.id());
  EXPECT_EQ("https://nearlyperfectresult.com/",
            matches[0].url_info.url().spec());  // Note: URL gets lowercased.
  EXPECT_EQ(ASCIIToUTF16("Practically Perfect Search Result"),
            matches[0].url_info.title());
  EXPECT_FALSE(matches[0].can_inline);

  // Search which should result in very poor result.
  matches = url_index_->HistoryItemsForTerms(ASCIIToUTF16("z y x"));
  ASSERT_EQ(1U, matches.size());
  // The results should have a poor score.
  EXPECT_LT(matches[0].raw_score, 500);
  EXPECT_EQ(33, matches[0].url_info.id());
  EXPECT_EQ("http://quiteuselesssearchresultxyz.com/",
            matches[0].url_info.url().spec());  // Note: URL gets lowercased.
  EXPECT_EQ(ASCIIToUTF16("Practically Useless Search Result"),
            matches[0].url_info.title());
  EXPECT_FALSE(matches[0].can_inline);

  // Search which will match at the end of an URL with encoded characters.
  matches = url_index_->HistoryItemsForTerms(ASCIIToUTF16("Mice"));
  ASSERT_EQ(1U, matches.size());
  EXPECT_EQ(30, matches[0].url_info.id());
  EXPECT_FALSE(matches[0].can_inline);

  // Verify that a single term can appear multiple times in the URL and as long
  // as one starts the URL it is still inlined.
  matches = url_index_->HistoryItemsForTerms(ASCIIToUTF16("fubar"));
  ASSERT_EQ(1U, matches.size());
  EXPECT_EQ(34, matches[0].url_info.id());
  EXPECT_EQ("http://fubarfubarandfubar.com/", matches[0].url_info.url().spec());
  EXPECT_EQ(ASCIIToUTF16("Situation Normal -- FUBARED"),
            matches[0].url_info.title());
  EXPECT_TRUE(matches[0].can_inline);
}

TEST_F(InMemoryURLIndexTest, URLPrefixMatching) {
  url_index_.reset(new InMemoryURLIndex(FilePath()));
  url_index_->Init(this, "en,ja,hi,zh");

  // "drudgere" - found, can inline
  ScoredHistoryMatches matches =
      url_index_->HistoryItemsForTerms(ASCIIToUTF16("drudgere"));
  ASSERT_EQ(1U, matches.size());
  EXPECT_TRUE(matches[0].can_inline);

  // "http://drudgere" - found, can inline
  matches = url_index_->HistoryItemsForTerms(ASCIIToUTF16("http://drudgere"));
  ASSERT_EQ(1U, matches.size());
  EXPECT_TRUE(matches[0].can_inline);

  // "www.atdmt" - not found
  matches = url_index_->HistoryItemsForTerms(ASCIIToUTF16("www.atdmt"));
  EXPECT_EQ(0U, matches.size());

  // "atdmt" - found, cannot inline
  matches = url_index_->HistoryItemsForTerms(ASCIIToUTF16("atdmt"));
  ASSERT_EQ(1U, matches.size());
  EXPECT_FALSE(matches[0].can_inline);

  // "view.atdmt" - found, can inline
  matches = url_index_->HistoryItemsForTerms(ASCIIToUTF16("view.atdmt"));
  ASSERT_EQ(1U, matches.size());
  EXPECT_TRUE(matches[0].can_inline);

  // "http://view.atdmt" - found, can inline
  matches = url_index_->HistoryItemsForTerms(ASCIIToUTF16("http://view.atdmt"));
  ASSERT_EQ(1U, matches.size());
  EXPECT_TRUE(matches[0].can_inline);

  // "cnn.com" - found, can inline
  matches = url_index_->HistoryItemsForTerms(ASCIIToUTF16("cnn.com"));
  ASSERT_EQ(2U, matches.size());
  // One match should be inline-able, the other not.
  EXPECT_TRUE(matches[0].can_inline != matches[1].can_inline);

  // "www.cnn.com" - found, can inline
  matches = url_index_->HistoryItemsForTerms(ASCIIToUTF16("www.cnn.com"));
  ASSERT_EQ(1U, matches.size());
  EXPECT_TRUE(matches[0].can_inline);

  // "www.cnn.com" - found, cannot inline
  matches = url_index_->HistoryItemsForTerms(ASCIIToUTF16("ww.cnn.com"));
  ASSERT_EQ(1U, matches.size());
  EXPECT_TRUE(!matches[0].can_inline);

  // "http://www.cnn.com" - found, can inline
  matches =
      url_index_->HistoryItemsForTerms(ASCIIToUTF16("http://www.cnn.com"));
  ASSERT_EQ(1U, matches.size());
  EXPECT_TRUE(matches[0].can_inline);

  // "tp://www.cnn.com" - found, cannot inline
  matches =
      url_index_->HistoryItemsForTerms(ASCIIToUTF16("tp://www.cnn.com"));
  ASSERT_EQ(1U, matches.size());
  EXPECT_TRUE(!matches[0].can_inline);
}

TEST_F(InMemoryURLIndexTest, ProperStringMatching) {
  url_index_.reset(new InMemoryURLIndex(FilePath()));
  url_index_->Init(this, "en,ja,hi,zh");

  // Search for the following with the expected results:
  // "atdmt view" - found
  // "atdmt.view" - not found
  // "view.atdmt" - found
  ScoredHistoryMatches matches =
      url_index_->HistoryItemsForTerms(ASCIIToUTF16("atdmt view"));
  ASSERT_EQ(1U, matches.size());
  matches = url_index_->HistoryItemsForTerms(ASCIIToUTF16("atdmt.view"));
  ASSERT_EQ(0U, matches.size());
  matches = url_index_->HistoryItemsForTerms(ASCIIToUTF16("view.atdmt"));
  ASSERT_EQ(1U, matches.size());
}

TEST_F(InMemoryURLIndexTest, HugeResultSet) {
  url_index_.reset(new InMemoryURLIndex(FilePath()));
  url_index_->Init(this, "en,ja,hi,zh");

  // Create a huge set of qualifying history items.
  for (URLID row_id = 5000; row_id < 6000; ++row_id) {
    URLRow new_row(GURL("http://www.brokeandaloneinmanitoba.com/"), row_id);
    new_row.set_last_visit(base::Time::Now());
    url_index_->UpdateURL(row_id, new_row);
  }

  ScoredHistoryMatches matches =
      url_index_->HistoryItemsForTerms(ASCIIToUTF16("b"));
  ASSERT_EQ(AutocompleteProvider::kMaxMatches, matches.size());
  // There are 7 matches already in the database.
  URLIndexPrivateData& private_data(*(url_index_->private_data_.get()));
  ASSERT_EQ(1008U, private_data.pre_filter_item_count_);
  ASSERT_EQ(500U, private_data.post_filter_item_count_);
  ASSERT_EQ(AutocompleteProvider::kMaxMatches,
            private_data.post_scoring_item_count_);
}

TEST_F(InMemoryURLIndexTest, TitleSearch) {
  url_index_.reset(new InMemoryURLIndex(FilePath()));
  url_index_->Init(this, "en,ja,hi,zh");
  // Signal if someone has changed the test DB.
  EXPECT_EQ(28U, url_index_->private_data_->history_info_map_.size());

  // Ensure title is being searched.
  ScoredHistoryMatches matches =
      url_index_->HistoryItemsForTerms(ASCIIToUTF16("MORTGAGE RATE DROPS"));
  ASSERT_EQ(1U, matches.size());

  // Verify that we got back the result we expected.
  EXPECT_EQ(1, matches[0].url_info.id());
  EXPECT_EQ("http://www.reuters.com/article/idUSN0839880620100708",
            matches[0].url_info.url().spec());
  EXPECT_EQ(ASCIIToUTF16(
      "UPDATE 1-US 30-yr mortgage rate drops to new record low | Reuters"),
      matches[0].url_info.title());
}

TEST_F(InMemoryURLIndexTest, TitleChange) {
  url_index_.reset(new InMemoryURLIndex(FilePath()));
  url_index_->Init(this, "en,ja,hi,zh");

  // Verify current title terms retrieves desired item.
  string16 original_terms =
      ASCIIToUTF16("lebronomics could high taxes influence");
  ScoredHistoryMatches matches =
      url_index_->HistoryItemsForTerms(original_terms);
  ASSERT_EQ(1U, matches.size());

  // Verify that we got back the result we expected.
  const URLID expected_id = 3;
  EXPECT_EQ(expected_id, matches[0].url_info.id());
  EXPECT_EQ("http://www.businessandmedia.org/articles/2010/20100708120415.aspx",
            matches[0].url_info.url().spec());
  EXPECT_EQ(ASCIIToUTF16(
      "LeBronomics: Could High Taxes Influence James' Team Decision?"),
      matches[0].url_info.title());
  URLRow old_row(matches[0].url_info);

  // Verify new title terms retrieves nothing.
  string16 new_terms = ASCIIToUTF16("does eat oats little lambs ivy");
  matches = url_index_->HistoryItemsForTerms(new_terms);
  ASSERT_EQ(0U, matches.size());

  // Update the row.
  old_row.set_title(ASCIIToUTF16("Does eat oats and little lambs eat ivy"));
  url_index_->UpdateURL(expected_id, old_row);

  // Verify we get the row using the new terms but not the original terms.
  matches = url_index_->HistoryItemsForTerms(new_terms);
  ASSERT_EQ(1U, matches.size());
  EXPECT_EQ(expected_id, matches[0].url_info.id());
  matches = url_index_->HistoryItemsForTerms(original_terms);
  ASSERT_EQ(0U, matches.size());
}

TEST_F(InMemoryURLIndexTest, NonUniqueTermCharacterSets) {
  url_index_.reset(new InMemoryURLIndex(FilePath()));
  url_index_->Init(this, "en,ja,hi,zh");

  // The presence of duplicate characters should succeed. Exercise by cycling
  // through a string with several duplicate characters.
  ScoredHistoryMatches matches =
      url_index_->HistoryItemsForTerms(ASCIIToUTF16("ABRA"));
  ASSERT_EQ(1U, matches.size());
  EXPECT_EQ(28, matches[0].url_info.id());
  EXPECT_EQ("http://www.ddj.com/windows/184416623",
            matches[0].url_info.url().spec());

  matches = url_index_->HistoryItemsForTerms(ASCIIToUTF16("ABRACAD"));
  ASSERT_EQ(1U, matches.size());
  EXPECT_EQ(28, matches[0].url_info.id());

  matches = url_index_->HistoryItemsForTerms(ASCIIToUTF16("ABRACADABRA"));
  ASSERT_EQ(1U, matches.size());
  EXPECT_EQ(28, matches[0].url_info.id());

  matches = url_index_->HistoryItemsForTerms(ASCIIToUTF16("ABRACADABR"));
  ASSERT_EQ(1U, matches.size());
  EXPECT_EQ(28, matches[0].url_info.id());

  matches = url_index_->HistoryItemsForTerms(ASCIIToUTF16("ABRACA"));
  ASSERT_EQ(1U, matches.size());
  EXPECT_EQ(28, matches[0].url_info.id());
}

TEST_F(InMemoryURLIndexTest, TypedCharacterCaching) {
  // Verify that match results for previously typed characters are retained
  // (in the term_char_word_set_cache_) and reused, if possible, in future
  // autocompletes.
  typedef URLIndexPrivateData::SearchTermCacheMap::iterator CacheIter;
  typedef URLIndexPrivateData::SearchTermCacheItem CacheItem;

  url_index_.reset(new InMemoryURLIndex(FilePath()));
  url_index_->Init(this, "en,ja,hi,zh");

  URLIndexPrivateData::SearchTermCacheMap& cache(
      url_index_->private_data_->search_term_cache_);

  // The cache should be empty at this point.
  EXPECT_EQ(0U, cache.size());

  // Now simulate typing search terms into the omnibox and check the state of
  // the cache as each item is 'typed'.

  // Simulate typing "r" giving "r" in the simulated omnibox. The results for
  // 'r' will be not cached because it is only 1 character long.
  url_index_->HistoryItemsForTerms(ASCIIToUTF16("r"));
  EXPECT_EQ(0U, cache.size());

  // Simulate typing "re" giving "r re" in the simulated omnibox.
  // 're' should be cached at this point but not 'r' as it is a single
  // character.
  url_index_->HistoryItemsForTerms(ASCIIToUTF16("r re"));
  ASSERT_EQ(1U, cache.size());
  CheckTerm(cache, ASCIIToUTF16("re"));

  // Simulate typing "reco" giving "r re reco" in the simulated omnibox.
  // 're' and 'reco' should be cached at this point but not 'r' as it is a
  // single character.
  url_index_->HistoryItemsForTerms(ASCIIToUTF16("r re reco"));
  ASSERT_EQ(2U, cache.size());
  CheckTerm(cache, ASCIIToUTF16("re"));
  CheckTerm(cache, ASCIIToUTF16("reco"));

  // Simulate typing "mort".
  // Since we now have only one search term, the cached results for 're' and
  // 'reco' should be purged, giving us only 1 item in the cache (for 'mort').
  url_index_->HistoryItemsForTerms(ASCIIToUTF16("mort"));
  ASSERT_EQ(1U, cache.size());
  CheckTerm(cache, ASCIIToUTF16("mort"));

  // Simulate typing "reco" giving "mort reco" in the simulated omnibox.
  url_index_->HistoryItemsForTerms(ASCIIToUTF16("mort reco"));
  ASSERT_EQ(2U, cache.size());
  CheckTerm(cache, ASCIIToUTF16("mort"));
  CheckTerm(cache, ASCIIToUTF16("reco"));

  // Simulate a <DELETE> by removing the 'reco' and adding back the 'rec'.
  url_index_->HistoryItemsForTerms(ASCIIToUTF16("mort rec"));
  ASSERT_EQ(2U, cache.size());
  CheckTerm(cache, ASCIIToUTF16("mort"));
  CheckTerm(cache, ASCIIToUTF16("rec"));
}

TEST_F(InMemoryURLIndexTest, Scoring) {
  URLRow row_a(MakeURLRow("http://abcdef", "fedcba", 3, 30, 1));
  // Test scores based on position.
  ScoredHistoryMatch scored_a(URLIndexPrivateData::ScoredMatchForURL(
      row_a, ASCIIToUTF16("abc"), Make1Term("abc")));
  ScoredHistoryMatch scored_b(URLIndexPrivateData::ScoredMatchForURL(
      row_a, ASCIIToUTF16("bcd"), Make1Term("bcd")));
  EXPECT_GT(scored_a.raw_score, scored_b.raw_score);
  // Test scores based on length.
  ScoredHistoryMatch scored_c(URLIndexPrivateData::ScoredMatchForURL(
      row_a, ASCIIToUTF16("abcd"), Make1Term("abcd")));
  EXPECT_LT(scored_a.raw_score, scored_c.raw_score);
  // Test scores based on order.
  ScoredHistoryMatch scored_d(URLIndexPrivateData::ScoredMatchForURL(
      row_a, ASCIIToUTF16("abcdef"), Make2Terms("abc", "def")));
  ScoredHistoryMatch scored_e(URLIndexPrivateData::ScoredMatchForURL(
      row_a, ASCIIToUTF16("def abc"), Make2Terms("def", "abc")));
  EXPECT_GT(scored_d.raw_score, scored_e.raw_score);
  // Test scores based on visit_count.
  URLRow row_b(MakeURLRow("http://abcdef", "fedcba", 10, 30, 1));
  ScoredHistoryMatch scored_f(URLIndexPrivateData::ScoredMatchForURL(
      row_b, ASCIIToUTF16("abc"), Make1Term("abc")));
  EXPECT_GT(scored_f.raw_score, scored_a.raw_score);
  // Test scores based on last_visit.
  URLRow row_c(MakeURLRow("http://abcdef", "fedcba", 3, 10, 1));
  ScoredHistoryMatch scored_g(URLIndexPrivateData::ScoredMatchForURL(
      row_c, ASCIIToUTF16("abc"), Make1Term("abc")));
  EXPECT_GT(scored_g.raw_score, scored_a.raw_score);
  // Test scores based on typed_count.
  URLRow row_d(MakeURLRow("http://abcdef", "fedcba", 3, 30, 10));
  ScoredHistoryMatch scored_h(URLIndexPrivateData::ScoredMatchForURL(
      row_d, ASCIIToUTF16("abc"), Make1Term("abc")));
  EXPECT_GT(scored_h.raw_score, scored_a.raw_score);
  // Test scores based on a terms appearing multiple times.
  URLRow row_i(MakeURLRow("http://csi.csi.csi/csi_csi",
      "CSI Guide to CSI Las Vegas, CSI New York, CSI Provo", 3, 30, 10));
  ScoredHistoryMatch scored_i(URLIndexPrivateData::ScoredMatchForURL(
      row_i, ASCIIToUTF16("csi"), Make1Term("csi")));
  EXPECT_LT(scored_i.raw_score, 1400);
}

TEST_F(InMemoryURLIndexTest, AddNewRows) {
  url_index_.reset(new InMemoryURLIndex(FilePath()));
  url_index_->Init(this, "en,ja,hi,zh");

  // Verify that the row we're going to add does not already exist.
  URLID new_row_id = 87654321;
  // Newly created URLRows get a last_visit time of 'right now' so it should
  // qualify as a quick result candidate.
  EXPECT_TRUE(url_index_->HistoryItemsForTerms(
      ASCIIToUTF16("brokeandalone")).empty());

  // Add a new row.
  URLRow new_row(GURL("http://www.brokeandaloneinmanitoba.com/"), new_row_id);
  new_row.set_last_visit(base::Time::Now());
  url_index_->UpdateURL(new_row_id, new_row);

  // Verify that we can retrieve it.
  EXPECT_EQ(1U, url_index_->HistoryItemsForTerms(
      ASCIIToUTF16("brokeandalone")).size());

  // Add it again just to be sure that is harmless.
  url_index_->UpdateURL(new_row_id, new_row);
  EXPECT_EQ(1U, url_index_->HistoryItemsForTerms(
      ASCIIToUTF16("brokeandalone")).size());
}

TEST_F(InMemoryURLIndexTest, DeleteRows) {
  url_index_.reset(new InMemoryURLIndex(FilePath()));
  url_index_->Init(this, "en,ja,hi,zh");

  ScoredHistoryMatches matches =
      url_index_->HistoryItemsForTerms(ASCIIToUTF16("DrudgeReport"));
  ASSERT_EQ(1U, matches.size());

  // Determine the row id for that result, delete that id, then search again.
  url_index_->DeleteURL(matches[0].url_info.id());
  EXPECT_TRUE(url_index_->HistoryItemsForTerms(
      ASCIIToUTF16("DrudgeReport")).empty());
}

TEST_F(InMemoryURLIndexTest, WhitelistedURLs) {
  struct TestData {
    const std::string url_spec;
    const bool expected_is_whitelisted;
  } data[] = {
    // URLs with whitelisted schemes.
    { "about:histograms", true },
    { "chrome://settings", true },
    { "file://localhost/Users/joeschmoe/sekrets", true },
    { "ftp://public.mycompany.com/myfile.txt", true },
    { "http://www.google.com/translate", true },
    { "https://www.gmail.com/", true },
    { "mailto:support@google.com", true },
    // URLs with unacceptable schemes.
    { "aaa://www.dummyhost.com;frammy", false },
    { "aaas://www.dummyhost.com;frammy", false },
    { "acap://suzie@somebody.com", false },
    { "cap://cal.example.com/Company/Holidays", false },
    { "cid:foo4*foo1@bar.net", false },
    { "crid://example.com/foobar", false },
    { "data:image/png;base64,iVBORw0KGgoAAAANSUhE=", false },
    { "dict://dict.org/d:shortcake:", false },
    { "dns://192.168.1.1/ftp.example.org?type=A", false },
    { "fax:+358.555.1234567", false },
    { "geo:13.4125,103.8667", false },
    { "go:Mercedes%20Benz", false },
    { "gopher://farnsworth.ca:666/gopher", false },
    { "h323:farmer-john;sixpence", false },
    { "iax:johnQ@example.com/12022561414", false },
    { "icap://icap.net/service?mode=translate&lang=french", false },
    { "im:fred@example.com", false },
    { "imap://michael@minbari.org/users.*", false },
    { "info:ddc/22/eng//004.678", false },
    { "ipp://example.com/printer/fox", false },
    { "iris:dreg1//example.com/local/myhosts", false },
    { "iris.beep:dreg1//example.com/local/myhosts", false },
    { "iris.lws:dreg1//example.com/local/myhosts", false },
    { "iris.xpc:dreg1//example.com/local/myhosts", false },
    { "iris.xpcs:dreg1//example.com/local/myhosts", false },
    { "ldap://ldap.itd.umich.edu/o=University%20of%20Michigan,c=US", false },
    { "mid:foo4%25foo1@bar.net", false },
    { "modem:+3585551234567;type=v32b?7e1;type=v110", false },
    { "msrp://atlanta.example.com:7654/jshA7weztas;tcp", false },
    { "msrps://atlanta.example.com:7654/jshA7weztas;tcp", false },
    { "news:colorectal.info.banned", false },
    { "nfs://server/d/e/f", false },
    { "nntp://www.example.com:6543/info.comp.lies/1234", false },
    { "pop://rg;AUTH=+APOP@mail.mycompany.com:8110", false },
    { "pres:fred@example.com", false },
    { "prospero://host.dom//pros/name", false },
    { "rsync://syler@lost.com/Source", false },
    { "rtsp://media.example.com:554/twister/audiotrack", false },
    { "service:acap://some.where.net;authentication=KERBEROSV4", false },
    { "shttp://www.terces.com/secret", false },
    { "sieve://example.com//script", false },
    { "sip:+1-212-555-1212:1234@gateway.com;user=phone", false },
    { "sips:+1-212-555-1212:1234@gateway.com;user=phone", false },
    { "sms:+15105551212?body=hello%20there", false },
    { "snmp://tester5@example.com:8161/bridge1;800002b804616263", false },
    { "soap.beep://stockquoteserver.example.com/StockQuote", false },
    { "soap.beeps://stockquoteserver.example.com/StockQuote", false },
    { "tag:blogger.com,1999:blog-555", false },
    { "tel:+358-555-1234567;postd=pp22", false },
    { "telnet://mayor_margie:one2rule4All@www.mycity.com:6789/", false },
    { "tftp://example.com/mystartupfile", false },
    { "tip://123.123.123.123/?urn:xopen:xid", false },
    { "tv:nbc.com", false },
    { "urn:foo:A123,456", false },
    { "vemmi://zeus.mctel.fr/demo", false },
    { "wais://www.mydomain.net:8765/mydatabase", false },
    { "xmpp:node@example.com", false },
    { "xmpp://guest@example.com", false },
  };
  url_index_.reset(new InMemoryURLIndex(FilePath()));
  URLIndexPrivateData& private_data(*(url_index_->private_data_.get()));
  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(data); ++i) {
    GURL url(data[i].url_spec);
    EXPECT_EQ(data[i].expected_is_whitelisted,
              private_data.URLSchemeIsWhitelisted(url));
  }
}

TEST_F(InMemoryURLIndexTest, CacheFilePath) {
  url_index_.reset(new InMemoryURLIndex(FilePath(FILE_PATH_LITERAL(
      "/flammmy/frammy/"))));
  FilePath full_file_path;
  url_index_->GetCacheFilePath(&full_file_path);
  std::vector<FilePath::StringType> expected_parts;
  FilePath(FILE_PATH_LITERAL("/flammmy/frammy/History Provider Cache")).
      GetComponents(&expected_parts);
  std::vector<FilePath::StringType> actual_parts;
  full_file_path.GetComponents(&actual_parts);
  ASSERT_EQ(expected_parts.size(), actual_parts.size());
  size_t count = expected_parts.size();
  for (size_t i = 0; i < count; ++i)
    EXPECT_EQ(expected_parts[i], actual_parts[i]);
  // Must clear the history_dir_ to satisfy the dtor's DCHECK.
  url_index_->history_dir_.clear();
}

TEST_F(InMemoryURLIndexTest, CacheSaveRestore) {
  // Save the cache to a protobuf, restore it, and compare the results.
  url_index_.reset(new InMemoryURLIndex(FilePath()));
  InMemoryURLIndex& url_index(*(url_index_.get()));
  url_index.Init(this, "en,ja,hi,zh");
  in_memory_url_index::InMemoryURLIndexCacheItem index_cache;
  URLIndexPrivateData& private_data(*(url_index_->private_data_.get()));
  private_data.SavePrivateData(&index_cache);

  // Capture our private data so we can later compare for equality.
  String16Vector word_list(private_data.word_list_);
  WordMap word_map(private_data.word_map_);
  CharWordIDMap char_word_map(private_data.char_word_map_);
  WordIDHistoryMap word_id_history_map(private_data.word_id_history_map_);
  HistoryIDWordMap history_id_word_map(private_data.history_id_word_map_);
  HistoryInfoMap history_info_map(private_data.history_info_map_);

  // Prove that there is really something there.
  EXPECT_FALSE(private_data.word_list_.empty());
  // available_words_ will already be empty since we have freshly built the
  // data set for this test.
  EXPECT_TRUE(private_data.available_words_.empty());
  EXPECT_FALSE(private_data.word_map_.empty());
  EXPECT_FALSE(private_data.char_word_map_.empty());
  EXPECT_FALSE(private_data.word_id_history_map_.empty());
  EXPECT_FALSE(private_data.history_id_word_map_.empty());
  EXPECT_FALSE(private_data.history_info_map_.empty());

  // Clear and then prove it's clear.
  private_data.Clear();
  EXPECT_TRUE(private_data.word_list_.empty());
  EXPECT_TRUE(private_data.available_words_.empty());
  EXPECT_TRUE(private_data.word_map_.empty());
  EXPECT_TRUE(private_data.char_word_map_.empty());
  EXPECT_TRUE(private_data.word_id_history_map_.empty());
  EXPECT_TRUE(private_data.history_id_word_map_.empty());
  EXPECT_TRUE(private_data.history_info_map_.empty());

  // Restore the cache.
  EXPECT_TRUE(private_data.RestorePrivateData(index_cache));

  // Compare the restored and captured for equality.
  EXPECT_EQ(word_list.size(), private_data.word_list_.size());
  EXPECT_EQ(word_map.size(), private_data.word_map_.size());
  EXPECT_EQ(char_word_map.size(), private_data.char_word_map_.size());
  EXPECT_EQ(word_id_history_map.size(),
            private_data.word_id_history_map_.size());
  EXPECT_EQ(history_id_word_map.size(),
            private_data.history_id_word_map_.size());
  EXPECT_EQ(history_info_map.size(), private_data.history_info_map_.size());
  // WordList must be index-by-index equal.
  size_t count = word_list.size();
  for (size_t i = 0; i < count; ++i)
    EXPECT_EQ(word_list[i], private_data.word_list_[i]);

  ExpectMapOfContainersIdentical(char_word_map,
                                 private_data.char_word_map_);
  ExpectMapOfContainersIdentical(word_id_history_map,
                                 private_data.word_id_history_map_);
  ExpectMapOfContainersIdentical(history_id_word_map,
                                 private_data.history_id_word_map_);

  for (HistoryInfoMap::const_iterator expected = history_info_map.begin();
       expected != history_info_map.end(); ++expected) {
    HistoryInfoMap::const_iterator actual =
        private_data.history_info_map_.find(expected->first);
    ASSERT_FALSE(private_data.history_info_map_.end() == actual);
    const URLRow& expected_row(expected->second);
    const URLRow& actual_row(actual->second);
    EXPECT_EQ(expected_row.visit_count(), actual_row.visit_count());
    EXPECT_EQ(expected_row.typed_count(), actual_row.typed_count());
    EXPECT_EQ(expected_row.last_visit(), actual_row.last_visit());
    EXPECT_EQ(expected_row.url(), actual_row.url());
  }
}

}  // namespace history
