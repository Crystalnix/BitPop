// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_FACEBOOK_CHAT_FACEBOOK_NOTIFICATION_VIEW_H_
#define CHROME_BROWSER_UI_COCOA_FACEBOOK_CHAT_FACEBOOK_NOTIFICATION_VIEW_H_
#pragma once

#import "chrome/browser/ui/cocoa/facebook_chat/facebook_bubble_view.h"

// Content view for a bubble with an arrow showing arbitrary content.
// This is where nonrectangular drawing happens.
@interface FacebookNotificationView : FacebookBubbleView {
 @private
//   CGFloat defaultWidth_;
   
   scoped_nsobject<NSTextStorage> textStorage_;
   NSLayoutManager *layoutManager_;
   NSTextContainer *textContainer_;

   scoped_nsobject<NSMutableArray> contentMessages_;
}

// The font used to display the content string
- (NSFont*)font;

//- (CGFloat)defaultWidth;
//- (void)setDefaultWidth:(CGFloat)width;

// Control the messages content as a queue
- (void)pushMessage:(NSString*)messageString;  // changes the view frame
- (NSString*)popMessage;                       // changes the view frame
- (NSUInteger)numMessagesRemaining;

@end


#endif // CHROME_BROWSER_UI_COCOA_FACEBOOK_CHAT_FACEBOOK_NOTIFICATION_VIEW_H_
