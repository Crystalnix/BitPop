// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/aura/client/stacking_client.h"

#include "ui/aura/root_window.h"

namespace aura {
namespace client {

const char kRootWindowStackingClient[] = "RootWindowStackingClient";

void SetStackingClient(StackingClient* stacking_client) {
  RootWindow::GetInstance()->SetProperty(kRootWindowStackingClient,
                                         stacking_client);
}

// static
StackingClient* GetStackingClient() {
  return reinterpret_cast<StackingClient*>(
      RootWindow::GetInstance()->GetProperty(kRootWindowStackingClient));
}

}  // namespace client
}  // namespace aura
