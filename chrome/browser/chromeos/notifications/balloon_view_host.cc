// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/notifications/balloon_view_host.h"

#include "base/stl_util-inl.h"
#include "base/values.h"
#include "chrome/common/extensions/extension_messages.h"
#include "content/common/view_messages.h"
#include "ipc/ipc_message.h"
#include "ipc/ipc_message_macros.h"

namespace chromeos {

BalloonViewHost::BalloonViewHost(Balloon* balloon)
    : ::BalloonViewHost(balloon) {
}

BalloonViewHost::~BalloonViewHost() {
  STLDeleteContainerPairSecondPointers(message_callbacks_.begin(),
                                       message_callbacks_.end());
}

bool BalloonViewHost::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(BalloonViewHost, message)
    IPC_MESSAGE_HANDLER(ViewHostMsg_WebUISend, OnWebUISend)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

bool BalloonViewHost::AddWebUIMessageCallback(
    const std::string& message,
    MessageCallback* callback) {
  std::pair<MessageCallbackMap::iterator, bool> ret;
  ret = message_callbacks_.insert(std::make_pair(message, callback));
  if (!ret.second)
    delete callback;
  return ret.second;
}

void BalloonViewHost::OnWebUISend(const GURL& source_url,
                                  const std::string& name,
                                  const ListValue& args) {
  // Look up the callback for this message.
  MessageCallbackMap::const_iterator callback =
      message_callbacks_.find(name);
  if (callback == message_callbacks_.end())
    return;

  // Run callback.
  callback->second->Run(&args);
}

}  // namespace chromeos
