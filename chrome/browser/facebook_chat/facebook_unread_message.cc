// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/facebook_chat/facebook_unread_message.h"

namespace {
  const int kSecondsToShow = 5;
}

FacebookUnreadMessage::FacebookUnreadMessage(const std::string &message)
  : message_(message),
    isVisible_(false) {
}

FacebookUnreadMessage::~FacebookUnreadMessage() {
}

void FacebookUnreadMessage::StartCountDown() {
  timer_.Start(TimeDelta::FromSeconds(kSecondsToShow), this, 
      &FacebookUnreadMessage::Hide);
}
