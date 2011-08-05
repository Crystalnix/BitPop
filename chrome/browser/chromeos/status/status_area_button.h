// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_STATUS_STATUS_AREA_BUTTON_H_
#define CHROME_BROWSER_CHROMEOS_STATUS_STATUS_AREA_BUTTON_H_
#pragma once

#include "chrome/browser/chromeos/status/status_area_host.h"
#include "views/controls/button/menu_button.h"
#include "views/controls/menu/view_menu_delegate.h"

namespace chromeos {

// Button to be used to represent status and allow menus to be popped up.
// Shows current button state by drawing a border around the current icon.
class StatusAreaButton : public views::MenuButton {
 public:
  explicit StatusAreaButton(StatusAreaHost* host,
                            views::ViewMenuDelegate* menu_delegate);
  virtual ~StatusAreaButton() {}
  virtual void PaintButton(gfx::Canvas* canvas, PaintButtonMode mode);

  // Overrides TextButton's SetText to clear max text size before seting new
  // text content so that the button size would fit the new text size.
  virtual void SetText(const std::wstring& text);

  void set_use_menu_button_paint(bool use_menu_button_paint) {
    use_menu_button_paint_ = use_menu_button_paint;
  }

  // views::MenuButton overrides.
  virtual bool Activate() OVERRIDE;

  // View overrides.
  virtual gfx::Size GetPreferredSize() OVERRIDE;
  virtual gfx::Insets GetInsets() const OVERRIDE;
  virtual void OnThemeChanged() OVERRIDE;
  virtual bool HitTest(const gfx::Point& l) const OVERRIDE;

  // Controls whether or not this status area button is able to be pressed.
  void set_active(bool active) { active_ = active; }
  bool active() const { return active_; }

 protected:
  // Subclasses should override these methods to return the correct dimensions.
  virtual int icon_height();
  virtual int icon_width();

  // Subclasses can override this method to return more or less padding.
  // The padding is added to both the left and right side.
  virtual int horizontal_padding();

  // True if the button wants to use views::MenuButton drawings.
  bool use_menu_button_paint_;

  // Insets to use for this button.
  gfx::Insets insets_;

  // Indicates when this button can be pressed.  Independent of
  // IsEnabled state, so that when IsEnabled is true, this can still
  // be false, and vice versa.
  bool active_;

  // The status area host,
  StatusAreaHost* host_;

 private:
  void UpdateTextStyle();

  DISALLOW_COPY_AND_ASSIGN(StatusAreaButton);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_STATUS_STATUS_AREA_BUTTON_H_
