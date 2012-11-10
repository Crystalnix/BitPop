// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_UI_CONTROLS_UI_CONTROLS_INTERNAL_H_
#define UI_UI_CONTROLS_UI_CONTROLS_INTERNAL_H_

#include "base/callback_forward.h"
#include "ui/ui_controls/ui_controls.h"
#include "ui/base/ui_export.h"

namespace ui_controls {
namespace internal {

// A utility functions for windows to send key or mouse events and
// run the task. These functions are internal, but exported so that
// aura implementation can use these utility functions.
UI_EXPORT bool SendKeyPressImpl(HWND hwnd,
                                ui::KeyboardCode key,
                                bool control,
                                bool shift,
                                bool alt,
                                const base::Closure& task);
UI_EXPORT bool SendMouseMoveImpl(long x, long y, const base::Closure& task);
UI_EXPORT bool SendMouseEventsImpl(MouseButton type,
                                   int state,
                                   const base::Closure& task);
UI_EXPORT void RunClosureAfterAllPendingUITasksImpl(const base::Closure& task);

}  // namespace internal
}  // namespace ui_controls

#endif  // UI_BASE_UI_CONTROLS_UI_CONTROLS_INTERNAL_H_
