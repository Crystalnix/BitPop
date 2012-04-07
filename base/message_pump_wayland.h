// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_MESSAGE_PUMP_WAYLAND_H_
#define BASE_MESSAGE_PUMP_WAYLAND_H_
#pragma once

#include "base/memory/scoped_ptr.h"
#include "base/message_pump_glib.h"
#include "base/message_pump_observer.h"

namespace base {

namespace wayland {
union WaylandEvent;
}

// The documentation for this class is in message_pump_glib.h
//
// The nested loop is exited by either posting a quit, or returning false
// from Dispatch.
class MessagePumpDispatcher {
 public:
  enum DispatchStatus {
    EVENT_IGNORED,    // The event was not processed.
    EVENT_PROCESSED,  // The event has been processed.
    EVENT_QUIT        // The event was processed and the message-loop should
                      // terminate.
  };

  // Dispatches the event. If true is returned processing continues as
  // normal. If false is returned, the nested loop exits immediately.
  virtual DispatchStatus Dispatch(wayland::WaylandEvent* event) = 0;

 protected:
  virtual ~MessagePumpDispatcher() {}
};

typedef MessagePumpGlib MessagePumpForUI;

}  // namespace base

#endif  // BASE_MESSAGE_PUMP_WAYLAND_H_
