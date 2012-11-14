// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_FACEBOOK_CHAT_FACEBOOK_CHAT_CREATE_INFO_H_
#define CHROME_BROWSER_FACEBOOK_CHAT_FACEBOOK_CHAT_CREATE_INFO_H_

#pragma once

#include <string>

struct FacebookChatCreateInfo {
  FacebookChatCreateInfo(const std::string &jid,
                         const std::string &username,
                         const std::string &status);
  FacebookChatCreateInfo();
  ~FacebookChatCreateInfo();

  std::string jid;
  std::string username;
  std::string status;
};

#endif  // CHROME_BROWSER_FACEBOOK_CHAT_FACEBOOK_CHAT_CREATE_INFO_H_

