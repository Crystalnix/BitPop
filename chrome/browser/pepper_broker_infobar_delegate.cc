// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/pepper_broker_infobar_delegate.h"

#include "chrome/browser/content_settings/host_content_settings_map.h"
#include "chrome/browser/infobars/infobar_tab_helper.h"
#include "chrome/browser/plugins/plugin_finder.h"
#include "chrome/browser/plugins/plugin_metadata.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/plugin_service.h"
#include "content/public/browser/user_metrics.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "net/base/net_util.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "webkit/plugins/webplugininfo.h"

// The URL for the "learn more" article about the PPAPI broker.
const char kPpapiBrokerLearnMoreUrl[] =
    "https://support.google.com/chrome/?p=ib_pepper_broker";

using content::OpenURLParams;
using content::Referrer;
using content::WebContents;

// static
void PepperBrokerInfoBarDelegate::Show(
    WebContents* web_contents,
    const GURL& url,
    const FilePath& plugin_path,
    const base::Callback<void(bool)>& callback) {
  Profile* profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  // TODO(wad): Add ephemeral device ID support for broker in guest mode.
  if (profile->IsGuestSession()) {
    callback.Run(false);
    return;
  }

  HostContentSettingsMap* content_settings =
      profile->GetHostContentSettingsMap();
  ContentSetting setting =
      content_settings->GetContentSetting(url, url,
                                          CONTENT_SETTINGS_TYPE_PPAPI_BROKER,
                                          std::string());
  switch (setting) {
    case CONTENT_SETTING_ALLOW: {
      content::RecordAction(
          content::UserMetricsAction("PPAPI.BrokerSettingAllow"));
      callback.Run(true);
      break;
    }
    case CONTENT_SETTING_BLOCK: {
      content::RecordAction(
          content::UserMetricsAction("PPAPI.BrokerSettingDeny"));
      callback.Run(false);
      break;
    }
    case CONTENT_SETTING_ASK: {
      content::RecordAction(
          content::UserMetricsAction("PPAPI.BrokerInfobarDisplayed"));

      InfoBarTabHelper* infobar_helper =
          InfoBarTabHelper::FromWebContents(web_contents);
      std::string languages =
          profile->GetPrefs()->GetString(prefs::kAcceptLanguages);
      infobar_helper->AddInfoBar(
          new PepperBrokerInfoBarDelegate(
              infobar_helper, url, plugin_path, languages, content_settings,
              callback));
      break;
    }
    default:
      NOTREACHED();
  }
}

PepperBrokerInfoBarDelegate::PepperBrokerInfoBarDelegate(
    InfoBarTabHelper* helper,
    const GURL& url,
    const FilePath& plugin_path,
    const std::string& languages,
    HostContentSettingsMap* content_settings,
    const base::Callback<void(bool)>& callback)
    : ConfirmInfoBarDelegate(helper),
      url_(url),
      plugin_path_(plugin_path),
      languages_(languages),
      content_settings_(content_settings),
      callback_(callback) {
}

PepperBrokerInfoBarDelegate::~PepperBrokerInfoBarDelegate() {
  if (!callback_.is_null())
    callback_.Run(false);
}

string16 PepperBrokerInfoBarDelegate::GetMessageText() const {
  content::PluginService* plugin_service =
      content::PluginService::GetInstance();
  webkit::WebPluginInfo plugin;
  bool success = plugin_service->GetPluginInfoByPath(plugin_path_, &plugin);
  DCHECK(success);
  scoped_ptr<PluginMetadata> plugin_metadata(
      PluginFinder::GetInstance()->GetPluginMetadata(plugin));
  return l10n_util::GetStringFUTF16(IDS_PEPPER_BROKER_MESSAGE,
                                    plugin_metadata->name(),
                                    net::FormatUrl(url_.GetOrigin(),
                                                   languages_));
}

int PepperBrokerInfoBarDelegate::GetButtons() const {
  return BUTTON_OK | BUTTON_CANCEL;
}

string16 PepperBrokerInfoBarDelegate::GetButtonLabel(
    InfoBarButton button) const {
  switch (button) {
    case BUTTON_OK:
      return l10n_util::GetStringUTF16(IDS_PEPPER_BROKER_ALLOW_BUTTON);
    case BUTTON_CANCEL:
      return l10n_util::GetStringUTF16(IDS_PEPPER_BROKER_DENY_BUTTON);
    default:
      NOTREACHED();
      return string16();
  }
}

bool PepperBrokerInfoBarDelegate::Accept() {
  DispatchCallback(true);
  return true;
}

bool PepperBrokerInfoBarDelegate::Cancel() {
  DispatchCallback(false);
  return true;
}

string16 PepperBrokerInfoBarDelegate::GetLinkText() const {
  return l10n_util::GetStringUTF16(IDS_LEARN_MORE);
}

bool PepperBrokerInfoBarDelegate::LinkClicked(
    WindowOpenDisposition disposition) {
  OpenURLParams params(
      GURL(kPpapiBrokerLearnMoreUrl), Referrer(),
      (disposition == CURRENT_TAB) ? NEW_FOREGROUND_TAB : disposition,
      content::PAGE_TRANSITION_LINK,
      false);
  owner()->GetWebContents()->OpenURL(params);
  return false;
}

gfx::Image* PepperBrokerInfoBarDelegate::GetIcon() const {
  return &ResourceBundle::GetSharedInstance().GetNativeImageNamed(
      IDR_INFOBAR_PLUGIN_INSTALL);
}

void PepperBrokerInfoBarDelegate::DispatchCallback(bool result) {
  content::RecordAction(result ?
      content::UserMetricsAction("PPAPI.BrokerInfobarClickedAllow") :
      content::UserMetricsAction("PPAPI.BrokerInfobarClickedDeny"));
  callback_.Run(result);
  callback_ = base::Callback<void(bool)>();
  content_settings_->SetContentSetting(
      ContentSettingsPattern::FromURLNoWildcard(url_),
      ContentSettingsPattern::Wildcard(),
      CONTENT_SETTINGS_TYPE_PPAPI_BROKER,
      std::string(), result ? CONTENT_SETTING_ALLOW : CONTENT_SETTING_BLOCK);
}
