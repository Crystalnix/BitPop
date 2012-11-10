// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_BROWSER_FRAME_AURA_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_BROWSER_FRAME_AURA_H_

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/ui/views/frame/native_browser_frame.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/widget/native_widget_aura.h"

class BrowserFrame;
class BrowserView;

namespace views {
class MenuRunner;
}

////////////////////////////////////////////////////////////////////////////////
// BrowserFrameAura
//
//  BrowserFrameAura is a NativeWidgetAura subclass that provides the window
//  frame for the Chrome browser window.
//
class BrowserFrameAura : public views::ContextMenuController,
                         public views::NativeWidgetAura,
                         public NativeBrowserFrame {
 public:
  BrowserFrameAura(BrowserFrame* browser_frame, BrowserView* browser_view);

  BrowserView* browser_view() const { return browser_view_; }

 protected:
  // Overridden from views::ContextMenuController:
  virtual void ShowContextMenuForView(views::View* source,
                                      const gfx::Point& p) OVERRIDE;

  // Overridden from views::NativeWidgetAura:
  virtual void OnWindowDestroying() OVERRIDE;

  // Overridden from NativeBrowserFrame:
  virtual views::NativeWidget* AsNativeWidget() OVERRIDE;
  virtual const views::NativeWidget* AsNativeWidget() const OVERRIDE;
  virtual void InitSystemContextMenu() OVERRIDE;
  virtual int GetMinimizeButtonOffset() const OVERRIDE;
  virtual void TabStripDisplayModeChanged() OVERRIDE;

 private:
  class WindowPropertyWatcher;

  virtual ~BrowserFrameAura();

  // The BrowserView is our ClientView. This is a pointer to it.
  BrowserView* browser_view_;

  scoped_ptr<WindowPropertyWatcher> window_property_watcher_;

  // System menu.
  scoped_ptr<views::MenuRunner> menu_runner_;

  DISALLOW_COPY_AND_ASSIGN(BrowserFrameAura);
};

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_BROWSER_FRAME_AURA_H_
