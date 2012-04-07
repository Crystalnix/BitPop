// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/base/events.h"

#include <linux/input.h>
#include <X11/extensions/XInput2.h>

#include "base/event_types.h"
#include "base/logging.h"
#include "ui/base/keycodes/keyboard_code_conversion_x.h"
#include "ui/gfx/point.h"

using namespace base::wayland;

namespace ui {

// These are the mouse events expected. The event type Wayland sends is an
// evdev event. The following is the correct mapping from evdev to expected
// events type.
enum WaylandEventButtonType {
  LEFT_BUTTON     = BTN_LEFT,
  MIDDLE_BUTTON   = BTN_RIGHT,
  RIGHT_BUTTON    = BTN_MIDDLE,
  SCROLL_UP       = BTN_SIDE,
  SCROLL_DOWN     = BTN_EXTRA,
};

} // namespace ui

namespace {

// Scroll amount for each wheelscroll event. 53 is also the value used for GTK+.
static int kWheelScrollAmount = 53;

int GetEventFlagsFromState(unsigned int state) {
  int flags = 0;
  if (state & ControlMask)
    flags |= ui::EF_CONTROL_DOWN;
  if (state & ShiftMask)
    flags |= ui::EF_SHIFT_DOWN;
  if (state & Mod1Mask)
    flags |= ui::EF_ALT_DOWN;
  if (state & LockMask)
    flags |= ui::EF_CAPS_LOCK_DOWN;
  if (state & Button1Mask)
    flags |= ui::EF_LEFT_MOUSE_BUTTON;
  if (state & Button2Mask)
    flags |= ui::EF_MIDDLE_MOUSE_BUTTON;
  if (state & Button3Mask)
    flags |= ui::EF_RIGHT_MOUSE_BUTTON;

  return flags;
}

int GetButtonEventFlagsFromNativeEvent(const base::NativeEvent& native_event) {
  // TODO(dnicoara): Need to add double click.
  int flags = 0;
  switch (native_event->button.button) {
    case ui::LEFT_BUTTON:
      return flags | ui::EF_LEFT_MOUSE_BUTTON;
    case ui::MIDDLE_BUTTON:
      return flags | ui::EF_MIDDLE_MOUSE_BUTTON;
    case ui::RIGHT_BUTTON:
      return flags | ui::EF_RIGHT_MOUSE_BUTTON;
  }
  return flags;
}

}  // namespace

namespace ui {

EventType EventTypeFromNative(const base::NativeEvent& native_event) {
  switch (native_event->type) {
    case WAYLAND_BUTTON:
      switch (native_event->button.button) {
        case LEFT_BUTTON:
        case RIGHT_BUTTON:
        case MIDDLE_BUTTON:
          return native_event->button.state ? ET_MOUSE_PRESSED
                                            : ET_MOUSE_RELEASED;
        case SCROLL_UP:
        case SCROLL_DOWN:
          return ET_MOUSEWHEEL;
        default:
          break;
      }
      break;
    case WAYLAND_KEY:
      return native_event->key.state ? ET_KEY_PRESSED : ET_KEY_RELEASED;
    case WAYLAND_MOTION:
      return ET_MOUSE_MOVED;
    case WAYLAND_POINTER_FOCUS:
      return native_event->pointer_focus.state ? ET_MOUSE_ENTERED
                                               : ET_MOUSE_EXITED;
    case WAYLAND_KEYBOARD_FOCUS:
      return ET_UNKNOWN;
    default:
      break;
  }
  return ET_UNKNOWN;
}

int EventFlagsFromNative(const base::NativeEvent& native_event) {
  switch (native_event->type) {
    case WAYLAND_BUTTON:
      return GetButtonEventFlagsFromNativeEvent(native_event) |
             GetEventFlagsFromState(native_event->button.modifiers);
    case WAYLAND_KEY:
      return GetEventFlagsFromState(native_event->key.modifiers);
    case WAYLAND_MOTION:
      return GetEventFlagsFromState(native_event->motion.modifiers);
    case WAYLAND_KEYBOARD_FOCUS:
      return GetEventFlagsFromState(native_event->keyboard_focus.modifiers);
    default:
      return 0;
  }
}

gfx::Point EventLocationFromNative(const base::NativeEvent& native_event) {
  switch (native_event->type) {
    case WAYLAND_BUTTON:
      return gfx::Point(native_event->button.x, native_event->button.y);
    case WAYLAND_MOTION:
      return gfx::Point(native_event->motion.x, native_event->motion.y);
    case WAYLAND_POINTER_FOCUS:
      return gfx::Point(native_event->pointer_focus.x,
                        native_event->pointer_focus.y);
    default:
      return gfx::Point();
  }
}

KeyboardCode KeyboardCodeFromNative(const base::NativeEvent& native_event) {
  return KeyboardCodeFromXKeysym(native_event->key.sym);
}

bool IsMouseEvent(const base::NativeEvent& native_event) {
  return native_event->type == WAYLAND_BUTTON ||
         native_event->type == WAYLAND_MOTION ||
         native_event->type == WAYLAND_POINTER_FOCUS;
}

int GetMouseWheelOffset(const base::NativeEvent& native_event) {
  return native_event->button.button == SCROLL_UP ?
      kWheelScrollAmount : -kWheelScrollAmount;
}

}  // namespace ui
