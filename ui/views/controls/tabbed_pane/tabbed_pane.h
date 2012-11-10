// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_CONTROLS_TABBED_PANE_TABBED_PANE_H_
#define UI_VIEWS_CONTROLS_TABBED_PANE_TABBED_PANE_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/string16.h"
#include "ui/views/view.h"

namespace views {

class NativeTabbedPaneWrapper;
class TabbedPaneListener;

// TabbedPane is a view that shows tabs. When the user clicks on a tab, the
// associated view is displayed.
class VIEWS_EXPORT TabbedPane : public View {
 public:
  TabbedPane();
  virtual ~TabbedPane();

  TabbedPaneListener* listener() const { return listener_; }
  void set_listener(TabbedPaneListener* listener) { listener_ = listener; }
#if defined(OS_WIN) && !defined(USE_AURA)
  bool use_native_win_control() { return use_native_win_control_; }
  void set_use_native_win_control(bool use_native_win_control) {
    use_native_win_control_ = use_native_win_control;
  }
#endif

  // Returns the number of tabs.
  int GetTabCount();

  // Returns the index of the selected tab.
  int GetSelectedTabIndex();

  // Returns the contents of the selected tab.
  View* GetSelectedTab();

  // Adds a new tab at the end of this TabbedPane with the specified |title|.
  // |contents| is the view displayed when the tab is selected and is owned by
  // the TabbedPane.
  void AddTab(const string16& title, View* contents);

  // Adds a new tab at |index| with |title|.
  // |contents| is the view displayed when the tab is selected and is owned by
  // the TabbedPane. If |select_if_first_tab| is true and the tabbed pane is
  // currently empty, the new tab is selected. If you pass in false for
  // |select_if_first_tab| you need to explicitly invoke SelectTabAt, otherwise
  // the tabbed pane will not have a valid selection.
  void AddTabAtIndex(int index,
                     const string16& title,
                     View* contents,
                     bool select_if_first_tab);

  // Removes the tab at |index| and returns the associated content view.
  // The caller becomes the owner of the returned view.
  View* RemoveTabAtIndex(int index);

  // Selects the tab at |index|, which must be valid.
  void SelectTabAt(int index);

  void SetAccessibleName(const string16& name);

  // Overridden from View:
  virtual gfx::Size GetPreferredSize() OVERRIDE;

 protected:
  // The object that actually implements the tabbed-pane.
  // Protected for tests access.
  NativeTabbedPaneWrapper* native_tabbed_pane_;

 private:
  // The tabbed-pane's class name.
  static const char kViewClassName[];

  // We support Ctrl+Tab and Ctrl+Shift+Tab to navigate tabbed option pages.
  void LoadAccelerators();

  // Overridden from View:
  virtual void Layout() OVERRIDE;
  virtual void ViewHierarchyChanged(bool is_add,
                                    View* parent,
                                    View* child) OVERRIDE;
  // Handles Ctrl+Tab and Ctrl+Shift+Tab navigation of pages.
  virtual bool AcceleratorPressed(const ui::Accelerator& accelerator) OVERRIDE;
  virtual std::string GetClassName() const OVERRIDE;
  virtual void OnFocus() OVERRIDE;
  virtual void OnPaintFocusBorder(gfx::Canvas* canvas) OVERRIDE;
  virtual void GetAccessibleState(ui::AccessibleViewState* state) OVERRIDE;

#if defined(OS_WIN) && !defined(USE_AURA)
  bool use_native_win_control_;
#endif

  // Our listener. Not owned. Notified when tab selection changes.
  TabbedPaneListener* listener_;

  // The accessible name of this tabbed pane.
  string16 accessible_name_;

  DISALLOW_COPY_AND_ASSIGN(TabbedPane);
};

}  // namespace views

#endif  // UI_VIEWS_CONTROLS_TABBED_PANE_TABBED_PANE_H_
