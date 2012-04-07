// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_AURA_CLIENT_STACKING_CLIENT_H_
#define UI_AURA_CLIENT_STACKING_CLIENT_H_
#pragma once

#include "ui/aura/aura_export.h"

namespace aura {
class Window;
namespace client {

// A property key to store a client that handles window parenting. The type of
// the value is |aura::client::StackingClient*|.
AURA_EXPORT extern const char kRootWindowStackingClient[];

// An interface implemented by an object that stacks windows.
class AURA_EXPORT StackingClient {
 public:
  virtual ~StackingClient() {}

  // Called by the Window when its parent is set to NULL, returns the window
  // that |window| should be added to instead.
  virtual Window* GetDefaultParent(Window* window) = 0;
};

AURA_EXPORT void SetStackingClient(StackingClient* stacking_client);
AURA_EXPORT StackingClient* GetStackingClient();

}  // namespace client
}  // namespace aura

#endif  // UI_AURA_CLIENT_STACKING_CLIENT_H_
