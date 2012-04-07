// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/automation/ui_controls.h"

#include "base/logging.h"
#include "base/message_loop.h"
#include "chrome/browser/automation/ui_controls_internal.h"
#include "ui/aura/root_window.h"
#include "ui/views/view.h"

namespace ui_controls {

bool SendKeyPress(gfx::NativeWindow window,
                  ui::KeyboardCode key,
                  bool control,
                  bool shift,
                  bool alt,
                  bool command) {
  DCHECK(!command);  // No command key on Aura
  return internal::SendKeyPressImpl(key, control, shift, alt, base::Closure());
}

bool SendKeyPressNotifyWhenDone(gfx::NativeWindow window,
                                ui::KeyboardCode key,
                                bool control,
                                bool shift,
                                bool alt,
                                bool command,
                                const base::Closure& task) {
  DCHECK(!command);  // No command key on Aura
  return internal::SendKeyPressImpl(key, control, shift, alt, task);
}

bool SendMouseMove(long x, long y) {
  gfx::Point point(x, y);
  aura::RootWindow::GetInstance()->ConvertPointToNativeScreen(&point);
  return internal::SendMouseMoveImpl(point.x(), point.y(), base::Closure());
}

bool SendMouseMoveNotifyWhenDone(long x, long y, const base::Closure& task) {
  gfx::Point point(x, y);
  aura::RootWindow::GetInstance()->ConvertPointToNativeScreen(&point);
  return internal::SendMouseMoveImpl(point.x(), point.y(), task);
}

bool SendMouseEvents(MouseButton type, int state) {
  return internal::SendMouseEventsImpl(type, state, base::Closure());
}

bool SendMouseEventsNotifyWhenDone(MouseButton type, int state,
    const base::Closure& task) {
  return internal::SendMouseEventsImpl(type, state, task);
}

bool SendMouseClick(MouseButton type) {
  return SendMouseEvents(type, UP | DOWN);
}

void MoveMouseToCenterAndPress(views::View* view,
                               MouseButton button,
                               int state,
                               const base::Closure& task) {
  DCHECK(view);
  DCHECK(view->GetWidget());
  gfx::Point view_center(view->width() / 2, view->height() / 2);
  views::View::ConvertPointToScreen(view, &view_center);
  SendMouseMove(view_center.x(), view_center.y());
  SendMouseEventsNotifyWhenDone(button, state, task);
}

void RunClosureAfterAllPendingUIEvents(const base::Closure& task) {
  // On windows, posting UI events is synchronous so just post the closure.
  MessageLoopForUI::current()->PostTask(FROM_HERE, task);
}

}  // namespace ui_controls
