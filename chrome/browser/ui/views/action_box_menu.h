// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_ACTION_BOX_MENU_H_
#define CHROME_BROWSER_UI_VIEWS_ACTION_BOX_MENU_H_

#include <map>

#include "base/memory/scoped_ptr.h"
#include "base/memory/scoped_vector.h"
#include "chrome/browser/ui/views/browser_action_view.h"
#include "ui/views/controls/menu/menu_delegate.h"

class ActionBoxButtonController;
class ActionBoxMenuModel;

namespace views {
class Background;
class Border;
class MenuItemView;
class MenuRunner;
class View;
}

// ActionBoxMenu adapts the ActionBoxMenuModel to view's menu related classes.
class ActionBoxMenu : public views::MenuDelegate,
                      public BrowserActionView::Delegate {
 public:
  // Constructs and initializes an ActionBoxMenu.
  static scoped_ptr<ActionBoxMenu> Create(
      Browser* browser,
      scoped_ptr<ActionBoxMenuModel> model);

  virtual ~ActionBoxMenu();

  // Shows the menu relative to the specified button.
  void RunMenu(views::MenuButton* menu_button, gfx::Point menu_offset);

 private:
  ActionBoxMenu(Browser* browser, scoped_ptr<ActionBoxMenuModel> model);

  // Overridden from views::MenuDelegate:
  virtual void ExecuteCommand(int id) OVERRIDE;

  // Overridden from BrowserActionView::Delegate and DragController overrides:
  virtual void InspectPopup(ExtensionAction* button) OVERRIDE;
  virtual int GetCurrentTabId() const OVERRIDE;
  virtual void OnBrowserActionExecuted(BrowserActionButton* button) OVERRIDE;
  virtual void OnBrowserActionVisibilityChanged() OVERRIDE;
  virtual gfx::Point GetViewContentOffset() const OVERRIDE;
  virtual bool NeedToShowMultipleIconStates() const OVERRIDE;
  virtual bool NeedToShowTooltip() const OVERRIDE;
  virtual void WriteDragDataForView(views::View* sender,
                                    const gfx::Point& press_pt,
                                    ui::OSExchangeData* data) OVERRIDE;
  virtual int GetDragOperationsForView(views::View* sender,
                                       const gfx::Point& p) OVERRIDE;
  virtual bool CanStartDragForView(views::View* sender,
                                   const gfx::Point& press_pt,
                                   const gfx::Point& p) OVERRIDE;

  // Populates |root_| with all the child menu items from the |model_|.
  void PopulateMenu();

  Browser* browser_;

  scoped_ptr<views::MenuRunner> menu_runner_;

  // The model that tracks the order of the toolbar icons.
  scoped_ptr<ActionBoxMenuModel> model_;

  DISALLOW_COPY_AND_ASSIGN(ActionBoxMenu);
};

#endif  // CHROME_BROWSER_UI_VIEWS_ACTION_BOX_MENU_H_
