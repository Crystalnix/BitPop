// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/facebook_chat/facebook_chat_item_controller.h"

#import "chrome/browser/ui/cocoa/facebook_chat/facebook_chatbar_controller.h"

namespace {
  const int kButtonWidth = 200;
  const int kButtonHeight = 25;
}

@implementation FacebookChatItemController

- (id)initWithModel:(FacebookChatItem*)downloadModel
            chatbar:(FacebookChatbarController*)chatbar {
  if ((self = [super init])) {
    bridge_.reset(new FacebookChatItemMac(downloadModel, self));

    chatbarController_ = chatbar;

    NSRect buttonFrame = NSZeroRect;
    buttonFrame.size.width = kButtonWidth;
    buttonFrame.size.height = kButtonHeight;

    button_ = [[NSButton alloc] initWithFrame:buttonFrame];
    [button_ setBezelStyle:NSRoundedBezelStyle];
    [button_ setTitle:
        [NSString stringWithUTF8String:downloadModel->username().c_str()]];
    [button_ setTarget:self];
    [button_ setAction:@selector(remove:)];
    
    [self setView:button_];
  }
  return self;
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

