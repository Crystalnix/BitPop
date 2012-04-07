// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/keyboard_ui.h"

#include "base/memory/ref_counted_memory.h"
#include "base/memory/singleton.h"
#include "base/string_piece.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/chrome_url_data_manager.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "ui/base/resource/resource_bundle.h"

using content::WebContents;

///////////////////////////////////////////////////////////////////////////////
// KeyboardUI

KeyboardUI::KeyboardUI(content::WebUI* web_ui)
    : WebUIController(web_ui) {
  KeyboardHTMLSource* html_source = new KeyboardHTMLSource();
  Profile* profile = Profile::FromWebUI(web_ui);
  profile->GetChromeURLDataManager()->AddDataSource(html_source);
}

KeyboardUI::~KeyboardUI() {
}

///////////////////////////////////////////////////////////////////////////////
// KeyboardHTMLSource

KeyboardUI::KeyboardHTMLSource::KeyboardHTMLSource()
    : DataSource(chrome::kChromeUIKeyboardHost, MessageLoop::current()) {
}

void KeyboardUI::KeyboardHTMLSource::StartDataRequest(const std::string& path,
                                                      bool is_incognito,
                                                      int request_id) {
  NOTREACHED() << "We should never get here since the extension should have"
               << "been triggered";
  SendResponse(request_id, NULL);
}

std::string KeyboardUI::KeyboardHTMLSource::GetMimeType(
    const std::string&) const {
  NOTREACHED() << "We should never get here since the extension should have"
               << "been triggered";
  return "text/html";
}
