// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/find_bar_host.h"

void FindBarHost::AudibleAlert() {
  // TODO(davemoore) implement.
  NOTIMPLEMENTED();
}

bool FindBarHost::ShouldForwardKeyEventToWebpageNative(
    const views::KeyEvent& key_event) {
  return true;
}
