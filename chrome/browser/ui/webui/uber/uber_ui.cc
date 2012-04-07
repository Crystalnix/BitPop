// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/uber/uber_ui.h"

#include "base/stl_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/chrome_url_data_manager.h"
#include "chrome/browser/ui/webui/chrome_web_ui_data_source.h"
#include "chrome/browser/ui/webui/chrome_web_ui_controller_factory.h"
#include "chrome/browser/ui/webui/extensions/extensions_ui.h"
#include "chrome/browser/ui/webui/options2/options_ui2.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "grit/browser_resources.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"

using content::WebContents;

namespace {

ChromeWebUIDataSource* CreateUberHTMLSource() {
  ChromeWebUIDataSource* source =
      new ChromeWebUIDataSource(chrome::kChromeUIUberHost);

  source->set_json_path("strings.js");
  source->add_resource_path("uber.js", IDR_UBER_JS);
  source->add_resource_path("uber_utils.js", IDR_UBER_UTILS_JS);
  source->set_default_resource(IDR_UBER_HTML);

  // Hack alert: continue showing "Loading..." until a real title is set.
  source->AddLocalizedString("pageTitle", IDS_TAB_LOADING_TITLE);

  source->AddString("settingsHost",
                    ASCIIToUTF16(chrome::kChromeUISettingsHost));
  source->AddString("extensionsHost",
                    ASCIIToUTF16(chrome::kChromeUIExtensionsHost));

#if defined(OS_CHROMEOS)
  source->AddString("aboutPageHost",
                    ASCIIToUTF16(chrome::kAboutOptionsSubPage));
#endif
  return source;
}

ChromeWebUIDataSource* CreateUberFrameHTMLSource() {
  ChromeWebUIDataSource* source =
      new ChromeWebUIDataSource(chrome::kChromeUIUberFrameHost);

  source->set_json_path("strings.js");
  source->add_resource_path("uber_frame.js", IDR_UBER_FRAME_JS);
  source->set_default_resource(IDR_UBER_FRAME_HTML);

  source->AddLocalizedString("shortProductName", IDS_SHORT_PRODUCT_NAME);

  source->AddString("settingsHost",
                    ASCIIToUTF16(chrome::kChromeUISettingsHost));
  source->AddLocalizedString("settingsDisplayName", IDS_SETTINGS_TITLE);
  source->AddString("extensionsHost",
                    ASCIIToUTF16(chrome::kChromeUIExtensionsHost));
  source->AddLocalizedString("extensionsDisplayName",
                             IDS_MANAGE_EXTENSIONS_SETTING_WINDOWS_TITLE);
#if defined(OS_CHROMEOS)
  source->AddString("aboutPageHost",
                    ASCIIToUTF16(chrome::kAboutOptionsSubPage));
  source->AddLocalizedString("aboutPageDisplayName", IDS_ABOUT_TAB_TITLE);
#endif

  return source;
}

}  // namespace

UberUI::UberUI(content::WebUI* web_ui) : WebUIController(web_ui) {
  Profile* profile = Profile::FromWebUI(web_ui);
  profile->GetChromeURLDataManager()->AddDataSource(CreateUberHTMLSource());

  RegisterSubpage(chrome::kChromeUIUberFrameURL);
  RegisterSubpage(chrome::kChromeUISettingsFrameURL);
  RegisterSubpage(chrome::kChromeUIExtensionsFrameURL);
#if defined(OS_CHROMEOS)
  RegisterSubpage(chrome::kChromeUIAboutPageFrameURL);
#endif
}

UberUI::~UberUI() {
  STLDeleteValues(&sub_uis_);
}

void UberUI::RegisterSubpage(const std::string& page_url) {
  content::WebUI* webui =
      web_ui()->GetWebContents()->CreateWebUI(GURL(page_url));

  webui->SetFrameXPath("//iframe[@src='" + page_url + "']");
  sub_uis_[page_url] = webui;
}

void UberUI::RenderViewCreated(RenderViewHost* render_view_host) {
  for (SubpageMap::iterator iter = sub_uis_.begin(); iter != sub_uis_.end();
       ++iter) {
    iter->second->GetController()->RenderViewCreated(render_view_host);
  }
}

void UberUI::RenderViewReused(RenderViewHost* render_view_host) {
  for (SubpageMap::iterator iter = sub_uis_.begin(); iter != sub_uis_.end();
       ++iter) {
    iter->second->GetController()->RenderViewReused(render_view_host);
  }
}

void UberUI::DidBecomeActiveForReusedRenderView() {
  for (SubpageMap::iterator iter = sub_uis_.begin(); iter != sub_uis_.end();
       ++iter) {
    iter->second->GetController()->DidBecomeActiveForReusedRenderView();
  }
}

bool UberUI::OverrideHandleWebUIMessage(const GURL& source_url,
                                        const std::string& message,
                                        const ListValue& args) {
  // Find the appropriate subpage and forward the message.
  SubpageMap::iterator subpage = sub_uis_.find(source_url.GetOrigin().spec());
  if (subpage == sub_uis_.end()) {
    // The message was sent from the uber page itself.
    DCHECK_EQ(std::string(chrome::kChromeUIUberHost), source_url.host());
    return false;
  }

  // The message was sent from a subpage.
  // TODO(jam) fix this to use interface
  //return subpage->second->GetController()->OverrideHandleWebUIMessage(
    //  source_url, message, args);
  subpage->second->ProcessWebUIMessage(source_url, message, args);
  return true;
}

// UberFrameUI

UberFrameUI::UberFrameUI(content::WebUI* web_ui) : WebUIController(web_ui) {
  Profile* profile = Profile::FromWebUI(web_ui);
  profile->GetChromeURLDataManager()->AddDataSource(
      CreateUberFrameHTMLSource());
}

UberFrameUI::~UberFrameUI() {
}
