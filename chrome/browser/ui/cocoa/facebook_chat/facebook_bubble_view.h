// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_FACEBOOK_CHAT_FACEBOOK_BUBBLE_VIEW_H_
#define CHROME_BROWSER_UI_COCOA_FACEBOOK_CHAT_FACEBOOK_BUBBLE_VIEW_H_
#pragma once

#import <Cocoa/Cocoa.h>

#include "base/memory/scoped_nsobject.h"

namespace fb_bubble {

const CGFloat kBubbleArrowHeight = 8.0;
const CGFloat kBubbleArrowWidth = 15.0;
const CGFloat kBubbleCornerRadius = 8.0;
const CGFloat kBubbleArrowXOffset = kBubbleArrowWidth + kBubbleCornerRadius;

enum BubbleArrowLocation {
  kBottomLeft,
  kBottomCenter,
};

}  // namespace info_bubble

// Content view for a bubble with an arrow showing arbitrary content.
// This is where nonrectangular drawing happens.
@interface FacebookBubbleView : NSView {
 @private
   fb_bubble::BubbleArrowLocation arrowLocation_;
   scoped_nsobject<NSColor> backgroundColor_;
}

@property(assign, nonatomic) fb_bubble::BubbleArrowLocation arrowLocation;

- (void)setBackgroundColor:(NSColor*)bgrColor;

// Returns the point location in view coordinates of the tip of the arrow.
- (NSPoint)arrowTip;

@end


#endif // CHROME_BROWSER_UI_COCOA_FACEBOOK_CHAT_FACEBOOK_BUBBLE_VIEW_H_
