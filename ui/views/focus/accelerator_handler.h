// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_FOCUS_ACCELERATOR_HANDLER_H_
#define UI_VIEWS_FOCUS_ACCELERATOR_HANDLER_H_
#pragma once

#include "build/build_config.h"

#if defined(TOOLKIT_USES_GTK)
#include <gdk/gdk.h>
#endif

#include <set>
#include <vector>

#include "base/compiler_specific.h"
#include "base/message_loop.h"
#include "ui/views/views_export.h"

namespace views {

#if defined(USE_AURA) && defined(USE_X11) && !defined(USE_WAYLAND)
// Dispatch an XEvent to the RootView. Return true if the event was dispatched
// and handled, false otherwise.
bool VIEWS_EXPORT DispatchXEvent(XEvent* xevent);
#endif  // USE_AURA && USE_X11 && !USE_WAYLAND

// This class delegates the key messages to the associated FocusManager class
// for the window that is receiving these messages for accelerator processing.
#if defined(OS_MACOSX)
class VIEWS_EXPORT AcceleratorHandler {
#else
class VIEWS_EXPORT AcceleratorHandler : public MessageLoop::Dispatcher {
#endif  // defined(OS_MACOSX)
 public:
  AcceleratorHandler();

  // Dispatcher method. This returns true if an accelerator was processed by the
  // focus manager
#if defined(OS_WIN)
  virtual bool Dispatch(const MSG& msg) OVERRIDE;
#elif defined(USE_WAYLAND)
  virtual base::MessagePumpDispatcher::DispatchStatus Dispatch(
      base::wayland::WaylandEvent* ev) OVERRIDE;
#elif defined(OS_MACOSX)
  // TODO(dhollowa): Implement on Mac.  http://crbug.com/109946
#elif defined(USE_AURA)
  virtual base::MessagePumpDispatcher::DispatchStatus Dispatch(
      XEvent* xev) OVERRIDE;
#else
  virtual bool Dispatch(GdkEvent* event) OVERRIDE;
#endif

 private:
#if defined(OS_WIN)
  // The keys currently pressed and consumed by the FocusManager.
  std::set<WPARAM> pressed_keys_;
#endif

  DISALLOW_COPY_AND_ASSIGN(AcceleratorHandler);
};

}  // namespace views

#endif  // UI_VIEWS_FOCUS_ACCELERATOR_HANDLER_H_
