// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_WEB_UI_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_WEB_UI_H_
#pragma once

#include <string>

#include "base/memory/scoped_ptr.h"
#include "chrome/browser/bookmarks/bookmark_manager_extension_api.h"
#include "chrome/browser/favicon/favicon_service.h"
#include "chrome/common/extensions/extension.h"
#include "content/public/browser/web_ui_controller.h"

class PrefService;
class Profile;

// This class implements WebUI for extensions and allows extensions to put UI in
// the main tab contents area. For example, each extension can specify an
// "options_page", and that page is displayed in the tab contents area and is
// hosted by this class.
class ExtensionWebUI : public content::WebUIController {
 public:
  static const char kExtensionURLOverrides[];

  ExtensionWebUI(content::WebUI* web_ui, const GURL& url);

  virtual ~ExtensionWebUI();

  virtual BookmarkManagerExtensionEventRouter*
      bookmark_manager_extension_event_router();

  // BrowserURLHandler
  static bool HandleChromeURLOverride(GURL* url,
                                      content::BrowserContext* browser_context);
  static bool HandleChromeURLOverrideReverse(
      GURL* url, content::BrowserContext* browser_context);

  // Register and unregister a dictionary of one or more overrides.
  // Page names are the keys, and chrome-extension: URLs are the values.
  // (e.g. { "newtab": "chrome-extension://<id>/my_new_tab.html" }
  static void RegisterChromeURLOverrides(Profile* profile,
      const Extension::URLOverrideMap& overrides);
  static void UnregisterChromeURLOverrides(Profile* profile,
      const Extension::URLOverrideMap& overrides);
  static void UnregisterChromeURLOverride(const std::string& page,
                                          Profile* profile,
                                          base::Value* override);

  // Called from BrowserPrefs
  static void RegisterUserPrefs(PrefService* prefs);

  // Get the favicon for the extension by getting an icon from the manifest.
  static void GetFaviconForURL(Profile* profile,
                               FaviconService::GetFaviconRequest* request,
                               const GURL& page_url);

 private:
  // Unregister the specified override, and if it's the currently active one,
  // ensure that something takes its place.
  static void UnregisterAndReplaceOverride(const std::string& page,
                                           Profile* profile,
                                           base::ListValue* list,
                                           base::Value* override);

  // TODO(aa): This seems out of place. Why is it not with the event routers for
  // the other extension APIs?
  scoped_ptr<BookmarkManagerExtensionEventRouter>
      bookmark_manager_extension_event_router_;

  // The URL this WebUI was created for.
  GURL url_;
};

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_WEB_UI_H_
