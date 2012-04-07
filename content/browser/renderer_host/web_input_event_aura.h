// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_WEB_INPUT_EVENT_AURA_H_
#define CONTENT_BROWSER_RENDERER_HOST_WEB_INPUT_EVENT_AURA_H_
#pragma once

#include "third_party/WebKit/Source/WebKit/chromium/public/WebInputEvent.h"

namespace aura {
class GestureEvent;
class KeyEvent;
class MouseEvent;
class ScrollEvent;
class TouchEvent;
}

namespace content {

WebKit::WebMouseEvent MakeWebMouseEvent(aura::MouseEvent* event);
WebKit::WebMouseWheelEvent MakeWebMouseWheelEvent(aura::MouseEvent* event);
WebKit::WebMouseWheelEvent MakeWebMouseWheelEvent(aura::ScrollEvent* event);
WebKit::WebKeyboardEvent MakeWebKeyboardEvent(aura::KeyEvent* event);
WebKit::WebGestureEvent MakeWebGestureEvent(aura::GestureEvent* event);

// Updates the WebTouchEvent based on the TouchEvent. It returns the updated
// WebTouchPoint contained in the WebTouchEvent, or NULL if no point was
// updated.
WebKit::WebTouchPoint* UpdateWebTouchEvent(aura::TouchEvent* event,
                                           WebKit::WebTouchEvent* web_event);

}

#endif  // CONTENT_BROWSER_RENDERER_HOST_WEB_INPUT_EVENT_AURA_H_
