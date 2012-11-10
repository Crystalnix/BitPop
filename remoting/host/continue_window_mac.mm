// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/continue_window.h"

#import <Cocoa/Cocoa.h>

#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/mac/scoped_nsautorelease_pool.h"
#include "base/memory/scoped_nsobject.h"
#include "base/sys_string_conversions.h"
#include "remoting/host/chromoting_host.h"

typedef remoting::ContinueWindow::ContinueSessionCallback
    ContinueSessionCallback;

// Handles the ContinueWindow.
@interface ContinueWindowMacController : NSObject {
 @private
  scoped_nsobject<NSMutableArray> shades_;
  scoped_nsobject<NSAlert> continue_alert_;
  remoting::ChromotingHost* host_;
  ContinueSessionCallback callback_;
}

- (id)initWithHost:(remoting::ChromotingHost*)host
          callback:(const ContinueSessionCallback&)callback;
- (void)show;
- (void)hide;
- (void)onCancel:(id)sender;
- (void)onContinue:(id)sender;
@end

namespace remoting {

// A bridge between C++ and ObjC implementations of ContinueWindow.
// Everything important occurs in ContinueWindowMacController.
class ContinueWindowMac : public remoting::ContinueWindow {
 public:
  ContinueWindowMac() {}
  virtual ~ContinueWindowMac() {}

  virtual void Show(remoting::ChromotingHost* host,
                    const ContinueSessionCallback& callback) OVERRIDE;
  virtual void Hide() OVERRIDE;

 private:
  scoped_nsobject<ContinueWindowMacController> controller_;
  ContinueSessionCallback callback_;

  DISALLOW_COPY_AND_ASSIGN(ContinueWindowMac);
};

void ContinueWindowMac::Show(remoting::ChromotingHost* host,
                             const ContinueSessionCallback& callback) {
  base::mac::ScopedNSAutoreleasePool pool;
  controller_.reset(
      [[ContinueWindowMacController alloc] initWithHost:host
                                               callback:callback]);
  [controller_ show];
}

void ContinueWindowMac::Hide() {
  base::mac::ScopedNSAutoreleasePool pool;
  [controller_ hide];
}

scoped_ptr<ContinueWindow> ContinueWindow::Create() {
  return scoped_ptr<ContinueWindow>(new ContinueWindowMac());
}

}  // namespace remoting

@implementation ContinueWindowMacController

- (id)initWithHost:(remoting::ChromotingHost*)host
          callback:(const ContinueSessionCallback&)callback {
  if ((self = [super init])) {
    host_ = host;
    callback_ = callback;
  }
  return self;
}

- (void)show {
  // Generate window shade
  NSArray* screens = [NSScreen screens];
  shades_.reset([[NSMutableArray alloc] initWithCapacity:[screens count]]);
  for (NSScreen *screen in screens) {
    NSWindow* shade =
      [[[NSWindow alloc] initWithContentRect:[screen frame]
                                   styleMask:NSBorderlessWindowMask
                                     backing:NSBackingStoreBuffered
                                       defer:NO
                                      screen:screen] autorelease];
    [shade setReleasedWhenClosed:NO];
    [shade setAlphaValue:0.8];
    [shade setOpaque:NO];
    [shade setBackgroundColor:[NSColor blackColor]];
    // Raise the window shade above just about everything else.
    // Leave the dock and menu bar exposed so the user has some basic level
    // of control (like they can quit Chromium).
    [shade setLevel:NSModalPanelWindowLevel - 1];
    [shade orderFront:nil];
    [shades_ addObject:shade];
  }

  // Create alert.
  const remoting::UiStrings& strings = host_->ui_strings();
  NSString* message = base::SysUTF16ToNSString(strings.continue_prompt);
  NSString* continue_button_string = base::SysUTF16ToNSString(
      strings.continue_button_text);
  NSString* cancel_button_string = base::SysUTF16ToNSString(
      strings.stop_sharing_button_text);
  continue_alert_.reset([[NSAlert alloc] init]);
  [continue_alert_ setMessageText:message];

  NSButton* continue_button =
      [continue_alert_ addButtonWithTitle:continue_button_string];
  [continue_button setAction:@selector(onContinue:)];
  [continue_button setTarget:self];

  NSButton* cancel_button =
      [continue_alert_ addButtonWithTitle:cancel_button_string];
  [cancel_button setAction:@selector(onCancel:)];
  [cancel_button setTarget:self];

  NSBundle *bundle = [NSBundle bundleForClass:[self class]];
  NSString *imagePath = [bundle pathForResource:@"chromoting128" ofType:@"png"];
  scoped_nsobject<NSImage> image(
      [[NSImage alloc] initByReferencingFile:imagePath]);
  [continue_alert_ setIcon:image];
  [continue_alert_ layout];

  // Force alert to be at the proper level and location.
  NSWindow* continue_window = [continue_alert_ window];
  [continue_window center];
  [continue_window setLevel:NSModalPanelWindowLevel];
  [continue_window orderWindow:NSWindowAbove
                    relativeTo:[[shades_ lastObject] windowNumber]];
  [continue_window makeKeyWindow];
}

- (void)hide {
  // Remove window shade.
  for (NSWindow* window in shades_.get()) {
    [window close];
  }
  shades_.reset();
  continue_alert_.reset();
}

- (void)onCancel:(id)sender {
  [self hide];
  callback_.Run(false);
  host_ = nil;
}

- (void)onContinue:(id)sender {
  [self hide];
  callback_.Run(true);
  host_ = nil;
}

@end
