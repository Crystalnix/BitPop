// Copyright (c) 2013 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2013 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/facebook_button_bubble_controller.h"

#include "base/logging.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/first_run/first_run.h"
#include "chrome/browser/search_engines/util.h"
#include "chrome/browser/ui/chrome_pages.h"
#import "chrome/browser/ui/cocoa/l10n_util.h"
#import "chrome/browser/ui/cocoa/info_bubble_view.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

@interface FacebookButtonBubbleController(Private)
- (id)initWithParentWindow:(NSWindow*)parentWindow
               anchorPoint:(NSPoint)anchorPoint
                   browser:(Browser*)browser
                   profile:(Profile*)profile
                     other:(FirstRunBubbleController*)other;
- (void)closeIfNotKey;
@end

@implementation FacebookButtonBubbleController

+ (FacebookButtonBubbleController*)showForParentWindow:(NSWindow*)parentWindow
            anchorPoint:(NSPoint)anchorPoint
                browser:(Browser*)browser
                profile:(Profile*)profile
                  other:(FirstRunBubbleController*)other{
  // Autoreleases itself on bubble close.
  return [[FacebookButtonBubbleController alloc]
                               initWithParentWindow:parentWindow
                                        anchorPoint:anchorPoint
                                            browser:browser
                                            profile:profile
                                              other:other];
}

- (id)initWithParentWindow:(NSWindow*)parentWindow
               anchorPoint:(NSPoint)anchorPoint
                   browser:(Browser*)browser
                   profile:(Profile*)profile
                     other:(FirstRunBubbleController*)other {
  if ((self = [super initWithWindowNibPath:@"FacebookButtonBubble"
                              parentWindow:parentWindow
                                anchoredAt:anchorPoint])) {
    browser_ = browser;
    profile_ = profile;
    other_ = other;
    [self showWindow:nil];

    // On 10.5, the first run bubble sometimes does not disappear when clicking
    // the omnibox. This happens if the bubble never became key, due to it
    // showing up so early in the startup sequence. As a workaround, close it
    // automatically after a few seconds if it doesn't become key.
    // http://crbug.com/52726
    [self performSelector:@selector(closeIfNotKey) withObject:nil afterDelay:3];
  }
  return self;
}

- (void)awakeFromNib {
  DCHECK(header_);

  [[self bubble] setArrowLocation:info_bubble::kTopRight];

  // Adapt window size to contents. Do this before all other layouting.
  CGFloat dy = cocoa_l10n_util::VerticallyReflowGroup([[self bubble] subviews]);
  NSSize ds = NSMakeSize(0, dy);
  ds = [[self bubble] convertSize:ds toView:nil];

  NSRect frame = [[self window] frame];
  frame.origin.y -= ds.height;
  frame.size.height += ds.height;
  [[self window] setFrame:frame display:YES];
}

- (void)close {
  // If the window is closed before the timer is fired, cancel the timer, since
  // it retains the controller.
  [NSObject cancelPreviousPerformRequestsWithTarget:self
                                           selector:@selector(closeIfNotKey)
                                             object:nil];
  [other_ close];
  [super close];
}

- (void)closeIfNotKey {
  if (![[self window] isKeyWindow])
    [self close];
}

@end  // FacebookButtonBubbleController
