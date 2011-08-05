// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views/desktop/desktop_views_delegate.h"

#include "base/logging.h"

namespace views {
namespace desktop {

////////////////////////////////////////////////////////////////////////////////
// DesktopViewsDelegate, public:

DesktopViewsDelegate::DesktopViewsDelegate() {
  DCHECK(!views::ViewsDelegate::views_delegate);
  views::ViewsDelegate::views_delegate = this;
}

DesktopViewsDelegate::~DesktopViewsDelegate() {
}

////////////////////////////////////////////////////////////////////////////////
// DesktopViewsDelegate, ViewsDelegate implementation:

ui::Clipboard* DesktopViewsDelegate::GetClipboard() const {
  return NULL;
}

void DesktopViewsDelegate::SaveWindowPlacement(views::Window* window,
                                   const std::wstring& window_name,
                                   const gfx::Rect& bounds,
                                   bool maximized) {
}

bool DesktopViewsDelegate::GetSavedWindowBounds(views::Window* window,
                                                const std::wstring& window_name,
                                                gfx::Rect* bounds) const {
  return false;
}

bool DesktopViewsDelegate::GetSavedMaximizedState(views::Window* window,
                                      const std::wstring& window_name,
                                      bool* maximized) const {
  return false;
}

void DesktopViewsDelegate::NotifyAccessibilityEvent(
      views::View* view, ui::AccessibilityTypes::Event event_type) {
}

void DesktopViewsDelegate::NotifyMenuItemFocused(
      const std::wstring& menu_name,
      const std::wstring& menu_item_name,
      int item_index,
      int item_count,
      bool has_submenu) {
}

#if defined(OS_WIN)
HICON DesktopViewsDelegate::GetDefaultWindowIcon() const {
  return NULL;
}
#endif

void DesktopViewsDelegate::AddRef() {
}

void DesktopViewsDelegate::ReleaseRef() {
}

int DesktopViewsDelegate::GetDispositionForEvent(int event_flags) {
  return 0;
}

}  // namespace desktop
}  // namespace views
