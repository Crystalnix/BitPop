// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_TABS_TAB_STRIP_MODEL_ORDER_CONTROLLER_H_
#define CHROME_BROWSER_UI_TABS_TAB_STRIP_MODEL_ORDER_CONTROLLER_H_

#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/common/page_transition_types.h"

class TabContents;

///////////////////////////////////////////////////////////////////////////////
// TabStripModelOrderController
//
//  An object that allows different types of ordering and reselection to be
//  heuristics plugged into a TabStripModel.
//
class TabStripModelOrderController : public TabStripModelObserver {
 public:
  explicit TabStripModelOrderController(TabStripModel* tabstrip);
  virtual ~TabStripModelOrderController();

  // Sets the insertion policy. Default is INSERT_AFTER.
  void set_insertion_policy(TabStripModel::InsertionPolicy policy) {
    insertion_policy_ = policy;
  }
  TabStripModel::InsertionPolicy insertion_policy() const {
    return insertion_policy_;
  }

  // Determine where to place a newly opened tab by using the supplied
  // transition and foreground flag to figure out how it was opened.
  int DetermineInsertionIndex(TabContents* new_contents,
                              content::PageTransition transition,
                              bool foreground);

  // Returns the index to append tabs at.
  int DetermineInsertionIndexForAppending();

  // Determine where to shift selection after a tab is closed.
  int DetermineNewSelectedIndex(int removed_index) const;

  // Overridden from TabStripModelObserver:
  virtual void ActiveTabChanged(TabContents* old_contents,
                                TabContents* new_contents,
                                int index,
                                bool user_gesture) OVERRIDE;

 private:
  // Returns a valid index to be selected after the tab at |removing_index| is
  // closed. If |index| is after |removing_index|, |index| is adjusted to
  // reflect the fact that |removing_index| is going away.
  int GetValidIndex(int index, int removing_index) const;

  TabStripModel* tabstrip_;

  TabStripModel::InsertionPolicy insertion_policy_;

  DISALLOW_COPY_AND_ASSIGN(TabStripModelOrderController);
};

#endif  // CHROME_BROWSER_UI_TABS_TAB_STRIP_MODEL_ORDER_CONTROLLER_H_
