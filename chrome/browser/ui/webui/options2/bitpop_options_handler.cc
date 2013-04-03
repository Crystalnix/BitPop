// Copyright (c) 2012-2013 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012-2013 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/options2/bitpop_options_handler.h"

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/memory/singleton.h"
#include "base/path_service.h"
#include "base/stl_util.h"
#include "base/string_number_conversions.h"
#include "base/utf_string_conversions.h"
#include "base/value_conversions.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/net/url_fixer_upper.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/options/options_util.h"
#include "chrome/browser/ui/webui/chrome_url_data_manager.h"
#include "chrome/browser/ui/webui/favicon_source.h"
#include "chrome/browser/ui/webui/options2/bitpop_proxy_domain_settings_handler.h"
#include "chrome/browser/ui/webui/options2/bitpop_uncensor_filter_handler.h"
#include "chrome/browser/ui/webui/web_ui_util.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "grit/theme_resources.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebView.h"
#include "ui/base/l10n/l10n_util.h"

#if !defined(OS_CHROMEOS)
#include "chrome/browser/printing/cloud_print/cloud_print_setup_handler.h"
#include "chrome/browser/ui/webui/options2/advanced_options_utils.h"
#endif

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/accessibility/accessibility_util.h"
#include "chrome/browser/chromeos/extensions/wallpaper_manager_util.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/options/take_photo_dialog.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/webui/options2/chromeos/timezone_options_util.h"
#include "ui/gfx/image/image_skia.h"
#endif  // defined(OS_CHROMEOS)

#if defined(OS_WIN)
#include "chrome/installer/util/auto_launch_util.h"
#endif  // defined(OS_WIN)

#if defined(TOOLKIT_GTK)
#include "chrome/browser/ui/gtk/gtk_theme_service.h"
#endif  // defined(TOOLKIT_GTK)

using content::BrowserContext;
using content::BrowserThread;
using content::DownloadManager;
using content::OpenURLParams;
using content::Referrer;

namespace options2 {

BitpopOptionsHandler::BitpopOptionsHandler() {
}

BitpopOptionsHandler::~BitpopOptionsHandler() {
}

void BitpopOptionsHandler::GetLocalizedValues(DictionaryValue* values) {
  DCHECK(values);

  static OptionsStringResource resources[] = {
    { "bitpopOptionsPageTitle", IDS_BITPOP_SETTINGS_TITLE },

    { "askBeforeUsing", IDS_BITPOP_ASK_BEFORE_USING_PROXY },
    { "bitpopSettingsTitle", IDS_BITPOP_SETTINGS_TITLE },
    { "facebookShowChat", IDS_BITPOP_FACEBOOK_SHOW_CHAT_LABEL },
    { "facebookShowJewels", IDS_BITPOP_FACEBOOK_SHOW_JEWELS_LABEL },
    { "neverUseProxy", IDS_BITPOP_NEVER_USE_PROXY },
    { "openFacebookNotificationsOptions",
        IDS_BITPOP_FACEBOOK_OPEN_NOTIFICATION_SETTINGS },
    { "openProxyDomainSettings",
        IDS_BITPOP_OPEN_PROXY_DOMAIN_SETTINGS_BUTTON_TITLE },
    { "openUncensorFilterLists",
        IDS_BITPOP_UNCENSOR_OPEN_LIST_BUTTON_TITLE },
    { "sectionTitleBitpopFacebookSidebar",
        IDS_BITPOP_FACEBOOK_SIDEBAR_SECTION_TITLE },
    { "sectionTitleFacebookNotifications",
        IDS_BITPOP_FACEBOOK_NOTIFICATIONS_SECTION_TITLE },
    { "sectionTitleGlobalProxyControl",
        IDS_BITPOP_GLOBAL_PROXY_CONTROL_TITLE },
    { "sectionTitleUncensorFilterControl",
        IDS_BITPOP_UNCENSOR_FILTER_CONTROL },
    { "showMessageForActiveProxy",
        IDS_BITPOP_SHOW_MESSAGE_FOR_ACTIVE_PROXY },
    { "uncensorAlwaysRedirectOn",
        IDS_BITPOP_UNCENSOR_ALWAYS_REDIRECT },
    { "uncensorNeverRedirectOff",
        IDS_BITPOP_UNCENSOR_NEVER_REDIRECT },
    { "uncensorNotifyUpdates",
        IDS_BITPOP_UNCENSOR_NOTIFY_UPDATES },
    { "uncensorShowMessage",
        IDS_BITPOP_UNCENSOR_SHOW_MESSAGE },
    { "useAutoProxy", IDS_BITPOP_USE_AUTO_PROXY },
    { "whenToUseProxy", IDS_BITPOP_WHEN_TO_USE_PROXY },
  };

  RegisterStrings(values, resources, arraysize(resources));
}

void BitpopOptionsHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
    "openFacebookNotificationsOptions",
    base::Bind(&BitpopOptionsHandler::OpenFacebookNotificationsOptions,
               base::Unretained(this)));
}

void BitpopOptionsHandler::InitializeHandler() {
}

void BitpopOptionsHandler::InitializePage() {
  // OnTemplateURLServiceChanged();
  // ObserveThemeChanged();
  // OnStateChanged();
  // UpdateDefaultBrowserState();

  // SetupMetricsReportingCheckbox();
  // SetupMetricsReportingSettingVisibility();
  // SetupPasswordGenerationSettingVisibility();
  // SetupFontSizeSelector();
  // SetupPageZoomSelector();
  // SetupAutoOpenFileTypes();
  // SetupProxySettingsSection();
  // SetupSSLConfigSettings();
}

void BitpopOptionsHandler::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
}

void BitpopOptionsHandler::OpenFacebookNotificationsOptions(const base::ListValue* param) {
  // Open a new tab in the current window for the notifications page.
  OpenURLParams params(
      GURL("http://www.facebook.com/settings?tab=notifications"), Referrer(),
      NEW_FOREGROUND_TAB, content::PAGE_TRANSITION_LINK, false);
  web_ui()->GetWebContents()->OpenURL(params);
}

}  // namespace options2
