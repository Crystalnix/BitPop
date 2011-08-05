// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VIEWS_DESKTOP_DESKTOP_VIEWS_DELEGATE_H_
#define VIEWS_DESKTOP_DESKTOP_VIEWS_DELEGATE_H_

#include "base/compiler_specific.h"
#include "views/views_delegate.h"

namespace views {
namespace desktop {

class DesktopViewsDelegate : public ViewsDelegate {
 public:
  DesktopViewsDelegate();
  virtual ~DesktopViewsDelegate();

 private:
  // Overridden from ViewsDelegate:
  virtual ui::Clipboard* GetClipboard() const OVERRIDE;
  virtual void SaveWindowPlacement(views::Window* window,
                                   const std::wstring& window_name,
                                   const gfx::Rect& bounds,
                                   bool maximized) OVERRIDE;
  virtual bool GetSavedWindowBounds(views::Window* window,
                                    const std::wstring& window_name,
                                    gfx::Rect* bounds) const OVERRIDE;
  virtual bool GetSavedMaximizedState(views::Window* window,
                                      const std::wstring& window_name,
                                      bool* maximized) const OVERRIDE;
  virtual void NotifyAccessibilityEvent(
      views::View* view, ui::AccessibilityTypes::Event event_type) OVERRIDE;
  virtual void NotifyMenuItemFocused(
      const std::wstring& menu_name,
      const std::wstring& menu_item_name,
      int item_index,
      int item_count,
      bool has_submenu) OVERRIDE;
#if defined(OS_WIN)
  virtual HICON GetDefaultWindowIcon() const OVERRIDE;
#endif
  virtual void AddRef() OVERRIDE;
  virtual void ReleaseRef() OVERRIDE;
  virtual int GetDispositionForEvent(int event_flags) OVERRIDE;

  DISALLOW_COPY_AND_ASSIGN(DesktopViewsDelegate);
};


}  // namespace desktop
}  // namespace views

#endif  // VIEWS_DESKTOP_DESKTOP_VIEWS_DELEGATE_H_
