// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/bookmarks_ui.h"

#include "base/memory/ref_counted_memory.h"
#include "base/memory/singleton.h"
#include "base/message_loop.h"
#include "base/string_number_conversions.h"
#include "base/stringprintf.h"
#include "chrome/browser/bookmarks/bookmark_editor.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/bookmarks/bookmark_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/webui/chrome_url_data_manager.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "googleurl/src/gurl.h"
#include "grit/theme_resources.h"
#include "ui/base/resource/resource_bundle.h"

using content::WebContents;

////////////////////////////////////////////////////////////////////////////////
//
// BookmarksUIHTMLSource
//
////////////////////////////////////////////////////////////////////////////////

BookmarksUIHTMLSource::BookmarksUIHTMLSource()
    : DataSource(chrome::kChromeUIBookmarksHost, MessageLoop::current()) {
}

void BookmarksUIHTMLSource::StartDataRequest(const std::string& path,
                                             bool is_incognito,
                                             int request_id) {
  NOTREACHED() << "We should never get here since the extension should have"
               << "been triggered";

  SendResponse(request_id, NULL);
}

std::string BookmarksUIHTMLSource::GetMimeType(const std::string& path) const {
  NOTREACHED() << "We should never get here since the extension should have"
               << "been triggered";
  return "text/html";
}

BookmarksUIHTMLSource::~BookmarksUIHTMLSource() {}

////////////////////////////////////////////////////////////////////////////////
//
// BookmarksUI
//
////////////////////////////////////////////////////////////////////////////////

BookmarksUI::BookmarksUI(content::WebUI* web_ui) : WebUIController(web_ui) {
  BookmarksUIHTMLSource* html_source = new BookmarksUIHTMLSource();

  // Set up the chrome://bookmarks/ source.
  Profile* profile = Profile::FromWebUI(web_ui);
  ChromeURLDataManager::AddDataSource(profile, html_source);
}

// static
base::RefCountedMemory* BookmarksUI::GetFaviconResourceBytes(
      ui::ScaleFactor scale_factor) {
  return ResourceBundle::GetSharedInstance().
      LoadDataResourceBytesForScale(IDR_BOOKMARKS_FAVICON, scale_factor);
}
