// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_HOST_DISCONNECT_WINDOW_H
#define REMOTING_HOST_DISCONNECT_WINDOW_H

#include <string>

namespace remoting {

class ChromotingHost;

class DisconnectWindow {
 public:

  enum {
    kMaximumConnectedNameWidthInPixels = 400
  };

  virtual ~DisconnectWindow() {}

  // Show the disconnect window allowing the user to shut down |host|.
  virtual void Show(ChromotingHost* host, const std::string& username) = 0;

  // Hide the disconnect window.
  virtual void Hide() = 0;

  static DisconnectWindow* Create();
};

}

#endif  // REMOTING_HOST_DISCONNECT_WINDOW_H
