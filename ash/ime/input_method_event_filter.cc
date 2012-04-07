// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/ime/input_method_event_filter.h"

#include "ash/ime/event.h"
#include "ash/shell.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/event.h"
#include "ui/aura/root_window.h"
#include "ui/base/ime/input_method.h"
#include "ui/base/ime/input_method_factory.h"

namespace ash {
namespace internal {

////////////////////////////////////////////////////////////////////////////////
// InputMethodEventFilter, public:

InputMethodEventFilter::InputMethodEventFilter()
    : EventFilter(aura::RootWindow::GetInstance()),
      ALLOW_THIS_IN_INITIALIZER_LIST(
          input_method_(ui::CreateInputMethod(this))) {
  // TODO(yusukes): Check if the root window is currently focused and pass the
  // result to Init().
  input_method_->Init(true);
  aura::RootWindow::GetInstance()->SetProperty(
      aura::client::kRootWindowInputMethod,
      input_method_.get());
}

InputMethodEventFilter::~InputMethodEventFilter() {
}

////////////////////////////////////////////////////////////////////////////////
// InputMethodEventFilter, EventFilter implementation:

bool InputMethodEventFilter::PreHandleKeyEvent(aura::Window* target,
                                               aura::KeyEvent* event) {
  const ui::EventType type = event->type();
  if (type == ui::ET_TRANSLATED_KEY_PRESS ||
      type == ui::ET_TRANSLATED_KEY_RELEASE) {
    // The |event| is already handled by this object, change the type of the
    // event to ui::ET_KEY_* and pass it to the next filter.
    static_cast<TranslatedKeyEvent*>(event)->ConvertToKeyEvent();
    return false;
  } else {
    input_method_->DispatchKeyEvent(event->native_event());
    return true;
  }
}

bool InputMethodEventFilter::PreHandleMouseEvent(aura::Window* target,
                                                 aura::MouseEvent* event) {
  return false;
}

ui::TouchStatus InputMethodEventFilter::PreHandleTouchEvent(
    aura::Window* target,
    aura::TouchEvent* event) {
  return ui::TOUCH_STATUS_UNKNOWN;
}

ui::GestureStatus InputMethodEventFilter::PreHandleGestureEvent(
    aura::Window* target,
    aura::GestureEvent* event) {
  return ui::GESTURE_STATUS_UNKNOWN;
}

////////////////////////////////////////////////////////////////////////////////
// InputMethodEventFilter, ui::InputMethodDelegate implementation:

void InputMethodEventFilter::DispatchKeyEventPostIME(
    const base::NativeEvent& event) {
#if defined(OS_WIN)
  DCHECK(event.message != WM_CHAR);
#endif
  TranslatedKeyEvent aura_event(event, false /* is_char */);
  aura::RootWindow::GetInstance()->DispatchKeyEvent(&aura_event);
}

void InputMethodEventFilter::DispatchFabricatedKeyEventPostIME(
    ui::EventType type,
    ui::KeyboardCode key_code,
    int flags) {
  TranslatedKeyEvent aura_event(type == ui::ET_KEY_PRESSED, key_code, flags);
  aura::RootWindow::GetInstance()->DispatchKeyEvent(&aura_event);
}

}  // namespace internal
}  // namespace ash
