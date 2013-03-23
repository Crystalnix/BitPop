// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/action_box_menu.h"

#include "chrome/browser/ui/toolbar/action_box_menu_model.h"
#include "chrome/browser/ui/views/browser_action_view.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/controls/button/menu_button.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/view.h"

// static
scoped_ptr<ActionBoxMenu> ActionBoxMenu::Create(
    Browser* browser,
    scoped_ptr<ActionBoxMenuModel> model) {
  scoped_ptr<ActionBoxMenu> menu(new ActionBoxMenu(browser, model.Pass()));
  menu->PopulateMenu();
  return menu.Pass();
}

ActionBoxMenu::~ActionBoxMenu() {
}

void ActionBoxMenu::RunMenu(views::MenuButton* menu_button,
                            gfx::Point menu_offset) {
  views::View::ConvertPointToScreen(menu_button, &menu_offset);

  // Ignore the result since we don't need to handle a deleted menu specially.
  ignore_result(
      menu_runner_->RunMenuAt(menu_button->GetWidget(),
                              menu_button,
                              gfx::Rect(menu_offset, menu_button->size()),
                              views::MenuItemView::TOPRIGHT,
                              views::MenuRunner::HAS_MNEMONICS));
}

ActionBoxMenu::ActionBoxMenu(Browser* browser,
                             scoped_ptr<ActionBoxMenuModel> model)
    : browser_(browser),
      model_(model.Pass()) {
  views::MenuItemView* menu = new views::MenuItemView(this);
  menu->set_has_icons(true);

  menu_runner_.reset(new views::MenuRunner(menu));
}

void ActionBoxMenu::ExecuteCommand(int id) {
  model_->ExecuteCommand(id);
}

void ActionBoxMenu::InspectPopup(ExtensionAction* action) {
}

int ActionBoxMenu::GetCurrentTabId() const {
  return 0;
}

void ActionBoxMenu::OnBrowserActionExecuted(BrowserActionButton* button) {
}

void ActionBoxMenu::OnBrowserActionVisibilityChanged() {
}

gfx::Point ActionBoxMenu::GetViewContentOffset() const {
  return gfx::Point(0, 0);
}

bool ActionBoxMenu::NeedToShowMultipleIconStates() const {
  return false;
}

bool ActionBoxMenu::NeedToShowTooltip() const {
  return false;
}

void ActionBoxMenu::WriteDragDataForView(views::View* sender,
                                         const gfx::Point& press_pt,
                                         ui::OSExchangeData* data) {
}

int ActionBoxMenu::GetDragOperationsForView(views::View* sender,
                                            const gfx::Point& p) {
  return 0;
}

bool ActionBoxMenu::CanStartDragForView(views::View* sender,
                                        const gfx::Point& press_pt,
                                        const gfx::Point& p) {
  return false;
}

void ActionBoxMenu::PopulateMenu() {
  for (int model_index = 0; model_index < model_->GetItemCount();
       ++model_index) {
    views::MenuItemView* menu_item =
        menu_runner_->GetMenu()->AppendMenuItemFromModel(
            model_.get(), model_index, model_->GetCommandIdAt(model_index));
    if (model_->GetTypeAt(model_index) == ui::MenuModel::TYPE_COMMAND) {
      if (model_->IsItemExtension(model_index)) {
        menu_item->SetMargins(0, 0);
        const extensions::Extension* extension =
            model_->GetExtensionAt(model_index);
        BrowserActionView* view = new BrowserActionView(extension,
            browser_, this);
        // |menu_item| will own the |view| from now on.
        menu_item->SetIconView(view);
      }
    }
  }
}
