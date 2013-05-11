// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/facebook_chat/facebook_chat_item_controller.h"

#include <string>

#include "base/mac/bundle_locations.h"
#include "base/mac/mac_util.h"
#include "base/string_number_conversions.h"
#include "base/sys_string_conversions.h"
#include "chrome/browser/facebook_chat/facebook_chatbar.h"
#include "chrome/browser/facebook_chat/facebook_chat_manager.h"
#include "chrome/browser/facebook_chat/facebook_chat_manager_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#import "chrome/browser/ui/cocoa/facebook_chat/facebook_chatbar_controller.h"
#import "chrome/browser/ui/cocoa/facebook_chat/facebook_popup_controller.h"
#import "chrome/browser/ui/cocoa/facebook_chat/facebook_notification_controller.h"
#include "chrome/browser/ui/lion_badge_image_source.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/url_constants.h"
#include "googleurl/src/gurl.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "grit/ui_resources.h"
#include "skia/ext/skia_utils_mac.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia_util_mac.h"
#include "ui/gfx/skia_util.h"

namespace {
  const int kButtonWidth = 178;
  const int kButtonHeight = 36;

  const CGFloat kNotificationWindowAnchorPointXOffset = 13.0;

  const CGFloat kChatWindowAnchorPointYOffset = 0.0;

  NSImage *availableImage = nil;
  NSImage *idleImage = nil;
  NSImage *composingImage = nil;

  const int kNotifyIconDimX = 26;
  const int kNotifyIconDimY = 15;
}

@interface FacebookChatItemController(Private)
+ (NSImage*)imageForNotificationBadgeWithNumber:(int)number;
@end


@implementation FacebookChatItemController

- (id)initWithModel:(FacebookChatItem*)downloadModel
            chatbar:(FacebookChatbarController*)chatbar {
  if ((self = [super initWithNibName:@"FacebookChatItem"
                              bundle:base::mac::FrameworkBundle()])) {

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
  if (!availableImage || !idleImage || !composingImage) {
    ResourceBundle& rb = ResourceBundle::GetSharedInstance();
    availableImage =
    		rb.GetNativeImageNamed(IDR_FACEBOOK_ONLINE_ICON_14).ToNSImage();
    idleImage = rb.GetNativeImageNamed(IDR_FACEBOOK_IDLE_ICON_14).ToNSImage();
    composingImage =
    		rb.GetNativeImageNamed(IDR_FACEBOOK_COMPOSING_ICON_14).ToNSImage();
  }

  NSFont* font = [NSFont controlContentFontOfSize:11];
  [button_ setFont:font];

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
  [button_ setFrame:[[self view] bounds]];
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
                       arrowLocation:info_bubble::kBottomCenter];
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
  Profile *profile = [chatbarController_ bridge]->browser()->profile();
  FacebookChatManager *mgr =
      FacebookChatManagerServiceFactory::GetForProfile(profile);
  std::string urlString(chrome::kFacebookChatExtensionPrefixURL);
  urlString += chrome::kFacebookChatExtensionChatPage;
  urlString += "#";
  urlString += bridge_->chat()->jid();
  urlString += "&";
  urlString += mgr->global_my_uid();
  return GURL(urlString);
}

- (FacebookChatItem*)chatItem {
  return bridge_->chat();
}

- (void)remove {
  [self closeAllPopups];

  [chatbarController_ remove:self];
}

+ (NSImage*)imageForNotificationBadgeWithNumber:(int)number {
  if (number > 0) {

   if (number > 99)
      number = 99;
    std::string num = base::IntToString(number);

    LionBadgeImageSource* source = new LionBadgeImageSource(
            gfx::Size(kNotifyIconDimX, kNotifyIconDimY),
            num);

    return NSImageFromImageSkia(gfx::ImageSkia(source, source->size()));
  }

  return NULL;
}

- (void)setUnreadMessagesNumber:(int)number {
  if (number != 0) {
    NSImage *img = [FacebookChatItemController
                       imageForNotificationBadgeWithNumber:number];
    [button_ setImage:img];

    if (!notificationController_.get()) {
      notificationController_.reset([[FacebookNotificationController alloc]
          initWithParentWindow:[chatbarController_ bridge]->
                                 browser()->window()->GetNativeWindow()
                    anchoredAt:[self popupPointForNotificationWindow]]);
    }

    [chatbarController_ placeFirstInOrder:self];

    std::string newMessage = [self chatItem]->GetMessageAtIndex(number-1);
    [notificationController_ messageReceived:
        base::SysUTF8ToNSString(newMessage)];
  } else {
    [self statusChanged];
    [notificationController_ close];
    notificationController_.reset(nil);
  }
}

- (void)statusChanged {
  int numNotifications = [self chatItem]->num_notifications();
  FacebookChatItem::Status status = [self chatItem]->status();

  if (numNotifications == 0) {
    if (status == FacebookChatItem::AVAILABLE)
      [button_ setImage:availableImage];
    else if (status == FacebookChatItem::IDLE)
      [button_ setImage:idleImage];
    else
      [button_ setImage:nil];

    [button_ setNeedsDisplay:YES];
  } else if (status != FacebookChatItem::COMPOSING) {
    NSImage *img = [FacebookChatItemController
        imageForNotificationBadgeWithNumber:numNotifications];
    [button_ setImage:img];

    [button_ setNeedsDisplay:YES];
  }

  if (status == FacebookChatItem::COMPOSING) {
    [button_ setImage:composingImage];

    [button_ setNeedsDisplay:YES];
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
  if ([self active] && [FacebookPopupController popup] &&
      [[FacebookPopupController popup] window] &&
      [[[FacebookPopupController popup] window] isVisible]) {
    NSPoint p = [self popupPointForChatWindow];

    NSWindow* parentWindow = [[self view] window];
    p = [parentWindow convertBaseToScreen:p];

    [[FacebookPopupController popup] setAnchorPoint:p];
  }

  if (notificationController_.get() && [notificationController_ window] //&&
      //[[notificationController_ window] isVisible]
      ) {
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

- (void)closeAllPopups {
  if ([self active])
    [[FacebookPopupController popup] close];
  if (notificationController_.get())
    [notificationController_ close];
}

@end

