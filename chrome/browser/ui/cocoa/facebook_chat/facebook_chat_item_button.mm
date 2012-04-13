// Copyright (c) 2012 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/facebook_chat/facebook_chat_item_button.h"

#import "chrome/browser/ui/cocoa/facebook_chat/facebook_chat_item_controller.h"
#include "chrome/browser/facebook_chat/facebook_chat_item.h"

@implementation FacebookChatItemButton

- (void)drawRect:(NSRect)rect {
  [super drawRect:rect];

  if ([controller_ chatItem]->num_notifications() != 0) {
    NSRect buttonBounds = [self bounds];
    buttonBounds.origin.x += 1;
    buttonBounds.origin.y += 2;
    buttonBounds.size.width -= 2;
    buttonBounds.size.height -= 6;

    NSBezierPath* path = [NSBezierPath bezierPathWithRoundedRect:buttonBounds
                                                         xRadius:3.0
                                                         yRadius:3.0];
    NSColor* strokeColor = [NSColor colorWithCalibratedRed:0
                                                     green:0.7
                                                      blue:0.7
                                                     alpha:1.0];
    [strokeColor set];
    [path setLineWidth:2.0];
    [path stroke];
  }
}

@end

