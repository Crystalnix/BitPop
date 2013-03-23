// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/welcome_ui_android.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/chrome_web_ui_data_source.h"
#include "chrome/common/url_constants.h"
#include "grit/browser_resources.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/google_chrome_strings.h"

WelcomeUI::WelcomeUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  // Set up the chrome://welcome source.
  ChromeWebUIDataSource* html_source =
      new ChromeWebUIDataSource(chrome::kChromeUIWelcomeHost);
  html_source->set_use_json_js_format_v2();

  // Localized strings.
  html_source->AddLocalizedString("title",
      IDS_NEW_TAB_CHROME_WELCOME_PAGE_TITLE);
  html_source->AddLocalizedString("takeATour", IDS_FIRSTRUN_TAKE_TOUR);
  html_source->AddLocalizedString("firstRunSignedIn", IDS_FIRSTRUN_SIGNED_IN);
  html_source->AddLocalizedString("settings", IDS_FIRSTRUN_SETTINGS_LINK);

  // Add required resources.
  html_source->set_json_path("strings.js");
  html_source->add_resource_path("about_welcome_android.css",
      IDR_ABOUT_WELCOME_CSS);
  html_source->add_resource_path("about_welcome_android.js",
      IDR_ABOUT_WELCOME_JS);
  html_source->set_default_resource(IDR_ABOUT_WELCOME_HTML);

  Profile* profile = Profile::FromWebUI(web_ui);
  ChromeURLDataManager::AddDataSource(profile, html_source);
}

WelcomeUI::~WelcomeUI() {
}
