// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/facebook_chat/facebook_chat_item_controller.h"

#include <string>

#include "base/mac/mac_util.h"
#include "base/sys_string_conversions.h"
#include "googleurl/src/gurl.h"
#include "chrome/browser/facebook_chat/facebook_chatbar.h"
#include "chrome/browser/facebook_chat/facebook_chat_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#import "chrome/browser/ui/cocoa/facebook_chat/facebook_chatbar_controller.h"
#import "chrome/browser/ui/cocoa/facebook_chat/facebook_popup_controller.h"
#import "chrome/browser/ui/cocoa/facebook_chat/facebook_notification_controller.h"
#include "chrome/common/url_constants.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/image.h"
#include "grit/theme_resources.h"
#include "grit/theme_resources_standard.h"

namespace {
  const int kButtonWidth = 163;
  const int kButtonHeight = 23;

  const CGFloat kNotificationWindowAnchorPointXOffset = 13.0;

  const CGFloat kChatWindowAnchorPointYOffset = 3.0;

  const CGFloat kBadgeImageDim = 14;

  NSImage *availableImage = nil;
  NSImage *idleImage = nil;
}

@interface FacebookChatItemController(Private)
+ (NSImage*)imageForNotificationBadgeWithNumber:(int)number;
@end


@implementation FacebookChatItemController

- (id)initWithModel:(FacebookChatItem*)downloadModel
            chatbar:(FacebookChatbarController*)chatbar {
  if ((self = [super initWithNibName:@"FacebookChatItem"
                              bundle:base::mac::MainAppBundle()])) {
    bridge_.reset(new FacebookChatItemMac(downloadModel, self));

    chatbarController_ = chatbar;
    active_ = downloadModel->needs_activation() ? YES : NO;

    showMouseEntered_ = NO;

    NSNotificationCenter* defaultCenter = [NSNotificationCenter defaultCenter];
    [[self view] setPostsFrameChangedNotifications:YES];
    [defaultCenter addObserver:self
                      selector:@selector(viewFrameDidChange:)
                          name:NSViewFrameDidChangeNotification
                        object:[self view]];
  }
  return self;
}

- (void)awakeFromNib {
  if (!availableImage || !idleImage) {
    ResourceBundle& rb = ResourceBundle::GetSharedInstance();
    availableImage = rb.GetNativeImageNamed(IDR_FACEBOOK_ONLINE_ICON_14);
    idleImage = rb.GetNativeImageNamed(IDR_FACEBOOK_IDLE_ICON_14);
  }

  [button_ setTitle:
        [NSString stringWithUTF8String:bridge_->chat()->username().c_str()]];
  [button_ setImagePosition:NSImageLeft];
  [self statusChanged];
  // int nNotifications = [self chatItem]->num_notifications();
  // if (nNotifications > 0)
  //   [self setUnreadMessagesNumber:nNotifications];
  buttonTrackingArea_.reset([[NSTrackingArea alloc] initWithRect:[button_ bounds]
            options: (NSTrackingMouseEnteredAndExited | NSTrackingActiveInKeyWindow )
            owner:self userInfo:nil]);
  [button_ addTrackingArea:buttonTrackingArea_];
}

- (void)dealloc {
  if (notificationController_.get()) {
    [notificationController_ parentControllerWillDie];
  }

  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [super dealloc];
}

- (IBAction)activateItemAction:(id)sender {
  [chatbarController_ activateItem:self];
}

- (IBAction)removeAction:(id)sender {
  [self chatItem]->Remove();
}

- (void)openChatWindow {
  GURL popupUrl = [self getPopupURL];
  NSPoint arrowPoint = [self popupPointForChatWindow];
  FacebookPopupController *fbpc =
    [FacebookPopupController showURL:popupUrl
                           inBrowser:[chatbarController_ bridge]->browser()
                          anchoredAt:arrowPoint
                       arrowLocation:fb_bubble::kBottomCenter
                             devMode:NO];
  NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
  [center addObserver:self
             selector:@selector(chatWindowWillClose:)
                 name:NSWindowWillCloseNotification
               object:[fbpc window]];

}

- (void)chatWindowWillClose:(NSNotification*)notification {
  [[NSNotificationCenter defaultCenter] removeObserver:self];

  [self setActive:NO];
}

- (NSSize)preferredSize {
  NSSize res;
  res.width = kButtonWidth;
  res.height = kButtonHeight;
  return res;
}

- (NSPoint)popupPointForChatWindow {
  if (!button_)
    return NSZeroPoint;
  if (![button_ isDescendantOf:[chatbarController_ view]])
    return NSZeroPoint;

  // Anchor point just above the center of the bottom.
  const NSRect bounds = [button_ bounds];
  DCHECK([button_ isFlipped]);
  NSPoint anchor = NSMakePoint(NSMidX(bounds),
                               NSMinY(bounds) - kChatWindowAnchorPointYOffset);
  return [button_ convertPoint:anchor toView:nil];
}

- (NSPoint)popupPointForNotificationWindow {
if (!button_)
    return NSZeroPoint;
  if (![button_ isDescendantOf:[chatbarController_ view]])
    return NSZeroPoint;

  // Anchor point just above the center of the bottom.
  const NSRect bounds = [button_ bounds];
  DCHECK([button_ isFlipped]);
  NSPoint anchor = NSMakePoint(NSMinX(bounds) +
                                 kNotificationWindowAnchorPointXOffset,
                               NSMinY(bounds) - kChatWindowAnchorPointYOffset);
  return [button_ convertPoint:anchor toView:nil];

}

- (GURL)getPopupURL {
  std::string urlString(chrome::kFacebookChatExtensionPrefixURL);
  urlString += chrome::kFacebookChatExtensionChatPage;
  urlString += "#";
  urlString += bridge_->chat()->jid();
  return GURL(urlString);
}

- (FacebookChatItem*)chatItem {
  return bridge_->chat();
}

- (void)remove {
  if ([self active])
    [[FacebookPopupController popup] close];
  if (notificationController_.get())
    [notificationController_ close];

  [chatbarController_ remove:self];
}

+ (NSImage*)imageForNotificationBadgeWithNumber:(int)number {
  NSImage *img = [[NSImage alloc] initWithSize:
      NSMakeSize(kBadgeImageDim, kBadgeImageDim)];
  [img lockFocus];

  NSRect badgeRect = NSMakeRect(0, 0, kBadgeImageDim, kBadgeImageDim);
  CGFloat badgeRadius = kBadgeImageDim / 2;

  NSBezierPath* badgeEdge = [NSBezierPath bezierPathWithOvalInRect:badgeRect];
  [[NSColor redColor] set];
  [badgeEdge fill];

  // Download count
  [[NSColor whiteColor] set];
  scoped_nsobject<NSNumberFormatter> formatter(
      [[NSNumberFormatter alloc] init]);
  NSString* countString =
      [formatter stringFromNumber:[NSNumber numberWithInt:number]];

  scoped_nsobject<NSShadow> countShadow([[NSShadow alloc] init]);
  [countShadow setShadowBlurRadius:3.0];
  [countShadow.get() setShadowColor:[NSColor whiteColor]];
  [countShadow.get() setShadowOffset:NSMakeSize(0.0, 0.0)];
  NSMutableDictionary* countAttrsDict =
      [NSMutableDictionary dictionaryWithObjectsAndKeys:
          [NSColor blackColor], NSForegroundColorAttributeName,
          countShadow.get(), NSShadowAttributeName,
          nil];
  CGFloat countFontSize = badgeRadius;
  NSSize countSize = NSZeroSize;
  scoped_nsobject<NSAttributedString> countAttrString;
  while (1) {
    NSFont* countFont = [NSFont fontWithName:@"Helvetica-Bold"
                                        size:countFontSize];
    [countAttrsDict setObject:countFont forKey:NSFontAttributeName];
    countAttrString.reset(
        [[NSAttributedString alloc] initWithString:countString
                                        attributes:countAttrsDict]);
    countSize = [countAttrString size];
    if (countSize.width > badgeRadius * 3) {
      countFontSize -= 1.0;
    } else {
      break;
    }
  }

  NSPoint countOrigin = NSMakePoint(kBadgeImageDim / 2, kBadgeImageDim / 2);
  countOrigin.x -= countSize.width / 2;
  countOrigin.y -= countSize.height / 2.2;  // tweak; otherwise too low

  [countAttrString.get() drawAtPoint:countOrigin];

  [img unlockFocus];

  return img;
}

- (void)setUnreadMessagesNumber:(int)number {
  if (number != 0) {
    NSImage *img = [FacebookChatItemController
        imageForNotificationBadgeWithNumber:number];
    [button_ setImage:img];
    [img release];

    [chatbarController_ placeFirstInOrder:self];

    if (!notificationController_.get()) {
      notificationController_.reset([[FacebookNotificationController alloc]
          initWithParentWindow:[chatbarController_ bridge]->
                                 browser()->window()->GetNativeHandle()
                    anchoredAt:[self popupPointForNotificationWindow]]);

    }
    std::string newMessage = [self chatItem]->GetMessageAtIndex(number-1);
    [notificationController_ messageReceived:
        base::SysUTF8ToNSString(newMessage)];
    //[button_ highlight:YES];
  } else {
    //[button_ highlight:NO];
    [self statusChanged];
    [notificationController_ close];
    notificationController_.reset(nil);
  }
}

- (void)statusChanged {
  if ([self chatItem]->num_notifications() == 0) {
    if ([self chatItem]->status() == FacebookChatItem::AVAILABLE)
      [button_ setImage:availableImage];
    else if ([self chatItem]->status() == FacebookChatItem::IDLE)
      [button_ setImage:idleImage];
    else
      [button_ setImage:nil];
  }
}

- (BOOL)active {
  return active_;
}

- (void)setActive:(BOOL)active {
  if (active) {
    if (notificationController_.get())
      [notificationController_ close];

    [self chatItem]->ClearUnreadMessages();
    [self openChatWindow];
  }

  active_ = active;
}

- (void)viewFrameDidChange:(NSNotification*)notification {
  [self layoutChildWindows];
}

- (void)layoutChildWindows {
    if ([self active] && [FacebookPopupController popup]) {
    [[FacebookPopupController popup] setAnchor:[self popupPointForChatWindow]];
  }

  if (notificationController_.get()) {
    [notificationController_ setAnchor:[self popupPointForNotificationWindow]];
  }
}

- (void)layedOutAfterAddingToChatbar {
  if ([self active])
    [self openChatWindow];

  int numNotifications = [self chatItem]->num_notifications();
  if (numNotifications > 0)
    [self setUnreadMessagesNumber:numNotifications];
}

- (void)mouseEntered:(NSEvent *)theEvent {
  if (notificationController_.get() &&
      [[notificationController_ window] isVisible] == NO) {
    [notificationController_ showWindow:self];
    showMouseEntered_ = YES;
  }
}

- (void)mouseExited:(NSEvent *)theEvent {
  if (showMouseEntered_) {
    [notificationController_ hideWindow];
    showMouseEntered_ = NO;
  }
}

- (void)switchParentWindow:(NSWindow*)window {
  if (notificationController_.get()) {
    [notificationController_ reparentWindowTo:window];
  }

  if ([self active] && [FacebookPopupController popup]) {
    [[FacebookPopupController popup] reparentWindowTo:window];
  }

  [self layoutChildWindows];
}

@end

