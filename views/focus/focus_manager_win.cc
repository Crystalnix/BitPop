// Copyright (c) 2006-2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views/focus/focus_manager.h"

#include "views/view.h"
#include "views/widget/native_widget.h"
#include "views/widget/widget.h"

namespace views {

void FocusManager::ClearNativeFocus() {
  // Keep the top root window focused so we get keyboard events.
  ::SetFocus(widget_->GetNativeView());
}

void FocusManager::FocusNativeView(gfx::NativeView native_view) {
  // Only reset focus if hwnd is not already focused.
  if (native_view && ::GetFocus() != native_view)
    ::SetFocus(native_view);
}

// static
FocusManager* FocusManager::GetFocusManagerForNativeView(
    gfx::NativeView native_view) {
  // TODO(beng): This method probably isn't necessary.
  views::NativeWidget* native_widget =
      views::NativeWidget::GetTopLevelNativeWidget(native_view);
  return native_widget ? native_widget->GetWidget()->GetFocusManager() : NULL;
}

// static
FocusManager* FocusManager::GetFocusManagerForNativeWindow(
    gfx::NativeWindow native_window) {
  return GetFocusManagerForNativeView(native_window);
}

}  // namespace views
