// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_CHROMEOS_MOBILE_SETUP_DIALOG_H_
#define CHROME_BROWSER_UI_WEBUI_CHROMEOS_MOBILE_SETUP_DIALOG_H_
#pragma once

#include <vector>

#include "base/memory/singleton.h"
#include "chrome/browser/ui/webui/html_dialog_ui.h"

class MobileSetupDialog {
 public:
  static void Show();

 private:
  DISALLOW_COPY_AND_ASSIGN(MobileSetupDialog);
};

#endif  // CHROME_BROWSER_UI_WEBUI_CHROMEOS_MOBILE_SETUP_DIALOG_H_
