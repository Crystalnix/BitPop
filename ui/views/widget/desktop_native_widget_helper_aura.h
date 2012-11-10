// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_WIDGET_DESKTOP_NATIVE_WIDGET_HELPER_AURA_H_
#define UI_VIEWS_WIDGET_DESKTOP_NATIVE_WIDGET_HELPER_AURA_H_

#include "ui/aura/root_window_observer.h"
#include "ui/gfx/rect.h"
#include "ui/views/views_export.h"
#include "ui/views/widget/native_widget_helper_aura.h"
#include "ui/views/widget/widget.h"

namespace aura {
class RootWindow;
class DesktopCursorClient;
namespace client {
class ScreenPositionClient;
}
namespace shared {
class CompoundEventFilter;
class InputMethodEventFilter;
class RootWindowCaptureClient;
}
}

namespace ui {
#if defined(OS_WIN)
class HWNDMessageFilter;
#endif
}

namespace views {
class NativeWidgetAura;
class WidgetMessageFilter;
#if defined(USE_X11)
class X11WindowEventFilter;
#endif

// Implementation of non-Ash desktop integration code, allowing
// NativeWidgetAuras to work in a traditional desktop environment.
class VIEWS_EXPORT DesktopNativeWidgetHelperAura
    : public NativeWidgetHelperAura,
      public aura::RootWindowObserver {
 public:
  explicit DesktopNativeWidgetHelperAura(NativeWidgetAura* widget);
  virtual ~DesktopNativeWidgetHelperAura();

  // In general, views/ does not care about the aura::RootWindow, even though
  // at any join point with the native OS, we're going to be dealing in
  // RootWindows.
  static aura::Window* GetViewsWindowForRootWindow(aura::RootWindow* root);

  // Overridden from aura::NativeWidgetHelperAura:
  virtual void PreInitialize(aura::Window* window,
                             const Widget::InitParams& params) OVERRIDE;
  virtual void PostInitialize() OVERRIDE;
  virtual void ShowRootWindow() OVERRIDE;
  virtual aura::RootWindow* GetRootWindow() OVERRIDE;
  virtual gfx::Rect ModifyAndSetBounds(const gfx::Rect& bounds) OVERRIDE;

  // Overridden from aura::RootWindowObserver:
  virtual void OnRootWindowResized(const aura::RootWindow* root,
                                   const gfx::Size& old_size) OVERRIDE;
  virtual void OnRootWindowHostClosed(const aura::RootWindow* root) OVERRIDE;

 private:
  // A weak pointer back to our owning widget.
  NativeWidgetAura* widget_;

  // Optionally, a RootWindow that we attach ourselves to.
  scoped_ptr<aura::RootWindow> root_window_;

  // Toplevel event filter which dispatches to other event filters.
  aura::shared::CompoundEventFilter* root_window_event_filter_;

  // An event filter that pre-handles all key events to send them to an IME.
  scoped_ptr<aura::shared::InputMethodEventFilter> input_method_filter_;

  // TODO(erg): This is temporary. Find out what needs to be done for desktop
  // environment.
  scoped_ptr<aura::shared::RootWindowCaptureClient> capture_client_;

  // We want some windows (omnibox, status bar) to have their own
  // NativeWidgetAura, but still act as if they're screen bounded toplevel
  // windows.
  bool is_embedded_window_;

  // In some cases, we set a screen position client on |root_window_|. If we
  // do, we're responsible for the lifetime.
  scoped_ptr<aura::client::ScreenPositionClient> position_client_;

  // A simple cursor client which just forwards events to the RootWindow.
  scoped_ptr<aura::DesktopCursorClient> cursor_client_;

#if defined(OS_WIN)
  scoped_ptr<ui::HWNDMessageFilter> hwnd_message_filter_;
#elif defined(USE_X11)
  scoped_ptr<X11WindowEventFilter> x11_window_event_filter_;
#endif

  DISALLOW_COPY_AND_ASSIGN(DesktopNativeWidgetHelperAura);
};

}  // namespace views

#endif  // UI_VIEWS_WIDGET_DESKTOP_NATIVE_WIDGET_HELPER_AURA_H_
