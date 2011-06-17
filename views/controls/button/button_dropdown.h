// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VIEWS_CONTROLS_BUTTON_BUTTON_DROPDOWN_H_
#define VIEWS_CONTROLS_BUTTON_BUTTON_DROPDOWN_H_
#pragma once

#include "base/task.h"
#include "views/controls/button/image_button.h"
#include "views/controls/menu/menu_2.h"

namespace views {

////////////////////////////////////////////////////////////////////////////////
//
// ButtonDropDown
//
// A button class that when pressed (and held) or pressed (and drag down) will
// display a menu
//
////////////////////////////////////////////////////////////////////////////////
class ButtonDropDown : public ImageButton {
 public:
  ButtonDropDown(ButtonListener* listener, ui::MenuModel* model);
  virtual ~ButtonDropDown();

  // Overridden from views::View
  virtual bool OnMousePressed(const MouseEvent& event) OVERRIDE;
  virtual bool OnMouseDragged(const MouseEvent& event) OVERRIDE;
  virtual void OnMouseReleased(const MouseEvent& event) OVERRIDE;
  // Showing the drop down results in a MouseCaptureLost, we need to ignore it.
  virtual void OnMouseCaptureLost() OVERRIDE {}
  virtual void OnMouseExited(const MouseEvent& event) OVERRIDE;
  // Display the right-click menu, as triggered by the keyboard, for instance.
  // Using the member function ShowDropDownMenu for the actual display.
  virtual void ShowContextMenu(const gfx::Point& p,
                               bool is_mouse_gesture) OVERRIDE;
  virtual void GetAccessibleState(ui::AccessibleViewState* state) OVERRIDE;

 protected:
  // Overridden from CustomButton. Returns true if the button should become
  // pressed when a user holds the mouse down over the button. For this
  // implementation, both left and right mouse buttons can trigger a change
  // to the PUSHED state.
  virtual bool ShouldEnterPushedState(const MouseEvent& event) OVERRIDE;

 private:
  // Internal function to show the dropdown menu
  void ShowDropDownMenu(gfx::NativeView window);

  // The model that populates the attached menu.
  ui::MenuModel* model_;
  scoped_ptr<Menu2> menu_;

  // Y position of mouse when left mouse button is pressed
  int y_position_on_lbuttondown_;

  // A factory for tasks that show the dropdown context menu for the button.
  ScopedRunnableMethodFactory<ButtonDropDown> show_menu_factory_;

  DISALLOW_COPY_AND_ASSIGN(ButtonDropDown);
};

}  // namespace views

#endif  // VIEWS_CONTROLS_BUTTON_BUTTON_DROPDOWN_H_
