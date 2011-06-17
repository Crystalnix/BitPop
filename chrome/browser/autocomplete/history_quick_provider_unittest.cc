// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/autocomplete/history_quick_provider.h"

#include <algorithm>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/autocomplete/autocomplete.h"
#include "chrome/browser/autocomplete/autocomplete_match.h"
#include "chrome/browser/history/history.h"
#include "chrome/browser/history/in_memory_url_index.h"
#include "chrome/browser/history/url_database.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/testing_browser_process.h"
#include "chrome/test/testing_browser_process_test.h"
#include "chrome/test/testing_profile.h"
#include "content/browser/browser_thread.h"
#include "testing/gtest/include/gtest/gtest.h"

using base::Time;
using base::TimeDelta;

struct TestURLInfo {
  std::string url;
  std::string title;
  int visit_count;
  int typed_count;
  int days_from_now;
} quick_test_db[] = {
  {"http://www.google.com/", "Google", 3, 3, 0},
  {"http://slashdot.org/favorite_page.html", "Favorite page", 200, 100, 0},
  {"http://kerneltrap.org/not_very_popular.html", "Less popular", 4, 0, 0},
  {"http://freshmeat.net/unpopular.html", "Unpopular", 1, 1, 0},
  {"http://news.google.com/?ned=us&topic=n", "Google News - U.S.", 2, 2, 0},
  {"http://news.google.com/", "Google News", 1, 1, 0},
  {"http://foo.com/", "Dir", 5, 5, 0},
  {"http://foo.com/dir/", "Dir", 2, 1, 10},
  {"http://foo.com/dir/another/", "Dir", 5, 1, 0},
  {"http://foo.com/dir/another/again/", "Dir", 10, 0, 0},
  {"http://foo.com/dir/another/again/myfile.html", "File", 10, 2, 0},
  {"http://visitedest.com/y/a", "VA", 5, 1, 0},
  {"http://visitedest.com/y/b", "VB", 4, 1, 0},
  {"http://visitedest.com/x/c", "VC", 3, 1, 0},
  {"http://visitedest.com/x/d", "VD", 2, 1, 0},
  {"http://visitedest.com/y/e", "VE", 1, 1, 0},
  {"http://typeredest.com/y/a", "TA", 3, 5, 0},
  {"http://typeredest.com/y/b", "TB", 3, 4, 0},
  {"http://typeredest.com/x/c", "TC", 3, 3, 0},
  {"http://typeredest.com/x/d", "TD", 3, 2, 0},
  {"http://typeredest.com/y/e", "TE", 3, 1, 0},
  {"http://daysagoest.com/y/a", "DA", 1, 1, 0},
  {"http://daysagoest.com/y/b", "DB", 1, 1, 1},
  {"http://daysagoest.com/x/c", "DC", 1, 1, 2},
  {"http://daysagoest.com/x/d", "DD", 1, 1, 3},
  {"http://daysagoest.com/y/e", "DE", 1, 1, 4},
  {"http://abcdefghixyzjklmnopqrstuvw.com/a", "", 3, 1, 0},
  {"http://spaces.com/path%20with%20spaces/foo.html", "Spaces", 2, 2, 0},
  {"http://abcdefghijklxyzmnopqrstuvw.com/a", "", 3, 1, 0},
  {"http://abcdefxyzghijklmnopqrstuvw.com/a", "", 3, 1, 0},
  {"http://abcxyzdefghijklmnopqrstuvw.com/a", "", 3, 1, 0},
  {"http://xyzabcdefghijklmnopqrstuvw.com/a", "", 3, 1, 0},
  {"http://cda.com/Dogs%20Cats%20Gorillas%20Sea%20Slugs%20and%20Mice",
   "Dogs & Cats & Mice", 1, 1, 0},
};

class HistoryQuickProviderTest : public TestingBrowserProcessTest,
                                 public ACProviderListener {
 public:
  HistoryQuickProviderTest()
      : ui_thread_(BrowserThread::UI, &message_loop_),
        file_thread_(BrowserThread::FILE, &message_loop_) {}

  // ACProviderListener
  virtual void OnProviderUpdate(bool updated_matches);

 protected:
  void SetUp() {
    profile_.reset(new TestingProfile());
    profile_->CreateHistoryService(true, false);
    profile_->CreateBookmarkModel(true);
    profile_->BlockUntilBookmarkModelLoaded();
    history_service_ = profile_->GetHistoryService(Profile::EXPLICIT_ACCESS);
    EXPECT_TRUE(history_service_);
    provider_ = new HistoryQuickProvider(this, profile_.get());
    FillData();
  }

  void TearDown() {
    provider_ = NULL;
  }

  // Fills test data into the history system.
  void FillData();

  // Runs an autocomplete query on |text| and checks to see that the returned
  // results' destination URLs match those provided. |expected_urls| does not
  // need to be in sorted order.
  void RunTest(const string16 text,
               std::vector<std::string> expected_urls,
               std::string expected_top_result);

  MessageLoopForUI message_loop_;
  BrowserThread ui_thread_;
  BrowserThread file_thread_;

  scoped_ptr<TestingProfile> profile_;
  HistoryService* history_service_;

  ACMatches ac_matches_;  // The resulting matches after running RunTest.

 private:
  scoped_refptr<HistoryQuickProvider> provider_;
};

void HistoryQuickProviderTest::OnProviderUpdate(bool updated_matches) {
  MessageLoop::current()->Quit();
}

void HistoryQuickProviderTest::FillData() {
  history::URLDatabase* db = history_service_->InMemoryDatabase();
  ASSERT_TRUE(db != NULL);
  for (size_t i = 0; i < arraysize(quick_test_db); ++i) {
    const TestURLInfo& cur = quick_test_db[i];
    const GURL current_url(cur.url);
    Time visit_time = Time::Now() - TimeDelta::FromDays(cur.days_from_now);

    history::URLRow url_info(current_url);
    url_info.set_title(UTF8ToUTF16(cur.title));
    url_info.set_visit_count(cur.visit_count);
    url_info.set_typed_count(cur.typed_count);
    url_info.set_last_visit(visit_time);
    url_info.set_hidden(false);
    EXPECT_TRUE(db->AddURL(url_info));

    history_service_->AddPageWithDetails(current_url, UTF8ToUTF16(cur.title),
                                         cur.visit_count, cur.typed_count,
                                         visit_time, false,
                                         history::SOURCE_BROWSED);
  }

  history::InMemoryURLIndex* index =
      new history::InMemoryURLIndex(FilePath(FILE_PATH_LITERAL("/dummy")));
  PrefService* prefs = profile_->GetPrefs();
  std::string languages(prefs->GetString(prefs::kAcceptLanguages));
  index->Init(db, languages);
  provider_->SetIndexForTesting(index);
}

class SetShouldContain : public std::unary_function<const std::string&,
                                                    std::set<std::string> > {
 public:
  explicit SetShouldContain(const ACMatches& matched_urls) {
    for (ACMatches::const_iterator iter = matched_urls.begin();
         iter != matched_urls.end(); ++iter)
      matches_.insert(iter->destination_url.spec());
  }

  void operator()(const std::string& expected) {
    EXPECT_EQ(1U, matches_.erase(expected));
  }

  std::set<std::string> LeftOvers() const { return matches_; }

 private:
  std::set<std::string> matches_;
};

void HistoryQuickProviderTest::RunTest(const string16 text,
                                       std::vector<std::string> expected_urls,
                                       std::string expected_top_result) {
  std::sort(expected_urls.begin(), expected_urls.end());

  MessageLoop::current()->RunAllPending();
  AutocompleteInput input(text, string16(), false, false, true,
                          AutocompleteInput::ALL_MATCHES);
  provider_->Start(input, false);
  EXPECT_TRUE(provider_->done());

  ac_matches_ = provider_->matches();

  // We should have gotten back at most AutocompleteProvider::kMaxMatches.
  EXPECT_LE(ac_matches_.size(), AutocompleteProvider::kMaxMatches);

  // If the number of expected and actual matches aren't equal then we need
  // test no further, but let's do anyway so that we know which URLs failed.
  EXPECT_EQ(expected_urls.size(), ac_matches_.size());

  // Verify that all expected URLs were found and that all found URLs
  // were expected.
  std::set<std::string> leftovers =
      for_each(expected_urls.begin(), expected_urls.end(),
               SetShouldContain(ac_matches_)).LeftOvers();
  EXPECT_EQ(0U, leftovers.size());

  // See if we got the expected top scorer.
  if (!ac_matches_.empty()) {
    std::partial_sort(ac_matches_.begin(), ac_matches_.begin() + 1,
                      ac_matches_.end(), AutocompleteMatch::MoreRelevant);
    EXPECT_EQ(expected_top_result, ac_matches_[0].destination_url.spec());
  }
}

TEST_F(HistoryQuickProviderTest, SimpleSingleMatch) {
  string16 text(ASCIIToUTF16("slashdot"));
  std::string expected_url("http://slashdot.org/favorite_page.html");
  std::vector<std::string> expected_urls;
  expected_urls.push_back(expected_url);
  RunTest(text, expected_urls, expected_url);
}

TEST_F(HistoryQuickProviderTest, MultiMatch) {
  string16 text(ASCIIToUTF16("foo"));
  std::vector<std::string> expected_urls;
  // Scores high because of typed_count.
  expected_urls.push_back("http://foo.com/");
  // Scores high because of visit count.
  expected_urls.push_back("http://foo.com/dir/another/");
  // Scores high because of high visit count.
  expected_urls.push_back("http://foo.com/dir/another/again/myfile.html");
  RunTest(text, expected_urls, "http://foo.com/");
}

TEST_F(HistoryQuickProviderTest, StartRelativeMatch) {
  string16 text(ASCIIToUTF16("xyz"));
  std::vector<std::string> expected_urls;
  expected_urls.push_back("http://xyzabcdefghijklmnopqrstuvw.com/a");
  expected_urls.push_back("http://abcxyzdefghijklmnopqrstuvw.com/a");
  expected_urls.push_back("http://abcdefxyzghijklmnopqrstuvw.com/a");
  RunTest(text, expected_urls, "http://xyzabcdefghijklmnopqrstuvw.com/a");
}

TEST_F(HistoryQuickProviderTest, VisitCountMatches) {
  string16 text(ASCIIToUTF16("visitedest"));
  std::vector<std::string> expected_urls;
  expected_urls.push_back("http://visitedest.com/y/a");
  expected_urls.push_back("http://visitedest.com/y/b");
  expected_urls.push_back("http://visitedest.com/x/c");
  RunTest(text, expected_urls, "http://visitedest.com/y/a");
}

TEST_F(HistoryQuickProviderTest, TypedCountMatches) {
  string16 text(ASCIIToUTF16("typeredest"));
  std::vector<std::string> expected_urls;
  expected_urls.push_back("http://typeredest.com/y/a");
  expected_urls.push_back("http://typeredest.com/y/b");
  expected_urls.push_back("http://typeredest.com/x/c");
  RunTest(text, expected_urls, "http://typeredest.com/y/a");
}

TEST_F(HistoryQuickProviderTest, DaysAgoMatches) {
  string16 text(ASCIIToUTF16("daysagoest"));
  std::vector<std::string> expected_urls;
  expected_urls.push_back("http://daysagoest.com/y/a");
  expected_urls.push_back("http://daysagoest.com/y/b");
  expected_urls.push_back("http://daysagoest.com/x/c");
  RunTest(text, expected_urls, "http://daysagoest.com/y/a");
}

TEST_F(HistoryQuickProviderTest, EncodingLimitMatch) {
  string16 text(ASCIIToUTF16("ice"));
  std::vector<std::string> expected_urls;
  std::string url(
      "http://cda.com/Dogs%20Cats%20Gorillas%20Sea%20Slugs%20and%20Mice");
  expected_urls.push_back(url);
  RunTest(text, expected_urls, url);
  // Verify that the matches' ACMatchClassifications offsets are in range.
  ACMatchClassifications content(ac_matches_[0].contents_class);
  // The max offset accounts for 6 occurrences of '%20' plus the 'http://'.
  const size_t max_offset = url.size() - ((6 * 2) + 7);
  for (ACMatchClassifications::const_iterator citer = content.begin();
       citer != content.end(); ++citer)
    EXPECT_LT(citer->offset, max_offset);
  ACMatchClassifications description(ac_matches_[0].description_class);
  std::string page_title("Dogs & Cats & Mice");
  for (ACMatchClassifications::const_iterator diter = description.begin();
       diter != description.end(); ++diter)
    EXPECT_LT(diter->offset, page_title.size());
}

TEST_F(HistoryQuickProviderTest, Spans) {
  // Test SpansFromTermMatch
  history::TermMatches matches_a;
  // Simulates matches: '.xx.xxx..xx...xxxxx..' which will test no match at
  // either beginning or end as well as adjacent matches.
  matches_a.push_back(history::TermMatch(1, 1, 2));
  matches_a.push_back(history::TermMatch(2, 4, 3));
  matches_a.push_back(history::TermMatch(3, 9, 1));
  matches_a.push_back(history::TermMatch(3, 10, 1));
  matches_a.push_back(history::TermMatch(4, 14, 5));
  ACMatchClassifications spans_a =
      HistoryQuickProvider::SpansFromTermMatch(matches_a, 20);
  // ACMatch spans should be: 'NM-NM---N-M-N--M----N-'
  ASSERT_EQ(9U, spans_a.size());
  EXPECT_EQ(0U, spans_a[0].offset);
  EXPECT_EQ(ACMatchClassification::NONE, spans_a[0].style);
  EXPECT_EQ(1U, spans_a[1].offset);
  EXPECT_EQ(ACMatchClassification::MATCH, spans_a[1].style);
  EXPECT_EQ(3U, spans_a[2].offset);
  EXPECT_EQ(ACMatchClassification::NONE, spans_a[2].style);
  EXPECT_EQ(4U, spans_a[3].offset);
  EXPECT_EQ(ACMatchClassification::MATCH, spans_a[3].style);
  EXPECT_EQ(7U, spans_a[4].offset);
  EXPECT_EQ(ACMatchClassification::NONE, spans_a[4].style);
  EXPECT_EQ(9U, spans_a[5].offset);
  EXPECT_EQ(ACMatchClassification::MATCH, spans_a[5].style);
  EXPECT_EQ(11U, spans_a[6].offset);
  EXPECT_EQ(ACMatchClassification::NONE, spans_a[6].style);
  EXPECT_EQ(14U, spans_a[7].offset);
  EXPECT_EQ(ACMatchClassification::MATCH, spans_a[7].style);
  EXPECT_EQ(19U, spans_a[8].offset);
  EXPECT_EQ(ACMatchClassification::NONE, spans_a[8].style);
  // Simulates matches: 'xx.xx' which will test matches at both beginning an
  // end.
  history::TermMatches matches_b;
  matches_b.push_back(history::TermMatch(1, 0, 2));
  matches_b.push_back(history::TermMatch(2, 3, 2));
  ACMatchClassifications spans_b =
      HistoryQuickProvider::SpansFromTermMatch(matches_b, 5);
  // ACMatch spans should be: 'M-NM-'
  ASSERT_EQ(3U, spans_b.size());
  EXPECT_EQ(0U, spans_b[0].offset);
  EXPECT_EQ(ACMatchClassification::MATCH, spans_b[0].style);
  EXPECT_EQ(2U, spans_b[1].offset);
  EXPECT_EQ(ACMatchClassification::NONE, spans_b[1].style);
  EXPECT_EQ(3U, spans_b[2].offset);
  EXPECT_EQ(ACMatchClassification::MATCH, spans_b[2].style);
}
