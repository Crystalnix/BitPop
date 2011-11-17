// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/facebook_chat/facebook_chatbar_controller.h"

#include "base/mac/mac_util.h"
#import "chrome/browser/ui/cocoa/facebook_chat/facebook_chatbar_mac.h"
#import "chrome/browser/ui/cocoa/facebook_chat/facebook_chat_item_controller.h"
#include "chrome/browser/facebook_chat/facebook_chat_item.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#import "chrome/browser/ui/cocoa/hover_close_button.h"

namespace {
// The size of the x button by default.
const NSSize kHoverCloseButtonDefaultSize = { 16, 16 };

const NSUInteger kMaxChatItemCount = 10;

const NSInteger kChatItemPadding = 10;

const NSInteger kChatbarHeight = 44;
}  // namespace

@interface FacebookChatbarController(Private)

- (void)layoutItems:(BOOL)skipFirst;
- (void)showChatbar:(BOOL)enable;
- (void)updateCloseButton;

@end

@implementation FacebookChatbarController

- (id)initWithBrowser:(Browser*)browser
       resizeDelegate:(id<ViewResizer>)resizeDelegate {
  if ((self = [super initWithNibName:@"FacebookChatbar"
                              bundle:base::mac::MainAppBundle()])) {
    resizeDelegate_ = resizeDelegate;
    maxBarHeight_ = NSHeight([[self view] bounds]);
    //currentShelfHeight_ = maxShelfHeight_;
    if (browser && browser->window())
      isFullscreen_ = browser->window()->IsFullscreen();
    else
      isFullscreen_ = NO;

    // Reset the download shelf's frame height to zero.  It will be properly
    // positioned and sized the first time we try to set its height. (Just
    // setting the rect to NSZeroRect does not work: it confuses Cocoa's view
    // layout logic. If the shelf's width is too small, cocoa makes the download
    // item container view wider than the browser window).
    NSRect frame = [[self view] frame];
    frame.size.height = 0;
    [[self view] setFrame:frame];

    NSNotificationCenter* defaultCenter = [NSNotificationCenter defaultCenter];
    [[self view] setPostsFrameChangedNotifications:YES];
    [defaultCenter addObserver:self
                      selector:@selector(viewFrameDidChange:)
                          name:NSViewFrameDidChangeNotification
                        object:[self view]];

    chatItemControllers_.reset([[NSMutableArray alloc] init]);

    bridge_.reset(new FacebookChatbarMac(browser, self));
  }
  return self;
}

- (BOOL)isVisible {
  return barIsVisible_;
}

- (void)showChatbar:(BOOL)enable {
  if ([self isVisible] == enable)
    return;

  if (enable)
    [resizeDelegate_ resizeView:self.view newHeight:maxBarHeight_];
  else
    [resizeDelegate_ resizeView:self.view newHeight:0];

  barIsVisible_ = enable;
  [self updateCloseButton];
}

- (void)show:(id)sender {
  [self showChatbar:YES];
}

- (void)hide:(id)sender {
  if (sender)
    bridge_->Hide();
  else
    [self showChatbar:NO];
}

- (FacebookChatbar*)bridge {
  return bridge_.get();
}

- (void)updateCloseButton {
  if (!barIsVisible_)
    return;

  NSRect selfBounds = [[self view] bounds];
  NSRect hoverFrame = [hoverCloseButton_ frame];
  NSRect bounds;

  if (isFullscreen_) {
    bounds = NSMakeRect(NSMinX(hoverFrame), 0,
                        selfBounds.size.width - NSMinX(hoverFrame),
                        selfBounds.size.height);
  } else {
    bounds.origin.x = NSMinX(hoverFrame);
    bounds.origin.y = NSMidY(hoverFrame) -
                      kHoverCloseButtonDefaultSize.height / 2.0;
    bounds.size = kHoverCloseButtonDefaultSize;
  }

  // Set the tracking off to create a new tracking area for the control.
  // When changing the bounds/frame on a HoverButton, the tracking isn't updated
  // correctly, it needs to be turned off and back on.
  [hoverCloseButton_ setTrackingEnabled:NO];
  [hoverCloseButton_ setFrame:bounds];
  [hoverCloseButton_ setTrackingEnabled:YES];
}

- (void)addChatItem:(FacebookChatItem*)item {
  DCHECK([NSThread isMainThread]);

  if (![self isVisible])
    [self show:nil];
    
  // Do not place an item with existing jid to the chat item controllers
  for (FacebookChatItemController *contr in chatItemControllers_.get())
    if ([contr chatItem]->jid() == item->jid())
      return;

  // Insert new item at the left.
  scoped_nsobject<FacebookChatItemController> controller(
      [[FacebookChatItemController alloc] initWithModel:item chatbar:self]);

  // Adding at index 0 in NSMutableArrays is O(1).
  [chatItemControllers_ insertObject:controller.get() atIndex:0];

  [[self view] addSubview:[controller view]];

  NSRect rc = [[controller view] frame];
  rc.size = [controller preferredSize];
  rc.origin.y = kChatbarHeight / 2 - rc.size.height / 2;
  [[controller view] setFrame:rc];

  // Keep only a limited number of items in the shelf.
  if ([chatItemControllers_ count] > kMaxChatItemCount) {
    DCHECK(kMaxChatItemCount > 0);

    // Since no user will ever see the item being removed (needs a horizontal
    // screen resolution greater than 3200 at 16 items at 200 pixels each),
    // there's no point in animating the removal.
    [self remove:[chatItemControllers_ lastObject]];
  }

  // Finally, move the remaining items to the right. Skip the first item when
  // laying out the items, so that the longer animation duration we set up above
  // is not overwritten.
  [self layoutItems:YES];

  if ([controller active])
    [controller openChatWindow];
}

- (void)activateItem:(FacebookChatItemController*)chatItem {
  for (FacebookChatItemController *controller in chatItemControllers_.get()) {
    if (controller == chatItem) {
      [controller setActive:YES];
    }
    else
      [controller setActive:NO];
  }
}

- (void)remove:(FacebookChatItemController*)chatItem {
  [[chatItem view] removeFromSuperview];

  [chatItemControllers_ removeObject:chatItem];

  [self layoutItems];

  // Check to see if we have any downloads remaining and if not, hide the shelf.
  if (![chatItemControllers_ count])
    [self showChatbar:NO];
}

- (void)layoutItems:(BOOL)skipFirst {
  CGFloat currentX = 0;
  for (FacebookChatItemController* itemController
      in chatItemControllers_.get()) {
    NSRect frame = [[itemController view] frame];
    frame.origin.x = ([[self view] bounds].size.width - 30 -
        [itemController preferredSize].width) - currentX;
    if (frame.origin.x < 10) {
      [[itemController view] setHidden:YES];
    } else if ([[itemController view] isHidden] == YES) {
      [[itemController view] setHidden:NO];
    }

    frame.size.width = [itemController preferredSize].width;
    //if (!skipFirst)
    [[itemController view] setFrame:frame];
    currentX += frame.size.width + kChatItemPadding;
    //skipFirst = NO;
  }

}

- (void)layoutItems {
  [self layoutItems:NO];
}

- (void)viewFrameDidChange:(NSNotification*)notification {
  [self layoutItems];
}

@end
