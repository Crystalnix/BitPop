// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_HTML_DIALOG_WINDOW_CONTROLLER_H_
#define CHROME_BROWSER_UI_COCOA_HTML_DIALOG_WINDOW_CONTROLLER_H_
#pragma once

#import <Cocoa/Cocoa.h>

#include "base/basictypes.h"
#import "base/mac/cocoa_protocols.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/ui/webui/html_dialog_ui.h"

class Browser;
class HtmlDialogWindowDelegateBridge;
class Profile;
class TabContentsWrapper;

// This controller manages a dialog box with properties and HTML content taken
// from a HTMLDialogUIDelegate object.
@interface HtmlDialogWindowController : NSWindowController<NSWindowDelegate> {
 @private
  // Order here is important, as tab_contents_ may send messages to
  // delegate_ when it gets destroyed.
  scoped_ptr<HtmlDialogWindowDelegateBridge> delegate_;
  scoped_ptr<TabContentsWrapper> contentsWrapper_;
}

// Creates and shows an HtmlDialogWindowController with the given
// delegate and profile whose lifetime is controlled by the given
// browser.  The window is automatically destroyed when it, or its
// controlling browser is closed.  Returns the created window.
//
// Make sure to use the returned window only when you know it is safe
// to do so, i.e. before OnDialogClosed() is called on the delegate.
+ (NSWindow*)showHtmlDialog:(HtmlDialogUIDelegate*)delegate
                    profile:(Profile*)profile
                    browser:(Browser*)browser;

@end

@interface HtmlDialogWindowController (TestingAPI)

// This is the designated initializer.  However, this is exposed only
// for testing; use showHtmlDialog instead.
- (id)initWithDelegate:(HtmlDialogUIDelegate*)delegate
               profile:(Profile*)profile
               browser:(Browser*)browser;

// Loads the HTML content from the delegate; this is not a lightweight
// process which is why it is not part of the constructor.  Must be
// called before showWindow.
- (void)loadDialogContents;

@end

#endif  // CHROME_BROWSER_UI_COCOA_HTML_DIALOG_WINDOW_CONTROLLER_H_

