// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef VIEWS_CONTROLS_MENU_MENU_HOST_GTK_H_
#define VIEWS_CONTROLS_MENU_MENU_HOST_GTK_H_
#pragma once

#include "views/controls/menu/native_menu_host.h"
#include "views/widget/widget_gtk.h"

namespace views {
namespace internal {
class NativeMenuHostDelegate;
}

// NativeMenuHost implementation for Gtk.
class MenuHostGtk : public WidgetGtk,
                    public NativeMenuHost {
 public:
  explicit MenuHostGtk(internal::NativeMenuHostDelegate* delegate);
  virtual ~MenuHostGtk();

 private:
  // Overridden from NativeMenuHost:
  virtual void InitMenuHost(gfx::NativeWindow parent,
                            const gfx::Rect& bounds) OVERRIDE;
  virtual void StartCapturing() OVERRIDE;
  virtual NativeWidget* AsNativeWidget() OVERRIDE;

  // Overridden from WidgetGtk:
  virtual RootView* CreateRootView() OVERRIDE;
  virtual bool ShouldReleaseCaptureOnMouseReleased() const OVERRIDE;
  virtual void ReleaseMouseCapture() OVERRIDE;
  virtual void OnDestroy(GtkWidget* object) OVERRIDE;
  virtual void HandleGtkGrabBroke() OVERRIDE;
  virtual void HandleXGrabBroke() OVERRIDE;

  // Have we done input grab?
  bool did_input_grab_;

  scoped_ptr<internal::NativeMenuHostDelegate> delegate_;

  DISALLOW_COPY_AND_ASSIGN(MenuHostGtk);
};

}  // namespace views

#endif  // VIEWS_CONTROLS_MENU_MENU_HOST_GTK_H_
