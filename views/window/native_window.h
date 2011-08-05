// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VIEWS_WIDGET_NATIVE_WINDOW_H_
#define VIEWS_WIDGET_NATIVE_WINDOW_H_
#pragma once

#include "ui/base/accessibility/accessibility_types.h"
#include "ui/gfx/native_widget_types.h"
#include "views/window/window.h"

class SkBitmap;

namespace gfx {
class Rect;
class Size;
}

namespace views {

class NativeWidget;
class NonClientFrameView;

////////////////////////////////////////////////////////////////////////////////
// NativeWindow interface
//
//  An interface implemented by an object that encapsulates a native window.
//
class NativeWindow {
 public:
  enum ShowState {
    SHOW_RESTORED,
    SHOW_MAXIMIZED,
    SHOW_INACTIVE
  };

  virtual ~NativeWindow() {}

  // Creates an appropriate default NativeWindow implementation for the current
  // OS/circumstance.
  static NativeWindow* CreateNativeWindow(
      internal::NativeWindowDelegate* delegate);

  virtual Window* GetWindow() = 0;
  virtual const Window* GetWindow() const = 0;

  virtual NativeWidget* AsNativeWidget() = 0;
  virtual const NativeWidget* AsNativeWidget() const = 0;

 protected:
  friend class Window;

  // Returns the bounds of the window in screen coordinates for its non-
  // maximized state, regardless of whether or not it is currently maximized.
  virtual gfx::Rect GetRestoredBounds() const = 0;

  // Shows the window.
  virtual void ShowNativeWindow(ShowState state) = 0;

  // Makes the NativeWindow modal.
  virtual void BecomeModal() = 0;

  // Centers the window and sizes it to the specified size.
  virtual void CenterWindow(const gfx::Size& size) = 0;

  // Retrieves the window's current restored bounds and maximized state, for
  // persisting.
  virtual void GetWindowBoundsAndMaximizedState(gfx::Rect* bounds,
                                                bool* maximized) const = 0;

  // Enables or disables the close button for the window.
  virtual void EnableClose(bool enable) = 0;

  // Sets the NativeWindow title.
  virtual void SetWindowTitle(const std::wstring& title) = 0;

  // Sets the Window icons. |window_icon| is a 16x16 icon suitable for use in
  // a title bar. |app_icon| is a larger size for use in the host environment
  // app switching UI.
  virtual void SetWindowIcons(const SkBitmap& window_icon,
                              const SkBitmap& app_icon) = 0;

  // Update native accessibility properties on the native window.
  virtual void SetAccessibleName(const std::wstring& name) = 0;
  virtual void SetAccessibleRole(ui::AccessibilityTypes::Role role) = 0;
  virtual void SetAccessibleState(ui::AccessibilityTypes::State state) = 0;

  // Window pass-thrus ---------------------------------------------------------
  // See documentation in window.h

  virtual void SetFullscreen(bool fullscreen) = 0;
  virtual bool IsFullscreen() const = 0;
  virtual void SetUseDragFrame(bool use_drag_frame) = 0;
  virtual NonClientFrameView* CreateFrameViewForWindow() = 0;
  virtual void UpdateFrameAfterFrameChange() = 0;
  virtual bool ShouldUseNativeFrame() const = 0;
  virtual void FrameTypeChanged() = 0;
};

}  // namespace views

#endif  // VIEWS_WIDGET_NATIVE_WINDOW_H_
