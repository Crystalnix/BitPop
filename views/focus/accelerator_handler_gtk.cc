// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views/focus/accelerator_handler.h"

#include <gtk/gtk.h>

#include "views/accelerator.h"
#include "views/focus/focus_manager.h"
#include "views/widget/widget_gtk.h"

namespace views {

AcceleratorHandler::AcceleratorHandler() {}

bool AcceleratorHandler::Dispatch(GdkEvent* event) {
  // The logic for handling keyboard accelerators has been moved into
  // WidgetGtk::OnKeyEvent handler (views/widget/widget_gtk.cc).
  gtk_main_do_event(event);
  return true;
}

}  // namespace views
