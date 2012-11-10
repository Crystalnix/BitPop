// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/shell.h"
#include "ash/shell_factory.h"
#include "ash/wm/coordinate_conversion.h"
#include "ash/wm/window_properties.h"
#include "ui/aura/root_window.h"
#include "ui/aura/ui_controls_aura.h"
#include "ui/gfx/screen.h"
#include "ui/ui_controls/ui_controls_aura.h"

namespace ash {
namespace internal {
namespace {

// Returns the UIControls object for RootWindow.
// kUIControlsKey is owned property and UIControls object
// will be deleted when the root window is deleted.
ui_controls::UIControlsAura* GetUIControlsForRootWindow(
    aura::RootWindow* root_window) {
  ui_controls::UIControlsAura* native_ui_control =
      root_window->GetProperty(kUIControlsKey);
  if (!native_ui_control) {
    native_ui_control = aura::CreateUIControlsAura(root_window);
    // Pass the ownership to the |root_window|.
    root_window->SetProperty(kUIControlsKey, native_ui_control);
  }
  return native_ui_control;
}

// Returns the UIControls object for the RootWindow at the |point| in
// absolute screen coordinates.  NULL if there is no RootWindow under the
// |point|.
ui_controls::UIControlsAura* GetUIControlsAt(const gfx::Point& point) {
  aura::RootWindow* root = wm::GetRootWindowAt(point);
  return root ? GetUIControlsForRootWindow(root) : NULL;
}

}  // namespace

class UIControlsAsh : public ui_controls::UIControlsAura {
 public:
  UIControlsAsh() {
  }
  virtual ~UIControlsAsh() {
  }

  // ui_controls::UIControslAura overrides:
  virtual bool SendKeyPress(gfx::NativeWindow window,
                            ui::KeyboardCode key,
                            bool control,
                            bool shift,
                            bool alt,
                            bool command) OVERRIDE {
    return SendKeyPressNotifyWhenDone(
        window, key, control, shift, alt, command, base::Closure());
  }

  virtual bool SendKeyPressNotifyWhenDone(
      gfx::NativeWindow window,
      ui::KeyboardCode key,
      bool control,
      bool shift,
      bool alt,
      bool command,
      const base::Closure& closure) OVERRIDE {
    aura::RootWindow* root =
        window ? window->GetRootWindow() : Shell::GetActiveRootWindow();
    ui_controls::UIControlsAura* ui_controls = GetUIControlsForRootWindow(root);
    return ui_controls && ui_controls->SendKeyPressNotifyWhenDone(
        window, key, control, shift, alt, command, closure);
  }

  virtual bool SendMouseMove(long x, long y) OVERRIDE {
    ui_controls::UIControlsAura* ui_controls =
        GetUIControlsAt(gfx::Point(x, y));
    return ui_controls && ui_controls->SendMouseMove(x, y);
  }

  virtual bool SendMouseMoveNotifyWhenDone(
      long x,
      long y,
      const base::Closure& closure) OVERRIDE {
    ui_controls::UIControlsAura* ui_controls =
        GetUIControlsAt(gfx::Point(x, y));
    return ui_controls &&
        ui_controls->SendMouseMoveNotifyWhenDone(x, y, closure);
  }

  virtual bool SendMouseEvents(ui_controls::MouseButton type,
                               int state) OVERRIDE {
    ui_controls::UIControlsAura* ui_controls =
        GetUIControlsAt(gfx::Screen::GetCursorScreenPoint());
    return ui_controls && ui_controls->SendMouseEvents(type, state);
  }

  virtual bool SendMouseEventsNotifyWhenDone(
      ui_controls::MouseButton type,
      int state,
      const base::Closure& closure) OVERRIDE {
    ui_controls::UIControlsAura* ui_controls =
        GetUIControlsAt(gfx::Screen::GetCursorScreenPoint());
    return ui_controls && ui_controls->SendMouseEventsNotifyWhenDone(
        type, state, closure);
  }

  virtual bool SendMouseClick(ui_controls::MouseButton type) OVERRIDE {
    ui_controls::UIControlsAura* ui_controls =
        GetUIControlsAt(gfx::Screen::GetCursorScreenPoint());
    return ui_controls && ui_controls->SendMouseClick(type);
  }

  virtual void RunClosureAfterAllPendingUIEvents(
      const base::Closure& closure) OVERRIDE {
    ui_controls::UIControlsAura* ui_controls = GetUIControlsForRootWindow(
        Shell::GetActiveRootWindow());
    if (ui_controls)
      ui_controls->RunClosureAfterAllPendingUIEvents(closure);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(UIControlsAsh);
};

ui_controls::UIControlsAura* CreateUIControls() {
  return new UIControlsAsh();
}

}  // namespace internal
}  // namespace ash
