// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_PLUGINS_UI_H_
#define CHROME_BROWSER_UI_WEBUI_PLUGINS_UI_H_
#pragma once

#include "content/public/browser/web_ui_controller.h"

class PrefService;
class RefCountedMemory;

class PluginsUI : public content::WebUIController {
 public:
  explicit PluginsUI(content::WebUI* web_ui);

  static RefCountedMemory* GetFaviconResourceBytes();
  static void RegisterUserPrefs(PrefService* prefs);

 private:
  DISALLOW_COPY_AND_ASSIGN(PluginsUI);
};

#endif  // CHROME_BROWSER_UI_WEBUI_PLUGINS_UI_H_
