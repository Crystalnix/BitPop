// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/facebook_chat/facebook_chat_item_controller.h"

#include <string>

#include "base/mac/mac_util.h"
#include "googleurl/src/gurl.h"
#include "chrome/browser/facebook_chat/facebook_chatbar.h"
#import "chrome/browser/ui/cocoa/facebook_chat/facebook_chatbar_controller.h"
#import "chrome/browser/ui/cocoa/extensions/extension_popup_controller.h"
#include "chrome/common/url_constants.h"

namespace {
  const int kButtonWidth = 163;
  const int kButtonHeight = 23;

  const CGFloat kChatWindowAnchorPointYOffset = 3.0;
}

@implementation FacebookChatItemController

- (id)initWithModel:(FacebookChatItem*)downloadModel
            chatbar:(FacebookChatbarController*)chatbar {
  if ((self = [super initWithNibName:@"FacebookChatItem"
                              bundle:base::mac::MainAppBundle()])) {
    bridge_.reset(new FacebookChatItemMac(downloadModel, self));

    chatbarController_ = chatbar;
  }
  return self;
}

- (void)awakeFromNib {
  [button_ setTitle:
        [NSString stringWithUTF8String:bridge_->chat()->username().c_str()]];
}

- (void)openChatWindow:(id)sender {
  GURL popupUrl = [self getPopupURL];
  NSPoint arrowPoint = [self popupPointForChatWindow];
  [ExtensionPopupController showURL:popupUrl
                          inBrowser:[chatbarController_ bridge]->browser()
                         anchoredAt:arrowPoint
                      arrowLocation:info_bubble::kBottomCenter
                            devMode:NO];
}

- (void)remove:(id)sender {
  [chatbarController_ remove:self];
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

- (GURL)getPopupURL {
  std::string urlString(chrome::kFacebookChatExtensionPrefixURL);
  urlString += chrome::kFacebookChatExtensionChatPage;
  urlString += "#";
  urlString += bridge_->chat()->jid();
  return GURL(urlString);
}

@end

