// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/facebook_chat/received_message_info.h"

#include "chrome/browser/facebook_chat/facebook_chat_create_info.h"

ReceivedMessageInfo::ReceivedMessageInfo(const std::string &jid,
    const std::string &username,
    const std::string &status,
    const std::string &message)
      : chatCreateInfo(new FacebookChatCreateInfo(jid, username, status)),
        message(message) {
}

ReceivedMessageInfo::ReceivedMessageInfo()
      : chatCreateInfo(new FacebookChatCreateInfo()),
        message("#EMPTY#") {
}

ReceivedMessageInfo::~ReceivedMessageInfo()
{
}

