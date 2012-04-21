// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_FACEBOOK_CHAT_FACEBOOK_RECEIVED_MESSAGE_INFO_H_
#define CHROME_BROWSER_FACEBOOK_CHAT_FACEBOOK_RECEIVED_MESSAGE_INFO_H_

#pragma once

#include <string>

#include "base/memory/scoped_ptr.h"

struct FacebookChatCreateInfo;

struct ReceivedMessageInfo {
  ReceivedMessageInfo(const std::string &jid,
                         const std::string &username,
                         const std::string &status,
                         const std::string &message);
  ReceivedMessageInfo();
  ~ReceivedMessageInfo();

  scoped_ptr<FacebookChatCreateInfo> chatCreateInfo;
  std::string message;
};

#endif  // CHROME_BROWSER_FACEBOOK_CHAT_RECEIVED_MESSAGE_INFO_H_
