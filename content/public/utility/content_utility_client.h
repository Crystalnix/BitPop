// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_UTILITY_CONTENT_UTILITY_CLIENT_H_
#define CONTENT_PUBLIC_UTILITY_CONTENT_UTILITY_CLIENT_H_
#pragma once

#include "content/public/common/content_client.h"

namespace content {

// Embedder API for participating in renderer logic.
class ContentUtilityClient {
 public:
  // Notifies us that the UtilityThread has been created.
  virtual void UtilityThreadStarted() = 0;

  // Allows the embedder to filter messages.
  virtual bool OnMessageReceived(const IPC::Message& message) = 0;
};

}  // namespace content

#endif  // CONTENT_PUBLIC_UTILITY_CONTENT_UTILITY_CLIENT_H_
