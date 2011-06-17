// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_AUTOCOMPLETE_AUTOCOMPLETE_POPUP_MODEL_H_
#define CHROME_BROWSER_AUTOCOMPLETE_AUTOCOMPLETE_POPUP_MODEL_H_
#pragma once

#include "base/memory/scoped_ptr.h"
#include "chrome/browser/autocomplete/autocomplete.h"
#include "chrome/browser/autocomplete/autocomplete_edit.h"

class AutocompleteEditView;
class AutocompletePopupView;
class Profile;
class SkBitmap;

class AutocompletePopupModel {
 public:
  AutocompletePopupModel(AutocompletePopupView* popup_view,
                         AutocompleteEditModel* edit_model,
                         Profile* profile);
  ~AutocompletePopupModel();

  // Invoked when the profile has changed.
  void set_profile(Profile* profile) { profile_ = profile; }

  // TODO(sky): see about removing this.
  Profile* profile() const { return profile_; }

  // Returns true if the popup is currently open.
  bool IsOpen() const;

  AutocompletePopupView* view() const { return view_; }

  // Returns the AutocompleteController used by this popup.
  AutocompleteController* autocomplete_controller() const {
    return edit_model_->autocomplete_controller();
  }

  const AutocompleteResult& result() const {
    return autocomplete_controller()->result();
  }

  size_t hovered_line() const {
    return hovered_line_;
  }

  // Call to change the hovered line.  |line| should be within the range of
  // valid lines (to enable hover) or kNoMatch (to disable hover).
  void SetHoveredLine(size_t line);

  size_t selected_line() const {
    return selected_line_;
  }

  // Call to change the selected line.  This will update all state and repaint
  // the necessary parts of the window, as well as updating the edit with the
  // new temporary text.  |line| will be clamped to the range of valid lines.
  // |reset_to_default| is true when the selection is being reset back to the
  // default match, and thus there is no temporary text (and no
  // |manually_selected_match_|). If |force| is true then the selected line will
  // be updated forcibly even if the |line| is same as the current selected
  // line.
  // NOTE: This assumes the popup is open, and thus both old and new values for
  // the selected line should not be kNoMatch.
  void SetSelectedLine(size_t line, bool reset_to_default, bool force);

  // Called when the user hits escape after arrowing around the popup.  This
  // will change the selected line back to the default match and redraw.
  void ResetToDefaultMatch();

  // Gets the selected keyword or keyword hint for the given match. If the match
  // is already keyword, then the keyword will be returned directly. Otherwise,
  // it returns GetKeywordForText(match.fill_into_edit, keyword).
  bool GetKeywordForMatch(const AutocompleteMatch& match,
                          string16* keyword) const;

  // Gets the selected keyword or keyword hint for the given text. Returns
  // true if |keyword| represents a keyword hint, or false if |keyword|
  // represents a selected keyword. (|keyword| will always be set [though
  // possibly to the empty string], and you cannot have both a selected keyword
  // and a keyword hint simultaneously.)
  bool GetKeywordForText(const string16& text, string16* keyword) const;

  // Immediately updates and opens the popup if necessary, then moves the
  // current selection down (|count| > 0) or up (|count| < 0), clamping to the
  // first or last result if necessary.  If |count| == 0, the selection will be
  // unchanged, but the popup will still redraw and modify the text in the
  // AutocompleteEditModel.
  void Move(int count);

  // Called when the user hits shift-delete.  This should determine if the item
  // can be removed from history, and if so, remove it and update the popup.
  void TryDeletingCurrentItem();

  // If |match| is from an extension, returns the extension icon; otherwise
  // returns NULL.
  const SkBitmap* GetIconIfExtensionMatch(const AutocompleteMatch& match) const;

  // The match the user has manually chosen, if any.
  const AutocompleteResult::Selection& manually_selected_match() const {
    return manually_selected_match_;
  }

  // Invoked from the edit model any time the result set of the controller
  // changes.
  void OnResultChanged();

  // The token value for selected_line_, hover_line_ and functions dealing with
  // a "line number" that indicates "no line".
  static const size_t kNoMatch = -1;

 private:
  AutocompletePopupView* view_;

  AutocompleteEditModel* edit_model_;

  // Profile for current tab.
  Profile* profile_;

  // The line that's currently hovered.  If we're not drawing a hover rect,
  // this will be kNoMatch, even if the cursor is over the popup contents.
  size_t hovered_line_;

  // The currently selected line.  This is kNoMatch when nothing is selected,
  // which should only be true when the popup is closed.
  size_t selected_line_;

  // The match the user has manually chosen, if any.
  AutocompleteResult::Selection manually_selected_match_;

  DISALLOW_COPY_AND_ASSIGN(AutocompletePopupModel);
};

#endif  // CHROME_BROWSER_AUTOCOMPLETE_AUTOCOMPLETE_POPUP_MODEL_H_
