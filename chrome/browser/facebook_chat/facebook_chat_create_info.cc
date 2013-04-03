// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/facebook_chat/facebook_chat_create_info.h"

FacebookChatCreateInfo::FacebookChatCreateInfo(const std::string &jid,
    const std::string &username,
    const std::string &status)
      : jid(jid),
        username(username),
        status(status) {
}

FacebookChatCreateInfo::FacebookChatCreateInfo()
      : jid("#NONE#"),
        username("#NONE#"),
        status("offline") {
}

FacebookChatCreateInfo::~FacebookChatCreateInfo()
{
}

