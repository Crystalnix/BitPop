// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_TEST_CHROMEDRIVER_NET_SYNC_WEBSOCKET_H_
#define CHROME_TEST_CHROMEDRIVER_NET_SYNC_WEBSOCKET_H_

#include <string>

class GURL;

// Proxy for using a WebSocket running on a background thread synchronously.
class SyncWebSocket {
 public:
  virtual ~SyncWebSocket() {}

  // Connects to the WebSocket server. Returns true on success.
  virtual bool Connect(const GURL& url) = 0;

  // Sends message. Returns true on success.
  virtual bool Send(const std::string& message) = 0;

  // Receives next message. Blocks until at least one message is received or
  // the socket is closed. Returns true on success and modifies |message|.
  virtual bool ReceiveNextMessage(std::string* message) = 0;
};

#endif  // CHROME_TEST_CHROMEDRIVER_NET_SYNC_WEBSOCKET_H_
