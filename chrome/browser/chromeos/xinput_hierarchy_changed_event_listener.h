// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_XINPUT_HIERARCHY_CHANGED_EVENT_LISTENER_H_
#define CHROME_BROWSER_CHROMEOS_XINPUT_HIERARCHY_CHANGED_EVENT_LISTENER_H_
#pragma once

// TODO(yusukes): Remove the #if once the ARM bot (crbug.com/84694) is fixed.
#if defined(HAVE_XINPUT2)

#include <gdk/gdk.h>

#include "base/memory/singleton.h"
#include "base/message_loop.h"

typedef union _XEvent XEvent;

namespace chromeos {

// XInputHierarchyChangedEventListener listens for an XI_HierarchyChanged event,
// which is sent to Chrome when X detects a system or USB keyboard (or mouse),
// then tells X to change the current XKB keyboard layout. Start by just calling
// instance() to get it going.
class XInputHierarchyChangedEventListener : public MessageLoopForUI::Observer {
 public:
  static XInputHierarchyChangedEventListener* GetInstance();

  void Stop();

 private:
  // Defines the delete on exit Singleton traits we like.  Best to have this
  // and const/dest private as recommended for Singletons.
  friend struct DefaultSingletonTraits<XInputHierarchyChangedEventListener>;

  XInputHierarchyChangedEventListener();
  virtual ~XInputHierarchyChangedEventListener();

  // When TOUCH_UI is not defined, WillProcessXEvent() will not be called
  // automatically. We have to call the function manually by adding the Gdk
  // event filter.
  static GdkFilterReturn GdkEventFilter(GdkXEvent* gxevent,
                                        GdkEvent* gevent,
                                        gpointer data);

  // MessageLoopForUI::Observer overrides.
  virtual void WillProcessEvent(GdkEvent* event) OVERRIDE {}
  virtual void DidProcessEvent(GdkEvent* event) OVERRIDE {}
  virtual bool WillProcessXEvent(XEvent* xevent)
#if defined(TOUCH_UI)
    OVERRIDE
#endif
    ;

  bool stopped_;
  int xiopcode_;

  DISALLOW_COPY_AND_ASSIGN(XInputHierarchyChangedEventListener);
};

}  // namespace chromeos

#endif
#endif  // CHROME_BROWSER_CHROMEOS_XINPUT_HIERARCHY_CHANGED_EVENT_LISTENER_H_
