// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_CHROMEOS_REGISTER_PAGE_UI_H_
#define CHROME_BROWSER_UI_WEBUI_CHROMEOS_REGISTER_PAGE_UI_H_
#pragma once

#include "content/browser/webui/web_ui.h"

// A custom WebUI that defines datasource for host registration page that
// is used in Chrome OS to register product on first sign in.
class RegisterPageUI : public WebUI {
 public:
  explicit RegisterPageUI(TabContents* contents);

 private:
  DISALLOW_COPY_AND_ASSIGN(RegisterPageUI);
};

#endif  // CHROME_BROWSER_UI_WEBUI_CHROMEOS_REGISTER_PAGE_UI_H_
