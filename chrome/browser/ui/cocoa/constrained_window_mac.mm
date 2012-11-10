// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/cocoa/constrained_window_mac.h"

#import "chrome/browser/ui/cocoa/browser_window_controller.h"
#include "chrome/browser/ui/constrained_window_tab_helper.h"
#include "chrome/browser/ui/tab_contents/tab_contents.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#import "third_party/GTM/AppKit/GTMWindowSheetController.h"

ConstrainedWindowMacDelegateSystemSheet::
ConstrainedWindowMacDelegateSystemSheet(id delegate, SEL didEndSelector)
    : systemSheet_(nil),
      delegate_([delegate retain]),
      didEndSelector_(didEndSelector) {}

ConstrainedWindowMacDelegateSystemSheet::
    ~ConstrainedWindowMacDelegateSystemSheet() {}

void ConstrainedWindowMacDelegateSystemSheet::set_sheet(id sheet) {
  systemSheet_.reset([sheet retain]);
}

NSArray* ConstrainedWindowMacDelegateSystemSheet::GetSheetParameters(
    id delegate,
    SEL didEndSelector) {
  return [NSArray arrayWithObjects:
      [NSNull null],  // window, must be [NSNull null]
      delegate,
      [NSValue valueWithPointer:didEndSelector],
      [NSValue valueWithPointer:NULL],  // context info for didEndSelector_.
      nil];
}

void ConstrainedWindowMacDelegateSystemSheet::RunSheet(
    GTMWindowSheetController* sheetController,
    NSView* view) {
  NSArray* params = GetSheetParameters(delegate_.get(), didEndSelector_);
  [sheetController beginSystemSheet:systemSheet_
                       modalForView:view
                     withParameters:params];
}

ConstrainedWindowMacDelegateCustomSheet::
ConstrainedWindowMacDelegateCustomSheet()
    : customSheet_(nil),
      delegate_(nil),
      didEndSelector_(NULL) {}

ConstrainedWindowMacDelegateCustomSheet::
ConstrainedWindowMacDelegateCustomSheet(id delegate, SEL didEndSelector)
    : customSheet_(nil),
      delegate_([delegate retain]),
      didEndSelector_(didEndSelector) {}

ConstrainedWindowMacDelegateCustomSheet::
~ConstrainedWindowMacDelegateCustomSheet() {}

void ConstrainedWindowMacDelegateCustomSheet::init(NSWindow* sheet,
                                                   id delegate,
                                                   SEL didEndSelector) {
    DCHECK(!delegate_.get());
    DCHECK(!didEndSelector_);
    customSheet_.reset([sheet retain]);
    delegate_.reset([delegate retain]);
    didEndSelector_ = didEndSelector;
    DCHECK(delegate_.get());
    DCHECK(didEndSelector_);
  }

void ConstrainedWindowMacDelegateCustomSheet::set_sheet(NSWindow* sheet) {
  customSheet_.reset([sheet retain]);
}

void ConstrainedWindowMacDelegateCustomSheet::RunSheet(
    GTMWindowSheetController* sheetController,
    NSView* view) {
  [sheetController beginSheet:customSheet_.get()
                 modalForView:view
                modalDelegate:delegate_.get()
               didEndSelector:didEndSelector_
                  contextInfo:NULL];
}

ConstrainedWindowMac::ConstrainedWindowMac(
    TabContents* tab_contents, ConstrainedWindowMacDelegate* delegate)
    : tab_contents_(tab_contents),
      delegate_(delegate),
      controller_(nil),
      should_be_visible_(false),
      closing_(false) {
  DCHECK(tab_contents);
  DCHECK(delegate);

  tab_contents->constrained_window_tab_helper()->AddConstrainedDialog(this);
}

ConstrainedWindowMac::~ConstrainedWindowMac() {}

void ConstrainedWindowMac::ShowConstrainedWindow() {
  should_be_visible_ = true;
  // The WebContents only has a native window if it is currently visible. In
  // this case, open the sheet now. Else, Realize() will be called later, when
  // our tab becomes visible.
  NSWindow* browserWindow =
      tab_contents_->web_contents()->GetView()->GetTopLevelNativeWindow();
  BrowserWindowController* browser_controller =
      [BrowserWindowController browserWindowControllerForWindow:browserWindow];
  if ([browser_controller canAttachConstrainedWindow])
    Realize(browser_controller);
}

void ConstrainedWindowMac::CloseConstrainedWindow() {
  // Protection against reentrancy, which might otherwise become a problem if
  // DeleteDelegate forcibly closes a constrained window in a way that results
  // in CloseConstrainedWindow being called again.
  if (closing_)
    return;

  closing_ = true;

  // Note: controller_ can be `nil` here if the sheet was never realized. That's
  // ok.
  [controller_ removeConstrainedWindow:this];
  delegate_->DeleteDelegate();
  tab_contents_->constrained_window_tab_helper()->WillClose(this);

  delete this;
}

void ConstrainedWindowMac::Realize(BrowserWindowController* controller) {
  if (!should_be_visible_)
    return;

  if (controller_ != nil) {
    DCHECK(controller_ == controller);
    return;
  }
  DCHECK(controller != nil);

  // Remember the controller we're adding ourselves to, so that we can later
  // remove us from it.
  controller_ = controller;
  [controller_ attachConstrainedWindow:this];
  delegate_->set_sheet_open(true);
}
