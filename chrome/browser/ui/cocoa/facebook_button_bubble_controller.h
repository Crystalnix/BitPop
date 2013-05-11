// Copyright (c) 2013 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2013 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <Cocoa/Cocoa.h>

#import "chrome/browser/ui/cocoa/first_run_bubble_controller.h"

class Browser;
class Profile;

// Manages the facebook button bubble.
@interface FacebookButtonBubbleController : BaseBubbleController {
 @private
  IBOutlet NSTextField* header_;
  Browser* browser_;
  Profile* profile_;
  FirstRunBubbleController* other_;
}

// Creates and shows a facebook button bubble.
+ (FacebookButtonBubbleController*)
			showForParentWindow:(NSWindow*)parentWindow
           		anchorPoint:(NSPoint)anchorPoint
	        				browser:(Browser*)browser
	        				profile:(Profile*)profile\
										other:(FirstRunBubbleController*)other;

@end
