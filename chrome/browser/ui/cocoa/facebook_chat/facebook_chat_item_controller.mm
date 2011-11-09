// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/facebook_chat/facebook_chat_item_controller.h"

#include "base/mac/mac_util.h"
#import "chrome/browser/ui/cocoa/facebook_chat/facebook_chatbar_controller.h"

namespace {
  const int kButtonWidth = 163;
  const int kButtonHeight = 23;
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

@end

