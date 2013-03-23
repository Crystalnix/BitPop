// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/location_bar/action_box_button_view.h"

#include "base/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/toolbar/action_box_menu_model.h"
#include "chrome/browser/ui/view_ids.h"
#include "chrome/browser/ui/views/action_box_menu.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "ui/base/accessibility/accessible_view_state.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/path.h"

// static
const int ActionBoxButtonView::kBorderOverlap = 2;

ActionBoxButtonView::ActionBoxButtonView(Browser* browser,
                                         const gfx::Point& menu_offset)
    : views::MenuButton(NULL, string16(), this, false),
      browser_(browser),
      menu_offset_(menu_offset),
      ALLOW_THIS_IN_INITIALIZER_LIST(controller_(browser, this)) {
  set_id(VIEW_ID_ACTION_BOX_BUTTON);
  SetTooltipText(l10n_util::GetStringUTF16(IDS_TOOLTIP_ACTION_BOX_BUTTON));
  SetIcon(*ui::ResourceBundle::GetSharedInstance().GetImageSkiaNamed(
      IDR_ACTION_BOX_BUTTON_NORMAL));
  SetHoverIcon(*ui::ResourceBundle::GetSharedInstance().GetImageSkiaNamed(
      IDR_ACTION_BOX_BUTTON_HOVER));
  SetPushedIcon(*ui::ResourceBundle::GetSharedInstance().GetImageSkiaNamed(
      IDR_ACTION_BOX_BUTTON_PUSHED));
  set_accessibility_focusable(true);
  set_border(NULL);
  SizeToPreferredSize();
}

ActionBoxButtonView::~ActionBoxButtonView() {
}

void ActionBoxButtonView::GetAccessibleState(ui::AccessibleViewState* state) {
  MenuButton::GetAccessibleState(state);
  state->name = l10n_util::GetStringUTF16(IDS_ACCNAME_ACTION_BOX_BUTTON);
}

void ActionBoxButtonView::OnMenuButtonClicked(View* source,
                                              const gfx::Point& point) {
  controller_.OnButtonClicked();
}

bool ActionBoxButtonView::HasHitTestMask() const {
  return true;
}

void ActionBoxButtonView::GetHitTestMask(gfx::Path* mask) const {
  SkRect clickable_rect;
  clickable_rect.iset(0, kBorderOverlap, width(), height() - kBorderOverlap);
  mask->addRect(clickable_rect);
}

void ActionBoxButtonView::ShowMenu(scoped_ptr<ActionBoxMenuModel> menu_model) {
  menu_ = ActionBoxMenu::Create(browser_, menu_model.Pass());
  menu_->RunMenu(this, menu_offset_);
}
