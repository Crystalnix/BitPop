// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/native_web_keyboard_event.h"

#if defined(TOOLKIT_USES_GTK)
#include <gdk/gdk.h>
#endif

#include "base/logging.h"
#include "views/events/event.h"

namespace {

int ViewsFlagsToWebInputEventModifiers(int flags) {
  return
      (flags & ui::EF_SHIFT_DOWN ? WebKit::WebInputEvent::ShiftKey : 0) |
      (flags & ui::EF_CONTROL_DOWN ? WebKit::WebInputEvent::ControlKey : 0) |
      (flags & ui::EF_CAPS_LOCK_DOWN ? WebKit::WebInputEvent::CapsLockOn : 0) |
      (flags & ui::EF_ALT_DOWN ? WebKit::WebInputEvent::AltKey : 0);
}

}  // namespace

NativeWebKeyboardEvent::NativeWebKeyboardEvent(
    const views::KeyEvent& event)
    : skip_in_browser(false) {
  DCHECK(event.type() == ui::ET_KEY_PRESSED ||
         event.type() == ui::ET_KEY_RELEASED);

  if (event.type() == ui::ET_KEY_PRESSED)
    type = WebKit::WebInputEvent::RawKeyDown;
  else
    type = WebKit::WebInputEvent::KeyUp;

  modifiers = ViewsFlagsToWebInputEventModifiers(event.flags());
  timeStampSeconds = event.time_stamp().ToDoubleT();
  windowsKeyCode = event.key_code();
  nativeKeyCode = windowsKeyCode;
  text[0] = event.GetCharacter();
  unmodifiedText[0] = event.GetUnmodifiedCharacter();
  setKeyIdentifierFromWindowsKeyCode();

#if defined(OS_WIN)
  // |os_event| is a MSG struct, so we can copy it directly.
  os_event = event.native_event();
#elif defined(TOOLKIT_USES_GTK)
  if (event.native_event()) {
    os_event =
        reinterpret_cast<GdkEventKey*>(gdk_event_copy(event.native_event()));
    nativeKeyCode = os_event->keyval;
  } else {
    os_event = NULL;
  }
#endif

#if defined(OS_LINUX)
  match_edit_command = false;
#endif
}

NativeWebKeyboardEvent::NativeWebKeyboardEvent(uint16 character,
                                               int flags,
                                               double time_stamp_seconds,
                                               FromViewsEvent)
    : skip_in_browser(true) {
  type = WebKit::WebInputEvent::Char;
  modifiers = ViewsFlagsToWebInputEventModifiers(flags);
  timeStampSeconds = time_stamp_seconds;
  windowsKeyCode = character;
  nativeKeyCode = character;
  text[0] = character;
  unmodifiedText[0] = character;
  isSystemKey = (flags & ui::EF_ALT_DOWN) != 0;

#if defined(OS_WIN)
  memset(&os_event, 0, sizeof(os_event));
#elif defined(TOOLKIT_USES_GTK)
  os_event = NULL;
#endif

#if defined(OS_LINUX)
  match_edit_command = false;
#endif
}
