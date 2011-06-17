// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/ui_test_utils.h"

#include <Carbon/Carbon.h>
#import <Cocoa/Cocoa.h>

#include "base/logging.h"
#include "base/message_loop.h"
#include "chrome/browser/automation/ui_controls.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#import "chrome/browser/ui/cocoa/view_id_util.h"

namespace ui_test_utils {

bool IsViewFocused(const Browser* browser, ViewID vid) {
  NSWindow* window = browser->window()->GetNativeHandle();
  DCHECK(window);
  NSView* view = view_id_util::GetView(window, vid);
  if (!view)
    return false;

  NSResponder* firstResponder = [window firstResponder];
  if (firstResponder == static_cast<NSResponder*>(view))
    return true;

  // Handle the special case of focusing a TextField.
  if ([firstResponder isKindOfClass:[NSTextView class]]) {
    NSView* delegate = static_cast<NSView*>([(NSTextView*)firstResponder
                                                          delegate]);
    if (delegate == view)
      return true;
  }

  return false;
}

void ClickOnView(const Browser* browser, ViewID vid) {
  NSWindow* window = browser->window()->GetNativeHandle();
  DCHECK(window);
  NSView* view = view_id_util::GetView(window, vid);
  DCHECK(view);
  ui_controls::MoveMouseToCenterAndPress(
      view,
      ui_controls::LEFT,
      ui_controls::DOWN | ui_controls::UP,
      new MessageLoop::QuitTask());
  RunMessageLoop();
}

void HideNativeWindow(gfx::NativeWindow window) {
  [window orderOut:nil];
}

void ShowAndFocusNativeWindow(gfx::NativeWindow window) {
  // Make sure an unbundled program can get the input focus.
  ProcessSerialNumber psn = { 0, kCurrentProcess };
  TransformProcessType(&psn,kProcessTransformToForegroundApplication);
  SetFrontProcess(&psn);

  [window makeKeyAndOrderFront:nil];
}

}  // namespace ui_test_utils
