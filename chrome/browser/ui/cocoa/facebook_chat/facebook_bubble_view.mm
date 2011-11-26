// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/facebook_chat/facebook_bubble_view.h"

@implementation FacebookBubbleView

@synthesize arrowLocation=arrowLocation_;

- (id)initWithFrame:(NSRect)frameRect {
  if ((self = [super initWithFrame:frameRect])) {
    // do member initialization here
    arrowLocation_ = fb_bubble::kBottomLeft;
    [self setBackgroundColor:[NSColor whiteColor]];
  }
  return self;
}

- (void)drawRect:(NSRect)rect {
  NSRect bounds = [self bounds];
  bounds.size.height -= fb_bubble::kBubbleArrowHeight;
  // we will only support bottom left arrow positioning as for now
  bounds.origin.y += fb_bubble::kBubbleArrowHeight;

  NSBezierPath *bezier = [NSBezierPath bezierPath];
  // Start with a rounded rectangle.
  [bezier appendBezierPathWithRoundedRect:bounds
      xRadius:fb_bubble::kBubbleCornerRadius
      yRadius:fb_bubble::kBubbleCornerRadius];

  // drawing an arrow at bottom left
  CGFloat dX = (arrowLocation_ == fb_bubble::kBottomLeft) ? 
      fb_bubble::kBubbleArrowXOffset :
      NSMidX(bounds) - NSMinX(bounds) - fb_bubble::kBubbleArrowWidth / 2;

  NSPoint arrowStart = NSMakePoint(NSMinX(bounds), NSMinY(bounds));
  arrowStart.x += dX;
  [bezier moveToPoint:arrowStart];
  [bezier lineToPoint:NSMakePoint(arrowStart.x + 
      fb_bubble::kBubbleArrowWidth / 2,
      arrowStart.y - fb_bubble::kBubbleArrowHeight)];
  [bezier lineToPoint:NSMakePoint(arrowStart.x +
      fb_bubble::kBubbleArrowWidth,
      arrowStart.y)];
  [bezier closePath];

  // fill the path with bg color
  [backgroundColor_ set];
  [bezier fill];
}

- (NSPoint)arrowTip {
  NSRect bounds = [self bounds];
  NSPoint tip = NSMakePoint((arrowLocation_ == fb_bubble::kBottomLeft) ?
      NSMinX(bounds) + fb_bubble::kBubbleArrowXOffset + 
        fb_bubble::kBubbleArrowWidth / 2 :
      NSMidX(bounds),
      NSMinY(bounds));
  return tip;
}

- (void)setBackgroundColor:(NSColor*)bgrColor {
  backgroundColor_.reset([bgrColor retain]);
}

@end
