// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views/controls/menu/menu_model_adapter.h"

#include "base/logging.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/menu_model.h"
#include "views/controls/menu/submenu_view.h"
#include "views/views_delegate.h"

namespace views {

MenuModelAdapter::MenuModelAdapter(ui::MenuModel* menu_model)
    : menu_model_(menu_model) {
  DCHECK(menu_model);
}

MenuModelAdapter::~MenuModelAdapter() {
}

void MenuModelAdapter::BuildMenu(MenuItemView* menu) {
  DCHECK(menu);

  // Clear the menu.
  if (menu->HasSubmenu()) {
    const int subitem_count = menu->GetSubmenu()->child_count();
    for (int i = 0; i < subitem_count; ++i)
      menu->RemoveMenuItemAt(0);
  }

  menu_map_.clear();
  menu_map_[menu] = menu_model_;

  // Repopulate the menu.
  BuildMenuImpl(menu, menu_model_);
  menu->ChildrenChanged();
}

// MenuModelAdapter, MenuDelegate implementation:

void MenuModelAdapter::ExecuteCommand(int id) {
  ui::MenuModel* model = menu_model_;
  int index = 0;
  if (ui::MenuModel::GetModelAndIndexForCommandId(id, &model, &index)) {
    model->ActivatedAt(index);
    return;
  }

  NOTREACHED();
}

void MenuModelAdapter::ExecuteCommand(int id, int mouse_event_flags) {
  ui::MenuModel* model = menu_model_;
  int index = 0;
  if (ui::MenuModel::GetModelAndIndexForCommandId(id, &model, &index)) {
    const int disposition =
        ViewsDelegate::views_delegate->GetDispositionForEvent(
            mouse_event_flags);
    model->ActivatedAtWithDisposition(index, disposition);
    return;
  }

  NOTREACHED();
}

bool MenuModelAdapter::GetAccelerator(int id,
                                      views::Accelerator* accelerator) {
  ui::MenuModel* model = menu_model_;
  int index = 0;
  if (ui::MenuModel::GetModelAndIndexForCommandId(id, &model, &index))
    return model->GetAcceleratorAt(index, accelerator);

  NOTREACHED();
  return false;
}

std::wstring MenuModelAdapter::GetLabel(int id) const {
  ui::MenuModel* model = menu_model_;
  int index = 0;
  if (ui::MenuModel::GetModelAndIndexForCommandId(id, &model, &index))
    return UTF16ToWide(model->GetLabelAt(index));

  NOTREACHED();
  return std::wstring();
}

const gfx::Font& MenuModelAdapter::GetLabelFont(int id) const {
  ui::MenuModel* model = menu_model_;
  int index = 0;
  if (ui::MenuModel::GetModelAndIndexForCommandId(id, &model, &index)) {
    const gfx::Font* font = model->GetLabelFontAt(index);
    return font ? *font : MenuDelegate::GetLabelFont(id);
  }

  NOTREACHED();
  return MenuDelegate::GetLabelFont(id);
}

bool MenuModelAdapter::IsCommandEnabled(int id) const {
  ui::MenuModel* model = menu_model_;
  int index = 0;
  if (ui::MenuModel::GetModelAndIndexForCommandId(id, &model, &index))
    return model->IsEnabledAt(index);

  NOTREACHED();
  return false;
}

bool MenuModelAdapter::IsItemChecked(int id) const {
  ui::MenuModel* model = menu_model_;
  int index = 0;
  if (ui::MenuModel::GetModelAndIndexForCommandId(id, &model, &index))
    return model->IsItemCheckedAt(index);

  NOTREACHED();
  return false;
}

void MenuModelAdapter::SelectionChanged(MenuItemView* menu) {
  const int id = menu->GetCommand();
  ui::MenuModel* model = menu_model_;
  int index = 0;
  if (ui::MenuModel::GetModelAndIndexForCommandId(id, &model, &index)) {
    model->HighlightChangedTo(index);
    return;
  }

  NOTREACHED();
}

void MenuModelAdapter::WillShowMenu(MenuItemView* menu) {
  // Look up the menu model for this menu.
  const std::map<MenuItemView*, ui::MenuModel*>::const_iterator map_iterator =
      menu_map_.find(menu);
  if (map_iterator != menu_map_.end()) {
    map_iterator->second->MenuWillShow();
    return;
  }

  NOTREACHED();
}

// MenuModelAdapter, private:

void MenuModelAdapter::BuildMenuImpl(MenuItemView* menu, ui::MenuModel* model) {
  DCHECK(menu);
  DCHECK(model);
  const int item_count = model->GetItemCount();
  for (int i = 0; i < item_count; ++i) {
    const int index = i + model->GetFirstItemIndex(NULL);
    MenuItemView* item = menu->AppendMenuItemFromModel(
        model, index, model->GetCommandIdAt(index));

    if (model->GetTypeAt(index) == ui::MenuModel::TYPE_SUBMENU) {
      DCHECK(item);
      DCHECK_EQ(MenuItemView::SUBMENU, item->GetType());
      ui::MenuModel* submodel = model->GetSubmenuModelAt(index);
      DCHECK(submodel);
      BuildMenuImpl(item, submodel);

      menu_map_[item] = submodel;
    }
  }

  menu->set_has_icons(model->HasIcons());
}

}  // views
