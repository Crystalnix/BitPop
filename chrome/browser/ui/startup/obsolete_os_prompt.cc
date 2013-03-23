// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/startup/obsolete_os_prompt.h"

namespace chrome {

#if !defined(TOOLKIT_GTK)
void ShowObsoleteOSPrompt(Browser* browser) {
  // Only shown on Gtk.
}
#endif

}  // namespace chrome
