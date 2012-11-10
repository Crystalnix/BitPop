// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_WM_OVERLAY_EVENT_FILTER_H_
#define ASH_WM_OVERLAY_EVENT_FILTER_H_

#include "ash/shell_observer.h"
#include "base/compiler_specific.h"
#include "ui/aura/event.h"
#include "ui/aura/event_filter.h"

namespace ash {
namespace internal {

// EventFilter for the "overlay window", which intercepts events before they are
// processed by the usual path (e.g. the partial screenshot UI, the keyboard
// overlay).  It does nothing the first time, but works when |Activate()| is
// called.  The main task of this event filter is just to stop propagation
// of any key events during activation, and also signal cancellation when keys
// for canceling are pressed.
class OverlayEventFilter : public aura::EventFilter,
                           public ShellObserver {
 public:
  // Windows that need to receive events from OverlayEventFilter implement this.
  class Delegate {
   public:
    // Invoked when OverlayEventFilter needs to stop handling events.
    virtual void Cancel() = 0;

    // Returns true if the overlay should be canceled in response to |event|.
    virtual bool IsCancelingKeyEvent(aura::KeyEvent* event) = 0;

    // Returns the window that needs to receive events.
    virtual aura::Window* GetWindow() = 0;
  };

  OverlayEventFilter();
  virtual ~OverlayEventFilter();

  // Starts the filtering of events.  It also notifies the specified
  // |delegate| when a key event means cancel (like Esc).  It holds the
  // pointer to the specified |delegate| until Deactivate() is called, but
  // does not take ownership.
  void Activate(Delegate* delegate);

  // Ends the filtering of events.
  void Deactivate();

  // Cancels the partial screenshot UI.  Do nothing if it's not activated.
  void Cancel();

  // aura::EventFilter overrides:
  virtual bool PreHandleKeyEvent(
      aura::Window* target, aura::KeyEvent* event) OVERRIDE;
  virtual bool PreHandleMouseEvent(
      aura::Window* target, aura::MouseEvent* event) OVERRIDE;
  virtual ui::TouchStatus PreHandleTouchEvent(
      aura::Window* target, aura::TouchEvent* event) OVERRIDE;
  virtual ui::GestureStatus PreHandleGestureEvent(
      aura::Window* target, aura::GestureEvent* event) OVERRIDE;

  // ShellObserver overrides:
  virtual void OnLoginStateChanged(user::LoginStatus status) OVERRIDE;
  virtual void OnAppTerminating() OVERRIDE;
  virtual void OnLockStateChanged(bool locked) OVERRIDE;

 private:
  Delegate* delegate_;

  DISALLOW_COPY_AND_ASSIGN(OverlayEventFilter);
};

}  // namespace internal
}  // namespace ash

#endif  // ASH_WM_OVERLAY_EVENT_FILTER_H_
