// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VIEWS_CONTROLS_MENU_NATIVE_MENU_X_H_
#define VIEWS_CONTROLS_MENU_NATIVE_MENU_X_H_
#pragma once

#include "views/controls/menu/menu_delegate.h"
#include "views/controls/menu/menu_item_view.h"
#include "views/controls/menu/menu_wrapper.h"

namespace ui {
class MenuModel;
}

namespace views {

// A non-GTK implementation of MenuWrapper, used currently for touchui.
class NativeMenuX : public MenuWrapper,
                    public MenuDelegate {
 public:
  explicit NativeMenuX(Menu2* menu);
  virtual ~NativeMenuX();

  // Overridden from MenuWrapper:
  virtual void RunMenuAt(const gfx::Point& point, int alignment);
  virtual void CancelMenu();
  virtual void Rebuild();
  virtual void UpdateStates();
  virtual gfx::NativeMenu GetNativeMenu() const;
  virtual MenuAction GetMenuAction() const;
  virtual void AddMenuListener(MenuListener* listener);
  virtual void RemoveMenuListener(MenuListener* listener);
  virtual void SetMinimumWidth(int width);

  // Overridden from MenuDelegate:
  virtual bool IsItemChecked(int id) const;
  virtual bool IsCommandEnabled(int id) const;
  virtual void ExecuteCommand(int id);
  virtual bool GetAccelerator(int id, views::Accelerator* accelerator);

 private:
  void AddMenuItemsFromModel(MenuItemView* parent, ui::MenuModel* model);
  void UpdateMenuFromModel(SubmenuView* menu, ui::MenuModel* model);

  // The attached model and delegate. Does not assume ownership.
  ui::MenuModel* model_;
  scoped_ptr<MenuItemView> root_;

  DISALLOW_COPY_AND_ASSIGN(NativeMenuX);
};

}  // namespace views

#endif  // VIEWS_CONTROLS_MENU_NATIVE_MENU_X_H_
