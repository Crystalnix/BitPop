// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/auto_reset.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "base/utf_string_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/range/range.h"
#include "views/controls/textfield/textfield.h"
#include "views/controls/textfield/textfield_views_model.h"
#include "views/test/test_views_delegate.h"
#include "views/test/views_test_base.h"
#include "views/views_delegate.h"

namespace views {

#include "views/test/views_test_base.h"

class TextfieldViewsModelTest : public ViewsTestBase,
                                public TextfieldViewsModel::Delegate {
 public:
  TextfieldViewsModelTest()
      : ViewsTestBase(),
        composition_text_confirmed_or_cleared_(false) {
  }

  virtual void OnCompositionTextConfirmedOrCleared() {
    composition_text_confirmed_or_cleared_ = true;
  }

 protected:
  bool composition_text_confirmed_or_cleared_;

 private:
  DISALLOW_COPY_AND_ASSIGN(TextfieldViewsModelTest);
};

#define EXPECT_STR_EQ(ascii, utf16) \
  EXPECT_EQ(ASCIIToWide(ascii), UTF16ToWide(utf16))

TEST_F(TextfieldViewsModelTest, EditString) {
  TextfieldViewsModel model(NULL);
  // append two strings
  model.Append(ASCIIToUTF16("HILL"));
  EXPECT_STR_EQ("HILL", model.text());
  model.Append(ASCIIToUTF16("WORLD"));
  EXPECT_STR_EQ("HILLWORLD", model.text());

  // Insert "E" to make hello
  model.MoveCursorRight(false);
  model.InsertChar('E');
  EXPECT_STR_EQ("HEILLWORLD", model.text());
  // Replace "I" with "L"
  model.ReplaceChar('L');
  EXPECT_STR_EQ("HELLLWORLD", model.text());
  model.ReplaceChar('L');
  model.ReplaceChar('O');
  EXPECT_STR_EQ("HELLOWORLD", model.text());

  // Delete 6th char "W", then delete 5th char O"
  EXPECT_EQ(5U, model.cursor_pos());
  EXPECT_TRUE(model.Delete());
  EXPECT_STR_EQ("HELLOORLD", model.text());
  EXPECT_TRUE(model.Backspace());
  EXPECT_EQ(4U, model.cursor_pos());
  EXPECT_STR_EQ("HELLORLD", model.text());

  // Move the cursor to start. backspace should fail.
  model.MoveCursorToHome(false);
  EXPECT_FALSE(model.Backspace());
  EXPECT_STR_EQ("HELLORLD", model.text());
  // Move the cursor to the end. delete should fail.
  model.MoveCursorToEnd(false);
  EXPECT_FALSE(model.Delete());
  EXPECT_STR_EQ("HELLORLD", model.text());
  // but backspace should work.
  EXPECT_TRUE(model.Backspace());
  EXPECT_STR_EQ("HELLORL", model.text());
}

TEST_F(TextfieldViewsModelTest, EmptyString) {
  TextfieldViewsModel model(NULL);
  EXPECT_EQ(string16(), model.text());
  EXPECT_EQ(string16(), model.GetSelectedText());
  EXPECT_EQ(string16(), model.GetVisibleText());

  model.MoveCursorLeft(true);
  EXPECT_EQ(0U, model.cursor_pos());
  model.MoveCursorRight(true);
  EXPECT_EQ(0U, model.cursor_pos());

  EXPECT_EQ(string16(), model.GetSelectedText());

  EXPECT_FALSE(model.Delete());
  EXPECT_FALSE(model.Backspace());
}

TEST_F(TextfieldViewsModelTest, Selection) {
  TextfieldViewsModel model(NULL);
  model.Append(ASCIIToUTF16("HELLO"));
  model.MoveCursorRight(false);
  model.MoveCursorRight(true);
  EXPECT_STR_EQ("E", model.GetSelectedText());
  model.MoveCursorRight(true);
  EXPECT_STR_EQ("EL", model.GetSelectedText());

  model.MoveCursorToHome(true);
  EXPECT_STR_EQ("H", model.GetSelectedText());
  model.MoveCursorToEnd(true);
  EXPECT_STR_EQ("ELLO", model.GetSelectedText());
  model.ClearSelection();
  EXPECT_EQ(string16(), model.GetSelectedText());
  model.SelectAll();
  EXPECT_STR_EQ("HELLO", model.GetSelectedText());
  // SelectAll should select towards the end.
  ui::Range range;
  model.GetSelectedRange(&range);
  EXPECT_EQ(0U, range.start());
  EXPECT_EQ(5U, range.end());

  // Select and move cursor
  model.MoveCursorTo(1U, false);
  model.MoveCursorTo(3U, true);
  EXPECT_STR_EQ("EL", model.GetSelectedText());
  model.MoveCursorLeft(false);
  EXPECT_EQ(1U, model.cursor_pos());
  model.MoveCursorTo(1U, false);
  model.MoveCursorTo(3U, true);
  model.MoveCursorRight(false);
  EXPECT_EQ(3U, model.cursor_pos());

  // Select all and move cursor
  model.SelectAll();
  model.MoveCursorLeft(false);
  EXPECT_EQ(0U, model.cursor_pos());
  model.SelectAll();
  model.MoveCursorRight(false);
  EXPECT_EQ(5U, model.cursor_pos());
}

TEST_F(TextfieldViewsModelTest, SelectionAndEdit) {
  TextfieldViewsModel model(NULL);
  model.Append(ASCIIToUTF16("HELLO"));
  model.MoveCursorRight(false);
  model.MoveCursorRight(true);
  model.MoveCursorRight(true);  // select "EL"
  EXPECT_TRUE(model.Backspace());
  EXPECT_STR_EQ("HLO", model.text());

  model.Append(ASCIIToUTF16("ILL"));
  model.MoveCursorRight(true);
  model.MoveCursorRight(true);  // select "LO"
  EXPECT_TRUE(model.Delete());
  EXPECT_STR_EQ("HILL", model.text());
  EXPECT_EQ(1U, model.cursor_pos());
  model.MoveCursorRight(true);  // select "I"
  model.InsertChar('E');
  EXPECT_STR_EQ("HELL", model.text());
  model.MoveCursorToHome(false);
  model.MoveCursorRight(true);  // select "H"
  model.ReplaceChar('B');
  EXPECT_STR_EQ("BELL", model.text());
  model.MoveCursorToEnd(false);
  model.MoveCursorLeft(true);
  model.MoveCursorLeft(true);  // select ">LL"
  model.ReplaceChar('E');
  EXPECT_STR_EQ("BEE", model.text());
}

TEST_F(TextfieldViewsModelTest, Password) {
  TextfieldViewsModel model(NULL);
  model.set_is_password(true);
  model.Append(ASCIIToUTF16("HELLO"));
  EXPECT_STR_EQ("*****", model.GetVisibleText());
  EXPECT_STR_EQ("HELLO", model.text());
  EXPECT_TRUE(model.Delete());

  EXPECT_STR_EQ("****", model.GetVisibleText());
  EXPECT_STR_EQ("ELLO", model.text());
  EXPECT_EQ(0U, model.cursor_pos());

  model.SelectAll();
  EXPECT_STR_EQ("ELLO", model.GetSelectedText());
  EXPECT_EQ(4U, model.cursor_pos());

  model.InsertChar('X');
  EXPECT_STR_EQ("*", model.GetVisibleText());
  EXPECT_STR_EQ("X", model.text());
}

TEST_F(TextfieldViewsModelTest, Word) {
  TextfieldViewsModel model(NULL);
  model.Append(
      ASCIIToUTF16("The answer to Life, the Universe, and Everything"));
  model.MoveCursorToNextWord(false);
  EXPECT_EQ(3U, model.cursor_pos());
  model.MoveCursorToNextWord(false);
  EXPECT_EQ(10U, model.cursor_pos());
  model.MoveCursorToNextWord(false);
  model.MoveCursorToNextWord(false);
  EXPECT_EQ(18U, model.cursor_pos());

  // Should passes the non word char ','
  model.MoveCursorToNextWord(true);
  EXPECT_EQ(23U, model.cursor_pos());
  EXPECT_STR_EQ(", the", model.GetSelectedText());

  // Move to the end.
  model.MoveCursorToNextWord(true);
  model.MoveCursorToNextWord(true);
  model.MoveCursorToNextWord(true);
  EXPECT_STR_EQ(", the Universe, and Everything", model.GetSelectedText());
  // Should be safe to go next word at the end.
  model.MoveCursorToNextWord(true);
  EXPECT_STR_EQ(", the Universe, and Everything", model.GetSelectedText());
  model.InsertChar('2');
  EXPECT_EQ(19U, model.cursor_pos());

  // Now backwards.
  model.MoveCursorLeft(false);  // leave 2.
  model.MoveCursorToPreviousWord(true);
  EXPECT_EQ(14U, model.cursor_pos());
  EXPECT_STR_EQ("Life", model.GetSelectedText());
  model.MoveCursorToPreviousWord(true);
  EXPECT_STR_EQ("to Life", model.GetSelectedText());
  model.MoveCursorToPreviousWord(true);
  model.MoveCursorToPreviousWord(true);
  model.MoveCursorToPreviousWord(true);  // Select to the begining.
  EXPECT_STR_EQ("The answer to Life", model.GetSelectedText());
  // Should be safe to go pervious word at the begining.
  model.MoveCursorToPreviousWord(true);
  EXPECT_STR_EQ("The answer to Life", model.GetSelectedText());
  model.ReplaceChar('4');
  EXPECT_EQ(string16(), model.GetSelectedText());
  EXPECT_STR_EQ("42", model.GetVisibleText());
}

TEST_F(TextfieldViewsModelTest, TextFragment) {
  TextfieldViewsModel model(NULL);
  TextfieldViewsModel::TextFragments fragments;
  // Empty string
  model.GetFragments(&fragments);
  EXPECT_EQ(1U, fragments.size());
  EXPECT_EQ(0U, fragments[0].start);
  EXPECT_EQ(0U, fragments[0].end);
  EXPECT_FALSE(fragments[0].selected);

  // Some string
  model.Append(ASCIIToUTF16("Hello world"));
  model.GetFragments(&fragments);
  EXPECT_EQ(1U, fragments.size());
  EXPECT_EQ(0U, fragments[0].start);
  EXPECT_EQ(11U, fragments[0].end);
  EXPECT_FALSE(fragments[0].selected);

  // Select 1st word
  model.MoveCursorToNextWord(true);
  model.GetFragments(&fragments);
  EXPECT_EQ(2U, fragments.size());
  EXPECT_EQ(0U, fragments[0].start);
  EXPECT_EQ(5U, fragments[0].end);
  EXPECT_TRUE(fragments[0].selected);
  EXPECT_EQ(5U, fragments[1].start);
  EXPECT_EQ(11U, fragments[1].end);
  EXPECT_FALSE(fragments[1].selected);

  // Select empty string
  model.ClearSelection();
  model.MoveCursorRight(true);
  model.GetFragments(&fragments);
  EXPECT_EQ(3U, fragments.size());
  EXPECT_EQ(0U, fragments[0].start);
  EXPECT_EQ(5U, fragments[0].end);
  EXPECT_FALSE(fragments[0].selected);
  EXPECT_EQ(5U, fragments[1].start);
  EXPECT_EQ(6U, fragments[1].end);
  EXPECT_TRUE(fragments[1].selected);

  EXPECT_EQ(6U, fragments[2].start);
  EXPECT_EQ(11U, fragments[2].end);
  EXPECT_FALSE(fragments[2].selected);

  // Select to the end.
  model.MoveCursorToEnd(true);
  model.GetFragments(&fragments);
  EXPECT_EQ(2U, fragments.size());
  EXPECT_EQ(0U, fragments[0].start);
  EXPECT_EQ(5U, fragments[0].end);
  EXPECT_FALSE(fragments[0].selected);
  EXPECT_EQ(5U, fragments[1].start);
  EXPECT_EQ(11U, fragments[1].end);
  EXPECT_TRUE(fragments[1].selected);
}

TEST_F(TextfieldViewsModelTest, SetText) {
  TextfieldViewsModel model(NULL);
  model.Append(ASCIIToUTF16("HELLO"));
  model.MoveCursorToEnd(false);
  model.SetText(ASCIIToUTF16("GOODBYE"));
  EXPECT_STR_EQ("GOODBYE", model.text());
  EXPECT_EQ(5U, model.cursor_pos());
  model.SelectAll();
  EXPECT_STR_EQ("GOODBYE", model.GetSelectedText());
  // Selection move the current pos to the end.
  EXPECT_EQ(7U, model.cursor_pos());
  model.MoveCursorToHome(false);
  EXPECT_EQ(0U, model.cursor_pos());
  model.MoveCursorToEnd(false);

  model.SetText(ASCIIToUTF16("BYE"));
  EXPECT_EQ(3U, model.cursor_pos());
  EXPECT_EQ(string16(), model.GetSelectedText());
  model.SetText(ASCIIToUTF16(""));
  EXPECT_EQ(0U, model.cursor_pos());
}

TEST_F(TextfieldViewsModelTest, Clipboard) {
  scoped_ptr<TestViewsDelegate> test_views_delegate(new TestViewsDelegate());
  AutoReset<views::ViewsDelegate*> auto_reset(
      &views::ViewsDelegate::views_delegate, test_views_delegate.get());
  ui::Clipboard* clipboard
      = views::ViewsDelegate::views_delegate->GetClipboard();
  string16 initial_clipboard_text;
  clipboard->ReadText(ui::Clipboard::BUFFER_STANDARD, &initial_clipboard_text);
  string16 clipboard_text;
  TextfieldViewsModel model(NULL);
  model.Append(ASCIIToUTF16("HELLO WORLD"));
  model.MoveCursorToEnd(false);

  // Test for cut: Empty selection.
  EXPECT_FALSE(model.Cut());
  clipboard->ReadText(ui::Clipboard::BUFFER_STANDARD, &clipboard_text);
  EXPECT_STR_EQ(UTF16ToUTF8(initial_clipboard_text), clipboard_text);
  EXPECT_STR_EQ("HELLO WORLD", model.text());
  EXPECT_EQ(11U, model.cursor_pos());

  // Test for cut: Non-empty selection.
  model.MoveCursorToPreviousWord(true);
  EXPECT_TRUE(model.Cut());
  clipboard->ReadText(ui::Clipboard::BUFFER_STANDARD, &clipboard_text);
  EXPECT_STR_EQ("WORLD", clipboard_text);
  EXPECT_STR_EQ("HELLO ", model.text());
  EXPECT_EQ(6U, model.cursor_pos());

  // Test for copy: Empty selection.
  model.Copy();
  clipboard->ReadText(ui::Clipboard::BUFFER_STANDARD, &clipboard_text);
  EXPECT_STR_EQ("WORLD", clipboard_text);
  EXPECT_STR_EQ("HELLO ", model.text());
  EXPECT_EQ(6U, model.cursor_pos());

  // Test for copy: Non-empty selection.
  model.Append(ASCIIToUTF16("HELLO WORLD"));
  model.SelectAll();
  model.Copy();
  clipboard->ReadText(ui::Clipboard::BUFFER_STANDARD, &clipboard_text);
  EXPECT_STR_EQ("HELLO HELLO WORLD", clipboard_text);
  EXPECT_STR_EQ("HELLO HELLO WORLD", model.text());
  EXPECT_EQ(17U, model.cursor_pos());

  // Test for paste.
  model.ClearSelection();
  model.MoveCursorToEnd(false);
  model.MoveCursorToPreviousWord(true);
  EXPECT_TRUE(model.Paste());
  clipboard->ReadText(ui::Clipboard::BUFFER_STANDARD, &clipboard_text);
  EXPECT_STR_EQ("HELLO HELLO WORLD", clipboard_text);
  EXPECT_STR_EQ("HELLO HELLO HELLO HELLO WORLD", model.text());
  EXPECT_EQ(29U, model.cursor_pos());
}

void SelectWordTestVerifier(TextfieldViewsModel &model,
    const std::string &expected_selected_string, size_t expected_cursor_pos) {
  EXPECT_STR_EQ(expected_selected_string, model.GetSelectedText());
  EXPECT_EQ(expected_cursor_pos, model.cursor_pos());
}

TEST_F(TextfieldViewsModelTest, SelectWordTest) {
  TextfieldViewsModel model(NULL);
  model.Append(ASCIIToUTF16("  HELLO  !!  WO     RLD "));

  // Test when cursor is at the beginning.
  model.MoveCursorToHome(false);
  model.SelectWord();
  SelectWordTestVerifier(model, "  ", 2U);

  // Test when cursor is at the beginning of a word.
  model.MoveCursorTo(2U, false);
  model.SelectWord();
  SelectWordTestVerifier(model, "HELLO", 7U);

  // Test when cursor is at the end of a word.
  model.MoveCursorTo(15U, false);
  model.SelectWord();
  SelectWordTestVerifier(model, "WO", 15U);

  // Test when cursor is somewhere in a non-alph-numeric fragment.
  for (size_t cursor_pos = 8; cursor_pos < 13U; cursor_pos++) {
    model.MoveCursorTo(cursor_pos, false);
    model.SelectWord();
    SelectWordTestVerifier(model, "  !!  ", 13U);
  }

  // Test when cursor is somewhere in a whitespace fragment.
  model.MoveCursorTo(17U, false);
  model.SelectWord();
  SelectWordTestVerifier(model, "     ", 20U);

  // Test when cursor is at the end.
  model.MoveCursorToEnd(false);
  model.SelectWord();
  SelectWordTestVerifier(model, " ", 24U);
}

TEST_F(TextfieldViewsModelTest, RangeTest) {
  TextfieldViewsModel model(NULL);
  model.Append(ASCIIToUTF16("HELLO WORLD"));
  model.MoveCursorToHome(false);
  ui::Range range;
  model.GetSelectedRange(&range);
  EXPECT_TRUE(range.is_empty());
  EXPECT_EQ(0U, range.start());
  EXPECT_EQ(0U, range.end());

  model.MoveCursorToNextWord(true);
  model.GetSelectedRange(&range);
  EXPECT_FALSE(range.is_empty());
  EXPECT_FALSE(range.is_reversed());
  EXPECT_EQ(0U, range.start());
  EXPECT_EQ(5U, range.end());

  model.MoveCursorLeft(true);
  model.GetSelectedRange(&range);
  EXPECT_FALSE(range.is_empty());
  EXPECT_EQ(0U, range.start());
  EXPECT_EQ(4U, range.end());

  model.MoveCursorToPreviousWord(true);
  model.GetSelectedRange(&range);
  EXPECT_TRUE(range.is_empty());
  EXPECT_EQ(0U, range.start());
  EXPECT_EQ(0U, range.end());

  // now from the end.
  model.MoveCursorToEnd(false);
  model.GetSelectedRange(&range);
  EXPECT_TRUE(range.is_empty());
  EXPECT_EQ(11U, range.start());
  EXPECT_EQ(11U, range.end());

  model.MoveCursorToPreviousWord(true);
  model.GetSelectedRange(&range);
  EXPECT_FALSE(range.is_empty());
  EXPECT_TRUE(range.is_reversed());
  EXPECT_EQ(11U, range.start());
  EXPECT_EQ(6U, range.end());

  model.MoveCursorRight(true);
  model.GetSelectedRange(&range);
  EXPECT_FALSE(range.is_empty());
  EXPECT_TRUE(range.is_reversed());
  EXPECT_EQ(11U, range.start());
  EXPECT_EQ(7U, range.end());

  model.MoveCursorToNextWord(true);
  model.GetSelectedRange(&range);
  EXPECT_TRUE(range.is_empty());
  EXPECT_EQ(11U, range.start());
  EXPECT_EQ(11U, range.end());

  // Select All
  model.MoveCursorToHome(true);
  model.GetSelectedRange(&range);
  EXPECT_FALSE(range.is_empty());
  EXPECT_TRUE(range.is_reversed());
  EXPECT_EQ(11U, range.start());
  EXPECT_EQ(0U, range.end());
}

TEST_F(TextfieldViewsModelTest, SelectRangeTest) {
  TextfieldViewsModel model(NULL);
  model.Append(ASCIIToUTF16("HELLO WORLD"));
  ui::Range range(0, 6);
  EXPECT_FALSE(range.is_reversed());
  model.SelectRange(range);
  EXPECT_STR_EQ("HELLO ", model.GetSelectedText());

  range = ui::Range(6, 1);
  EXPECT_TRUE(range.is_reversed());
  model.SelectRange(range);
  EXPECT_STR_EQ("ELLO ", model.GetSelectedText());

  range = ui::Range(2, 1000);
  EXPECT_FALSE(range.is_reversed());
  model.SelectRange(range);
  EXPECT_STR_EQ("LLO WORLD", model.GetSelectedText());

  range = ui::Range(1000, 3);
  EXPECT_TRUE(range.is_reversed());
  model.SelectRange(range);
  EXPECT_STR_EQ("LO WORLD", model.GetSelectedText());

  range = ui::Range(0, 0);
  EXPECT_TRUE(range.is_empty());
  model.SelectRange(range);
  EXPECT_TRUE(model.GetSelectedText().empty());

  range = ui::Range(3, 3);
  EXPECT_TRUE(range.is_empty());
  model.SelectRange(range);
  EXPECT_TRUE(model.GetSelectedText().empty());

  range = ui::Range(1000, 100);
  EXPECT_FALSE(range.is_empty());
  model.SelectRange(range);
  EXPECT_TRUE(model.GetSelectedText().empty());

  range = ui::Range(1000, 1000);
  EXPECT_TRUE(range.is_empty());
  model.SelectRange(range);
  EXPECT_TRUE(model.GetSelectedText().empty());
}

TEST_F(TextfieldViewsModelTest, CompositionTextTest) {
  TextfieldViewsModel model(this);
  model.Append(ASCIIToUTF16("1234590"));
  model.SelectRange(ui::Range(5, 5));
  EXPECT_FALSE(model.HasSelection());
  EXPECT_EQ(5U, model.cursor_pos());

  ui::Range range;
  model.GetTextRange(&range);
  EXPECT_EQ(0U, range.start());
  EXPECT_EQ(7U, range.end());

  ui::CompositionText composition;
  composition.text = ASCIIToUTF16("678");
  composition.underlines.push_back(ui::CompositionUnderline(0, 3, 0, false));
  composition.selection = ui::Range(2, 3);
  model.SetCompositionText(composition);
  EXPECT_TRUE(model.HasCompositionText());
  EXPECT_TRUE(model.HasSelection());

  model.GetTextRange(&range);
  EXPECT_EQ(10U, range.end());

  model.GetCompositionTextRange(&range);
  EXPECT_EQ(5U, range.start());
  EXPECT_EQ(8U, range.end());

  model.GetSelectedRange(&range);
  EXPECT_EQ(7U, range.start());
  EXPECT_EQ(8U, range.end());

  EXPECT_STR_EQ("1234567890", model.text());
  EXPECT_STR_EQ("8", model.GetSelectedText());
  EXPECT_STR_EQ("456", model.GetTextFromRange(ui::Range(3, 6)));

  TextfieldViewsModel::TextFragments fragments;
  model.GetFragments(&fragments);
  EXPECT_EQ(4U, fragments.size());
  EXPECT_EQ(0U, fragments[0].start);
  EXPECT_EQ(5U, fragments[0].end);
  EXPECT_FALSE(fragments[0].selected);
  EXPECT_FALSE(fragments[0].underline);
  EXPECT_EQ(5U, fragments[1].start);
  EXPECT_EQ(7U, fragments[1].end);
  EXPECT_FALSE(fragments[1].selected);
  EXPECT_TRUE(fragments[1].underline);
  EXPECT_EQ(7U, fragments[2].start);
  EXPECT_EQ(8U, fragments[2].end);
  EXPECT_TRUE(fragments[2].selected);
  EXPECT_TRUE(fragments[2].underline);
  EXPECT_EQ(8U, fragments[3].start);
  EXPECT_EQ(10U, fragments[3].end);
  EXPECT_FALSE(fragments[3].selected);
  EXPECT_FALSE(fragments[3].underline);

  EXPECT_FALSE(composition_text_confirmed_or_cleared_);
  model.ClearCompositionText();
  EXPECT_TRUE(composition_text_confirmed_or_cleared_);
  composition_text_confirmed_or_cleared_ = false;
  EXPECT_FALSE(model.HasCompositionText());
  EXPECT_FALSE(model.HasSelection());
  EXPECT_EQ(5U, model.cursor_pos());

  model.SetCompositionText(composition);
  EXPECT_STR_EQ("1234567890", model.text());
  EXPECT_TRUE(model.SetText(ASCIIToUTF16("1234567890")));
  EXPECT_TRUE(composition_text_confirmed_or_cleared_);
  composition_text_confirmed_or_cleared_ = false;
  model.MoveCursorToEnd(false);

  model.SetCompositionText(composition);
  EXPECT_STR_EQ("1234567890678", model.text());

  model.InsertText(UTF8ToUTF16("-"));
  EXPECT_TRUE(composition_text_confirmed_or_cleared_);
  composition_text_confirmed_or_cleared_ = false;
  EXPECT_STR_EQ("1234567890-", model.text());
  EXPECT_FALSE(model.HasCompositionText());
  EXPECT_FALSE(model.HasSelection());

  model.MoveCursorLeft(true);
  EXPECT_STR_EQ("-", model.GetSelectedText());
  model.SetCompositionText(composition);
  EXPECT_STR_EQ("1234567890678", model.text());

  model.ReplaceText(UTF8ToUTF16("-"));
  EXPECT_TRUE(composition_text_confirmed_or_cleared_);
  composition_text_confirmed_or_cleared_ = false;
  EXPECT_STR_EQ("1234567890-", model.text());
  EXPECT_FALSE(model.HasCompositionText());
  EXPECT_FALSE(model.HasSelection());

  model.SetCompositionText(composition);
  model.Append(UTF8ToUTF16("-"));
  EXPECT_TRUE(composition_text_confirmed_or_cleared_);
  composition_text_confirmed_or_cleared_ = false;
  EXPECT_STR_EQ("1234567890-678-", model.text());

  model.SetCompositionText(composition);
  model.Delete();
  EXPECT_TRUE(composition_text_confirmed_or_cleared_);
  composition_text_confirmed_or_cleared_ = false;
  EXPECT_STR_EQ("1234567890-678-", model.text());

  model.SetCompositionText(composition);
  model.Backspace();
  EXPECT_TRUE(composition_text_confirmed_or_cleared_);
  composition_text_confirmed_or_cleared_ = false;
  EXPECT_STR_EQ("1234567890-678-", model.text());

  model.SetText(string16());
  model.SetCompositionText(composition);
  model.MoveCursorLeft(false);
  EXPECT_TRUE(composition_text_confirmed_or_cleared_);
  composition_text_confirmed_or_cleared_ = false;
  EXPECT_STR_EQ("678", model.text());
  EXPECT_EQ(2U, model.cursor_pos());

  model.SetCompositionText(composition);
  model.MoveCursorRight(false);
  EXPECT_TRUE(composition_text_confirmed_or_cleared_);
  composition_text_confirmed_or_cleared_ = false;
  EXPECT_STR_EQ("676788", model.text());
  EXPECT_EQ(6U, model.cursor_pos());

  model.SetCompositionText(composition);
  model.MoveCursorToPreviousWord(false);
  EXPECT_TRUE(composition_text_confirmed_or_cleared_);
  composition_text_confirmed_or_cleared_ = false;
  EXPECT_STR_EQ("676788678", model.text());

  model.SetText(string16());
  model.SetCompositionText(composition);
  model.MoveCursorToNextWord(false);
  EXPECT_TRUE(composition_text_confirmed_or_cleared_);
  composition_text_confirmed_or_cleared_ = false;

  model.SetCompositionText(composition);
  model.MoveCursorToHome(true);
  EXPECT_TRUE(composition_text_confirmed_or_cleared_);
  composition_text_confirmed_or_cleared_ = false;
  EXPECT_STR_EQ("678678", model.text());

  model.SetCompositionText(composition);
  model.MoveCursorToEnd(false);
  EXPECT_TRUE(composition_text_confirmed_or_cleared_);
  composition_text_confirmed_or_cleared_ = false;
  EXPECT_STR_EQ("678", model.text());

  model.SetCompositionText(composition);
  model.MoveCursorTo(0, true);
  EXPECT_TRUE(composition_text_confirmed_or_cleared_);
  composition_text_confirmed_or_cleared_ = false;
  EXPECT_STR_EQ("678678", model.text());

  model.SetCompositionText(composition);
  model.SelectRange(ui::Range(0, 3));
  EXPECT_TRUE(composition_text_confirmed_or_cleared_);
  composition_text_confirmed_or_cleared_ = false;
  EXPECT_STR_EQ("678", model.text());

  model.SetCompositionText(composition);
  model.SelectAll();
  EXPECT_TRUE(composition_text_confirmed_or_cleared_);
  composition_text_confirmed_or_cleared_ = false;
  EXPECT_STR_EQ("678", model.text());

  model.SetCompositionText(composition);
  model.SelectWord();
  EXPECT_TRUE(composition_text_confirmed_or_cleared_);
  composition_text_confirmed_or_cleared_ = false;
  EXPECT_STR_EQ("678", model.text());

  model.SetCompositionText(composition);
  model.ClearSelection();
  EXPECT_TRUE(composition_text_confirmed_or_cleared_);
  composition_text_confirmed_or_cleared_ = false;

  model.SetCompositionText(composition);
  EXPECT_FALSE(model.Cut());
  EXPECT_FALSE(composition_text_confirmed_or_cleared_);
}

}  // namespace views
