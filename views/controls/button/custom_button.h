// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VIEWS_CONTROLS_BUTTON_CUSTOM_BUTTON_H_
#define VIEWS_CONTROLS_BUTTON_CUSTOM_BUTTON_H_
#pragma once

#include "ui/base/animation/animation_delegate.h"
#include "views/controls/button/button.h"

namespace ui {
class ThrobAnimation;
}

namespace views {

// A button with custom rendering. The common base class of ImageButton and
// TextButton.
// Note that this type of button is not focusable by default and will not be
// part of the focus chain.  Call SetFocusable(true) to make it part of the
// focus chain.
class CustomButton : public Button,
                     public ui::AnimationDelegate {
 public:
  // The menu button's class name.
  static const char kViewClassName[];

  virtual ~CustomButton();

  // Possible states
  enum ButtonState {
    BS_NORMAL = 0,
    BS_HOT,
    BS_PUSHED,
    BS_DISABLED,
    BS_COUNT
  };

  // Get/sets the current display state of the button.
  ButtonState state() const { return state_; }
  void SetState(ButtonState state);

  // Starts throbbing. See HoverAnimation for a description of cycles_til_stop.
  void StartThrobbing(int cycles_til_stop);

  // Stops throbbing immediately.
  void StopThrobbing();

  // Set how long the hover animation will last for.
  void SetAnimationDuration(int duration);

  void set_triggerable_event_flags(int triggerable_event_flags) {
    triggerable_event_flags_ = triggerable_event_flags;
  }
  int triggerable_event_flags() const { return triggerable_event_flags_; }

  // Sets whether |RequestFocus| should be invoked on a mouse press. The default
  // is true.
  void set_request_focus_on_press(bool value) {
    request_focus_on_press_ = value;
  }
  bool request_focus_on_press() const { return request_focus_on_press_; }

  // See description above field.
  void set_animate_on_state_change(bool value) {
    animate_on_state_change_ = value;
  }

  // Returns true if the mouse pointer is over this control.  Note that this
  // isn't the same as IsHotTracked() because the mouse may be over the control
  // when it's disabled.
  bool IsMouseHovered() const;

  // Overridden from View:
  virtual void SetHotTracked(bool flag) OVERRIDE;
  virtual bool IsHotTracked() const OVERRIDE;
  virtual void OnEnabledChanged() OVERRIDE;
  virtual bool IsEnabled() const OVERRIDE;
  virtual std::string GetClassName() const OVERRIDE;
  virtual bool OnMousePressed(const MouseEvent& event) OVERRIDE;
  virtual bool OnMouseDragged(const MouseEvent& event) OVERRIDE;
  virtual void OnMouseReleased(const MouseEvent& event) OVERRIDE;
  virtual void OnMouseCaptureLost() OVERRIDE;
  virtual void OnMouseEntered(const MouseEvent& event) OVERRIDE;
  virtual void OnMouseExited(const MouseEvent& event) OVERRIDE;
  virtual void OnMouseMoved(const MouseEvent& event) OVERRIDE;
  virtual bool OnKeyPressed(const KeyEvent& event) OVERRIDE;
  virtual bool OnKeyReleased(const KeyEvent& event) OVERRIDE;
  virtual bool AcceleratorPressed(const Accelerator& accelerator) OVERRIDE;
  virtual void ShowContextMenu(const gfx::Point& p,
                               bool is_mouse_gesture) OVERRIDE;
  virtual void OnDragDone() OVERRIDE;
  virtual void GetAccessibleState(ui::AccessibleViewState* state) OVERRIDE;

  // Overridden from ui::AnimationDelegate:
  virtual void AnimationProgressed(const ui::Animation* animation) OVERRIDE;

 protected:
  // Construct the Button with a Listener. See comment for Button's ctor.
  explicit CustomButton(ButtonListener* listener);

  // Returns true if the event is one that can trigger notifying the listener.
  // This implementation returns true if the left mouse button is down.
  virtual bool IsTriggerableEvent(const MouseEvent& event);

  // Returns true if the button should become pressed when the user
  // holds the mouse down over the button. For this implementation,
  // we simply return IsTriggerableEvent(event).
  virtual bool ShouldEnterPushedState(const MouseEvent& event);

  // Overridden from View:
  virtual void ViewHierarchyChanged(bool is_add,
                                    View* parent,
                                    View* child) OVERRIDE;
  virtual bool IsFocusable() const OVERRIDE;
  virtual void OnBlur() OVERRIDE;

  // The button state (defined in implementation)
  ButtonState state_;

  // Hover animation.
  scoped_ptr<ui::ThrobAnimation> hover_animation_;

 private:
  // Should we animate when the state changes? Defaults to true.
  bool animate_on_state_change_;

  // Is the hover animation running because StartThrob was invoked?
  bool is_throbbing_;

  // Mouse event flags which can trigger button actions.
  int triggerable_event_flags_;

  // See description above setter.
  bool request_focus_on_press_;

  DISALLOW_COPY_AND_ASSIGN(CustomButton);
};

}  // namespace views

#endif  // VIEWS_CONTROLS_BUTTON_CUSTOM_BUTTON_H_
