// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/input_method/candidate_window_view.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/widget/widget.h"

typedef views::ViewsTestBase CandidateWindowViewTest;

namespace chromeos {
namespace input_method {

namespace {

void ClearInputMethodLookupTable(InputMethodLookupTable* table) {
  table->visible = false;
  table->cursor_absolute_index = 0;
  table->page_size = 10;
  table->candidates.clear();
  table->orientation = InputMethodLookupTable::kVertical;
  table->labels.clear();
  table->annotations.clear();
  table->mozc_candidates.Clear();
}

void InitializeMozcCandidates(InputMethodLookupTable* table) {
  table->mozc_candidates.Clear();
  table->mozc_candidates.set_position(0);
  table->mozc_candidates.set_size(0);
}

void SetCaretRectIntoMozcCandidates(
    InputMethodLookupTable* table,
    mozc::commands::Candidates::CandidateWindowLocation location,
    int x,
    int y,
    int width,
    int height) {
  table->mozc_candidates.set_window_location(location);
  mozc::commands::Rectangle *rect =
      table->mozc_candidates.mutable_composition_rectangle();
  rect->set_x(x);
  rect->set_y(y);
  rect->set_width(width);
  rect->set_height(height);
}

void AppendCandidateIntoMozcCandidates(InputMethodLookupTable* table,
                                       std::string value) {
  mozc::commands::Candidates::Candidate *candidate =
      table->mozc_candidates.add_candidate();

  int current_entry_count = table->mozc_candidates.candidate_size();
  candidate->set_index(current_entry_count);
  candidate->set_value(value);
  candidate->set_id(current_entry_count);
  candidate->set_information_id(current_entry_count);
}

}  // namespace

TEST_F(CandidateWindowViewTest, ShouldUpdateCandidateViewsTest) {
  // This test verifies the process of judging update lookup-table or not.
  // This judgement is handled by ShouldUpdateCandidateViews, which returns true
  // if update is necessary and vice versa.
  const char* kSampleCandidate1 = "Sample Candidate 1";
  const char* kSampleCandidate2 = "Sample Candidate 2";
  const char* kSampleCandidate3 = "Sample Candidate 3";

  const char* kSampleAnnotation1 = "Sample Annotation 1";
  const char* kSampleAnnotation2 = "Sample Annotation 2";
  const char* kSampleAnnotation3 = "Sample Annotation 3";

  const char* kSampleLabel1 = "Sample Label 1";
  const char* kSampleLabel2 = "Sample Label 2";
  const char* kSampleLabel3 = "Sample Label 3";

  InputMethodLookupTable old_table;
  InputMethodLookupTable new_table;

  ClearInputMethodLookupTable(&old_table);
  ClearInputMethodLookupTable(&new_table);

  old_table.visible = true;
  old_table.cursor_absolute_index = 0;
  old_table.page_size = 1;
  old_table.candidates.clear();
  old_table.orientation = InputMethodLookupTable::kVertical;
  old_table.labels.clear();
  old_table.annotations.clear();

  new_table = old_table;

  EXPECT_FALSE(CandidateWindowView::ShouldUpdateCandidateViews(old_table,
                                                               new_table));

  new_table.visible = false;
  // Visibility would be ignored.
  EXPECT_FALSE(CandidateWindowView::ShouldUpdateCandidateViews(old_table,
                                                               new_table));
  new_table = old_table;
  new_table.candidates.push_back(kSampleCandidate1);
  old_table.candidates.push_back(kSampleCandidate1);
  EXPECT_FALSE(CandidateWindowView::ShouldUpdateCandidateViews(old_table,
                                                               new_table));
  new_table.labels.push_back(kSampleLabel1);
  old_table.labels.push_back(kSampleLabel1);
  EXPECT_FALSE(CandidateWindowView::ShouldUpdateCandidateViews(old_table,
                                                               new_table));
  new_table.annotations.push_back(kSampleAnnotation1);
  old_table.annotations.push_back(kSampleAnnotation1);
  EXPECT_FALSE(CandidateWindowView::ShouldUpdateCandidateViews(old_table,
                                                               new_table));

  new_table.cursor_absolute_index = 1;
  EXPECT_TRUE(CandidateWindowView::ShouldUpdateCandidateViews(old_table,
                                                              new_table));
  new_table = old_table;

  new_table.page_size = 2;
  EXPECT_TRUE(CandidateWindowView::ShouldUpdateCandidateViews(old_table,
                                                              new_table));
  new_table = old_table;

  new_table.orientation = InputMethodLookupTable::kHorizontal;
  EXPECT_TRUE(CandidateWindowView::ShouldUpdateCandidateViews(old_table,
                                                              new_table));

  new_table = old_table;
  new_table.candidates.push_back(kSampleCandidate2);
  EXPECT_TRUE(CandidateWindowView::ShouldUpdateCandidateViews(old_table,
                                                              new_table));
  old_table.candidates.push_back(kSampleCandidate3);
  EXPECT_TRUE(CandidateWindowView::ShouldUpdateCandidateViews(old_table,
                                                              new_table));
  new_table.candidates.clear();
  EXPECT_TRUE(CandidateWindowView::ShouldUpdateCandidateViews(old_table,
                                                              new_table));
  new_table.candidates.push_back(kSampleCandidate2);
  old_table.candidates.clear();
  EXPECT_TRUE(CandidateWindowView::ShouldUpdateCandidateViews(old_table,
                                                              new_table));

  new_table = old_table;
  new_table.labels.push_back(kSampleLabel2);
  EXPECT_TRUE(CandidateWindowView::ShouldUpdateCandidateViews(old_table,
                                                              new_table));
  old_table.labels.push_back(kSampleLabel3);
  EXPECT_TRUE(CandidateWindowView::ShouldUpdateCandidateViews(old_table,
                                                              new_table));
  new_table.labels.clear();
  EXPECT_TRUE(CandidateWindowView::ShouldUpdateCandidateViews(old_table,
                                                              new_table));
  new_table.labels.push_back(kSampleLabel2);
  old_table.labels.clear();
  EXPECT_TRUE(CandidateWindowView::ShouldUpdateCandidateViews(old_table,
                                                              new_table));

  new_table = old_table;
  new_table.annotations.push_back(kSampleAnnotation2);
  EXPECT_TRUE(CandidateWindowView::ShouldUpdateCandidateViews(old_table,
                                                              new_table));
  old_table.annotations.push_back(kSampleAnnotation3);
  EXPECT_TRUE(CandidateWindowView::ShouldUpdateCandidateViews(old_table,
                                                              new_table));
  new_table.annotations.clear();
  EXPECT_TRUE(CandidateWindowView::ShouldUpdateCandidateViews(old_table,
                                                              new_table));
  new_table.annotations.push_back(kSampleAnnotation2);
  old_table.annotations.clear();
  EXPECT_TRUE(CandidateWindowView::ShouldUpdateCandidateViews(old_table,
                                                              new_table));
}

TEST_F(CandidateWindowViewTest, MozcSuggestWindowShouldUpdateTest) {
  // ShouldUpdateCandidateViews method should also judge with consideration of
  // the mozc specific candidate information. Following tests verify them.
  const char* kSampleCandidate1 = "Sample Candidate 1";
  const char* kSampleCandidate2 = "Sample Candidate 2";

  const int kCaretPositionX1 = 10;
  const int kCaretPositionY1 = 20;
  const int kCaretPositionWidth1 = 30;
  const int kCaretPositionHeight1 = 40;

  const int kCaretPositionX2 = 15;
  const int kCaretPositionY2 = 25;
  const int kCaretPositionWidth2 = 35;
  const int kCaretPositionHeight2 = 45;

  InputMethodLookupTable old_table;
  InputMethodLookupTable new_table;

  // State chagne from using non-mozc candidate to mozc candidate.
  ClearInputMethodLookupTable(&old_table);
  ClearInputMethodLookupTable(&new_table);

  old_table.candidates.push_back(kSampleCandidate1);
  InitializeMozcCandidates(&new_table);
  AppendCandidateIntoMozcCandidates(&new_table, kSampleCandidate1);
  SetCaretRectIntoMozcCandidates(&new_table,
                                 mozc::commands::Candidates::COMPOSITION,
                                 kCaretPositionX1,
                                 kCaretPositionY1,
                                 kCaretPositionWidth1,
                                 kCaretPositionHeight1);

  EXPECT_TRUE(CandidateWindowView::ShouldUpdateCandidateViews(old_table,
                                                              new_table));

  // State change from using mozc candidate to non-mozc candidate
  ClearInputMethodLookupTable(&old_table);
  ClearInputMethodLookupTable(&new_table);

  InitializeMozcCandidates(&old_table);
  AppendCandidateIntoMozcCandidates(&old_table, kSampleCandidate1);
  SetCaretRectIntoMozcCandidates(&old_table,
                                 mozc::commands::Candidates::COMPOSITION,
                                 kCaretPositionX1,
                                 kCaretPositionY1,
                                 kCaretPositionWidth1,
                                 kCaretPositionHeight1);

  new_table.candidates.push_back(kSampleCandidate1);

  EXPECT_TRUE(CandidateWindowView::ShouldUpdateCandidateViews(old_table,
                                                              new_table));

  // State change from using mozc candidate to mozc candidate

  // No change
  ClearInputMethodLookupTable(&old_table);
  ClearInputMethodLookupTable(&new_table);

  InitializeMozcCandidates(&old_table);
  AppendCandidateIntoMozcCandidates(&old_table, kSampleCandidate1);
  SetCaretRectIntoMozcCandidates(&old_table,
                                 mozc::commands::Candidates::COMPOSITION,
                                 kCaretPositionX1,
                                 kCaretPositionY1,
                                 kCaretPositionWidth1,
                                 kCaretPositionHeight1);

  InitializeMozcCandidates(&new_table);
  AppendCandidateIntoMozcCandidates(&new_table, kSampleCandidate1);
  SetCaretRectIntoMozcCandidates(&new_table,
                                 mozc::commands::Candidates::COMPOSITION,
                                 kCaretPositionX1,
                                 kCaretPositionY1,
                                 kCaretPositionWidth1,
                                 kCaretPositionHeight1);

  EXPECT_FALSE(CandidateWindowView::ShouldUpdateCandidateViews(old_table,
                                                               new_table));
  // Position change only
  ClearInputMethodLookupTable(&old_table);
  ClearInputMethodLookupTable(&new_table);

  InitializeMozcCandidates(&old_table);
  AppendCandidateIntoMozcCandidates(&old_table, kSampleCandidate1);
  SetCaretRectIntoMozcCandidates(&old_table,
                                 mozc::commands::Candidates::COMPOSITION,
                                 kCaretPositionX1,
                                 kCaretPositionY1,
                                 kCaretPositionWidth1,
                                 kCaretPositionHeight1);

  InitializeMozcCandidates(&new_table);
  AppendCandidateIntoMozcCandidates(&new_table, kSampleCandidate1);
  SetCaretRectIntoMozcCandidates(&new_table,
                                 mozc::commands::Candidates::COMPOSITION,
                                 kCaretPositionX2,
                                 kCaretPositionY2,
                                 kCaretPositionWidth2,
                                 kCaretPositionHeight2);

  EXPECT_TRUE(CandidateWindowView::ShouldUpdateCandidateViews(old_table,
                                                              new_table));
  // Candidate contents only
  ClearInputMethodLookupTable(&old_table);
  ClearInputMethodLookupTable(&new_table);

  InitializeMozcCandidates(&old_table);
  AppendCandidateIntoMozcCandidates(&old_table, kSampleCandidate1);
  SetCaretRectIntoMozcCandidates(&old_table,
                                 mozc::commands::Candidates::COMPOSITION,
                                 kCaretPositionX1,
                                 kCaretPositionY1,
                                 kCaretPositionWidth1,
                                 kCaretPositionHeight1);

  InitializeMozcCandidates(&new_table);
  AppendCandidateIntoMozcCandidates(&new_table, kSampleCandidate2);
  SetCaretRectIntoMozcCandidates(&new_table,
                                 mozc::commands::Candidates::COMPOSITION,
                                 kCaretPositionX1,
                                 kCaretPositionY1,
                                 kCaretPositionWidth1,
                                 kCaretPositionHeight1);

  EXPECT_TRUE(CandidateWindowView::ShouldUpdateCandidateViews(old_table,
                                                              new_table));

  // Both candidate and position
  ClearInputMethodLookupTable(&old_table);
  ClearInputMethodLookupTable(&new_table);

  InitializeMozcCandidates(&old_table);
  AppendCandidateIntoMozcCandidates(&old_table, kSampleCandidate1);
  SetCaretRectIntoMozcCandidates(&old_table,
                                 mozc::commands::Candidates::COMPOSITION,
                                 kCaretPositionX1,
                                 kCaretPositionY1,
                                 kCaretPositionWidth1,
                                 kCaretPositionHeight1);

  InitializeMozcCandidates(&new_table);
  AppendCandidateIntoMozcCandidates(&new_table, kSampleCandidate2);
  SetCaretRectIntoMozcCandidates(&new_table,
                                 mozc::commands::Candidates::COMPOSITION,
                                 kCaretPositionX2,
                                 kCaretPositionY2,
                                 kCaretPositionWidth2,
                                 kCaretPositionHeight2);

  EXPECT_TRUE(CandidateWindowView::ShouldUpdateCandidateViews(old_table,
                                                              new_table));
}

TEST_F(CandidateWindowViewTest, MozcUpdateCandidateTest) {
  // This test verifies whether UpdateCandidates function updates window mozc
  // specific candidate position correctly on the correct condition.

  // For testing, we have to prepare empty widget.
  // We should NOT manually free widget by default, otherwise double free will
  // be occurred. So, we should instantiate widget class with "new" operation.
  views::Widget* widget = new views::Widget;
  views::Widget::InitParams params(views::Widget::InitParams::TYPE_WINDOW);
  widget->Init(params);

  CandidateWindowView candidate_window_view(widget);
  candidate_window_view.Init();

  const int kCaretPositionX1 = 10;
  const int kCaretPositionY1 = 20;
  const int kCaretPositionWidth1 = 30;
  const int kCaretPositionHeight1 = 40;

  const int kCaretPositionX2 = 15;
  const int kCaretPositionY2 = 25;
  const int kCaretPositionWidth2 = 35;
  const int kCaretPositionHeight2 = 45;

  InputMethodLookupTable new_table;
  ClearInputMethodLookupTable(&new_table);
  InitializeMozcCandidates(&new_table);

  // If window location is CARET, use default position. So
  // is_suggestion_window_location_available_ should be false.
  SetCaretRectIntoMozcCandidates(&new_table,
                                 mozc::commands::Candidates::CARET,
                                 kCaretPositionX1,
                                 kCaretPositionY1,
                                 kCaretPositionWidth1,
                                 kCaretPositionHeight1);
  candidate_window_view.UpdateCandidates(new_table);
  EXPECT_FALSE(candidate_window_view.is_suggestion_window_location_available_);

  // If window location is COMPOSITION, update position and set
  // is_suggestion_window_location_available_ as true.
  SetCaretRectIntoMozcCandidates(&new_table,
                                 mozc::commands::Candidates::COMPOSITION,
                                 kCaretPositionX1,
                                 kCaretPositionY1,
                                 kCaretPositionWidth1,
                                 kCaretPositionHeight1);
  candidate_window_view.UpdateCandidates(new_table);
  EXPECT_TRUE(candidate_window_view.is_suggestion_window_location_available_);
  EXPECT_EQ(kCaretPositionX1,
            candidate_window_view.suggestion_window_location_.x());
  EXPECT_EQ(kCaretPositionY1,
            candidate_window_view.suggestion_window_location_.y());
  EXPECT_EQ(kCaretPositionWidth1,
            candidate_window_view.suggestion_window_location_.width());
  EXPECT_EQ(kCaretPositionHeight1,
            candidate_window_view.suggestion_window_location_.height());

  SetCaretRectIntoMozcCandidates(&new_table,
                                 mozc::commands::Candidates::COMPOSITION,
                                 kCaretPositionX2,
                                 kCaretPositionY2,
                                 kCaretPositionWidth2,
                                 kCaretPositionHeight2);
  candidate_window_view.UpdateCandidates(new_table);
  EXPECT_TRUE(candidate_window_view.is_suggestion_window_location_available_);
  EXPECT_EQ(kCaretPositionX2,
            candidate_window_view.suggestion_window_location_.x());
  EXPECT_EQ(kCaretPositionY2,
            candidate_window_view.suggestion_window_location_.y());
  EXPECT_EQ(kCaretPositionWidth2,
            candidate_window_view.suggestion_window_location_.width());
  EXPECT_EQ(kCaretPositionHeight2,
            candidate_window_view.suggestion_window_location_.height());

  // We should call CloseNow method, otherwise this test will leak memory.
  widget->CloseNow();
}
}  // namespace input_method
}  // namespace chromeos
