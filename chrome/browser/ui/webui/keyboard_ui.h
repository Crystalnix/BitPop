// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_KEYBOARD_UI_H_
#define CHROME_BROWSER_UI_WEBUI_KEYBOARD_UI_H_
#pragma once

#include <string>

#include "chrome/browser/ui/webui/chrome_url_data_manager.h"
#include "content/public/browser/web_ui_controller.h"

// The TabContents used for the keyboard page.
class KeyboardUI : public content::WebUIController {
 public:
  explicit KeyboardUI(content::WebUI* web_ui);
  virtual ~KeyboardUI();

  class KeyboardHTMLSource : public ChromeURLDataManager::DataSource {
   public:
    KeyboardHTMLSource();

    // Overrides from DataSource
    virtual void StartDataRequest(const std::string& path,
                                  bool is_incognito,
                                  int request_id) OVERRIDE;
    virtual std::string GetMimeType(const std::string&) const OVERRIDE;

   private:
    virtual ~KeyboardHTMLSource() {}

    DISALLOW_COPY_AND_ASSIGN(KeyboardHTMLSource);
  };

 private:
  DISALLOW_COPY_AND_ASSIGN(KeyboardUI);
};

#endif  // CHROME_BROWSER_UI_WEBUI_KEYBOARD_UI_H_
