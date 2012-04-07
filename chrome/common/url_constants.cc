// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/url_constants.h"

#include "googleurl/src/url_util.h"

namespace {
const char* kSavableSchemes[] = {
  chrome::kExtensionScheme,
  NULL
};
}  // namespace

namespace chrome {

#if defined(OS_CHROMEOS)
const char kCrosScheme[] = "cros";
#endif

const char kAboutPluginsURL[] = "about:plugins";
const char kAboutVersionURL[] = "about:version";

// Add Chrome UI URLs as necessary, in alphabetical order.
// Be sure to add the corresponding kChromeUI*Host constant below.
// This is the About Chrome page.
const char kChromeUIAboutPageFrameURL[] = "chrome://about-page-frame/";
// This is a WebUI page that lists other WebUI pages.
const char kChromeUIAboutURL[] = "chrome://about/";
const char kChromeUIBookmarksURL[] = "chrome://bookmarks/";
const char kChromeUICertificateViewerURL[] = "chrome://view-cert/";
const char kChromeUIChromeURLsURL[] = "chrome://chrome-urls/";
const char kChromeUICloudPrintResourcesURL[] = "chrome://cloudprintresources/";
const char kChromeUIConflictsURL[] = "chrome://conflicts/";
const char kChromeUIConstrainedHTMLTestURL[] = "chrome://constrained-test/";
const char kChromeUICrashesURL[] = "chrome://crashes/";
const char kChromeUICrashURL[] = "chrome://crash/";
const char kChromeUICreditsURL[] = "chrome://credits/";
const char kChromeUIDevToolsURL[] = "chrome-devtools://devtools/";
const char kChromeUIDownloadsURL[] = "chrome://downloads/";
const char kChromeUIEditSearchEngineDialogURL[] = "chrome://editsearchengine/";
const char kChromeUIExtensionIconURL[] = "chrome://extension-icon/";
const char kChromeUIExtensionsFrameURL[] = "chrome://extensions-frame/";
const char kChromeUIExtensionsURL[] = "chrome://extensions/";
const char kChromeUIFaviconURL[] = "chrome://favicon/";
const char kChromeUIFeedbackURL[] = "chrome://feedback/";
const char kChromeUIFlagsURL[] = "chrome://flags/";
const char kChromeUIFlashURL[] = "chrome://flash/";
const char kChromeUIGpuCleanURL[] = "chrome://gpuclean";
const char kChromeUIGpuCrashURL[] = "chrome://gpucrash";
const char kChromeUIGpuHangURL[] = "chrome://gpuhang";
const char kChromeUIHangURL[] = "chrome://hang/";
const char kChromeUIHistoryURL[] = "chrome://history/";
const char kChromeUIHungRendererDialogURL[] = "chrome://hung-renderer/";
const char kChromeUIInputWindowDialogURL[] = "chrome://input-window-dialog/";
const char kChromeUIIPCURL[] = "chrome://ipc/";
const char kChromeUIKeyboardURL[] = "chrome://keyboard/";
const char kChromeUIKillURL[] = "chrome://kill/";
const char kChromeUIMemoryRedirectURL[] = "chrome://memory-redirect/";
const char kChromeUIMemoryURL[] = "chrome://memory/";
const char kChromeUINetInternalsURL[] = "chrome://net-internals/";
const char kChromeUINetworkViewCacheURL[] = "chrome://view-http-cache/";
const char kChromeUINewProfile[] = "chrome://newprofile/";
const char kChromeUINewTabURL[] = "chrome://newtab/";
const char kChromeUIOmniboxURL[] = "chrome://omnibox/";
const char kChromeUIPluginsURL[] = "chrome://plugins/";
const char kChromeUIPolicyURL[] = "chrome://policy/";
const char kChromeUIPrintURL[] = "chrome://print/";
const char kChromeUISessionsURL[] = "chrome://sessions/";
const char kChromeUISettingsURL[] = "chrome://settings/";
// settings-frame is the URL used to directly access the new settings page in
// the UberPage, AKA options2.
const char kChromeUISettingsFrameURL[] = "chrome://settings-frame/";
const char kChromeUIShorthangURL[] = "chrome://shorthang/";
const char kChromeUISSLClientCertificateSelectorURL[] = "chrome://select-cert/";
const char kChromeUISyncPromoURL[] = "chrome://syncpromo/";
const char kChromeUITaskManagerURL[] = "chrome://tasks/";
const char kChromeUITermsURL[] = "chrome://terms/";
const char kChromeUIThumbnailURL[] = "chrome://thumb/";
const char kChromeUIUberURL[] = "chrome://chrome/";
const char kChromeUIUberFrameURL[] = "chrome://uber-frame/";
const char kChromeUIVersionURL[] = "chrome://version/";
const char kChromeUIWorkersURL[] = "chrome://workers/";

#if defined(OS_CHROMEOS)
const char kChromeUIActivationMessage[] = "chrome://activationmessage/";
const char kChromeUIActiveDownloadsURL[] = "chrome://active-downloads/";
const char kChromeUIChooseMobileNetworkURL[] =
    "chrome://choose-mobile-network/";
const char kChromeUIDiscardsURL[] = "chrome://discards/";
const char kChromeUIImageBurnerURL[] = "chrome://imageburner/";
const char kChromeUIKeyboardOverlayURL[] = "chrome://keyboardoverlay/";
const char kChromeUILockScreenURL[] = "chrome://lock/";
const char kChromeUIMediaplayerURL[] = "chrome://mediaplayer/";
const char kChromeUIMobileSetupURL[] = "chrome://mobilesetup/";
const char kChromeUIOobeURL[] = "chrome://oobe/";
const char kChromeUIOSCreditsURL[] = "chrome://os-credits/";
const char kChromeUIProxySettingsURL[] = "chrome://proxy-settings/";
const char kChromeUIRegisterPageURL[] = "chrome://register/";
const char kChromeUISimUnlockURL[] = "chrome://sim-unlock/";
const char kChromeUISlideshowURL[] = "chrome://slideshow/";
const char kChromeUISystemInfoURL[] = "chrome://system/";
const char kChromeUITermsOemURL[] = "chrome://terms/oem";
const char kChromeUIUserImageURL[] = "chrome://userimage/";
#endif

#if defined(FILE_MANAGER_EXTENSION)
const char kChromeUIFileManagerURL[] = "chrome://files/";
#endif

#if (defined(OS_LINUX) && defined(TOOLKIT_VIEWS)) || defined(USE_AURA)
const char kChromeUICollectedCookiesURL[] = "chrome://collected-cookies/";
const char kChromeUIHttpAuthURL[] = "chrome://http-auth/";
const char kChromeUITabModalConfirmDialogURL[] =
    "chrome://tab-modal-confirm-dialog/";
#endif

// Add Chrome UI hosts here, in alphabetical order.
// Add hosts to kChromePaths in browser_about_handler.cc to be listed by
// chrome://chrome-urls (about:about) and the built-in AutocompleteProvider.
const char kChromeUIAboutHost[] = "about";
const char kChromeUIAboutPageFrameHost[] = "about-page-frame";
const char kChromeUIAppCacheInternalsHost[] = "appcache-internals";
const char kChromeUIBlankHost[] = "blank";
const char kChromeUIBlobInternalsHost[] = "blob-internals";
const char kChromeUIBookmarksHost[] = "bookmarks";
const char kChromeUIBrowserCrashHost[] = "inducebrowsercrashforrealz";
const char kChromeUICacheHost[] = "cache";
const char kChromeUICertificateViewerHost[] = "view-cert";
const char kChromeUIChromeURLsHost[] = "chrome-urls";
const char kChromeUICloudPrintResourcesHost[] = "cloudprintresources";
const char kChromeUICloudPrintSetupHost[] = "cloudprintsetup";
const char kChromeUIConflictsHost[] = "conflicts";
const char kChromeUIConstrainedHTMLTestHost[] = "constrained-test";
const char kChromeUICrashesHost[] = "crashes";
const char kChromeUICrashHost[] = "crash";
const char kChromeUICreditsHost[] = "credits";
const char kChromeUIDefaultHost[] = "version";
const char kChromeUIDevToolsHost[] = "devtools";
const char kChromeUIDialogHost[] = "dialog";
const char kChromeUIDNSHost[] = "dns";
const char kChromeUIDownloadsHost[] = "downloads";
const char kChromeUIEditSearchEngineDialogHost[] = "editsearchengine";
const char kChromeUIExtensionIconHost[] = "extension-icon";
const char kChromeUIExtensionsFrameHost[] = "extensions-frame";
const char kChromeUIExtensionsHost[] = "extensions";
const char kChromeUIFaviconHost[] = "favicon";
const char kChromeUIFeedbackHost[] = "feedback";
const char kChromeUIFlagsHost[] = "flags";
const char kChromeUIFlashHost[] = "flash";
const char kChromeUIGpuCleanHost[] = "gpuclean";
const char kChromeUIGpuCrashHost[] = "gpucrash";
const char kChromeUIGpuHangHost[] = "gpuhang";
const char kChromeUIGpuHost[] = "gpu";
const char kChromeUIGpuInternalsHost[] = "gpu-internals";
const char kChromeUIHangHost[] = "hang";
const char kChromeUIHistogramsHost[] = "histograms";
const char kChromeUIHistoryHost[] = "history";
const char kChromeUIHungRendererDialogHost[] = "hung-renderer";
const char kChromeUIInputWindowDialogHost[] = "input-window-dialog";
const char kChromeUIIPCHost[] = "ipc";
const char kChromeUIKeyboardHost[] = "keyboard";
const char kChromeUIKillHost[] = "kill";
const char kChromeUIMediaInternalsHost[] = "media-internals";
const char kChromeUIMemoryHost[] = "memory";
const char kChromeUIMemoryRedirectHost[] = "memory-redirect";
const char kChromeUINetInternalsHost[] = "net-internals";
const char kChromeUINetworkActionPredictorHost[] = "network-action-predictor";
const char kChromeUINetworkViewCacheHost[] = "view-http-cache";
const char kChromeUINewTabHost[] = "newtab";
const char kChromeUIOmniboxHost[] = "omnibox";
const char kChromeUIPluginsHost[] = "plugins";
const char kChromeUIPolicyHost[] = "policy";
const char kChromeUIPrintHost[] = "print";
const char kChromeUIProfilerHost[] = "profiler";
const char kChromeUIQuotaInternalsHost[] = "quota-internals";
const char kChromeUIResourcesHost[] = "resources";
const char kChromeUISessionsHost[] = "sessions";
const char kChromeUISettingsHost[] = "settings";
const char kChromeUISettingsFrameHost[] = "settings-frame";
const char kChromeUIShorthangHost[] = "shorthang";
const char kChromeUISSLClientCertificateSelectorHost[] = "select-cert";
const char kChromeUIStatsHost[] = "stats";
const char kChromeUISyncHost[] = "sync";
const char kChromeUISyncInternalsHost[] = "sync-internals";
const char kChromeUISyncPromoHost[] = "syncpromo";
const char kChromeUISyncResourcesHost[] = "syncresources";
const char kChromeUITaskManagerHost[] = "tasks";
const char kChromeUITCMallocHost[] = "tcmalloc";
const char kChromeUITermsHost[] = "terms";
const char kChromeUIThumbnailHost[] = "thumb";
const char kChromeUITouchIconHost[] = "touch-icon";
const char kChromeUITracingHost[] = "tracing";
const char kChromeUIUberFrameHost[] = "uber-frame";
const char kChromeUIUberHost[] = "chrome";
const char kChromeUIVersionHost[] = "version";
const char kChromeUIWorkersHost[] = "workers";

const char kChromeUIScreenshotPath[] = "screenshots";
const char kChromeUIThemePath[] = "theme";

#if defined(OS_LINUX) || defined(OS_OPENBSD)
const char kChromeUILinuxProxyConfigHost[] = "linux-proxy-config";
const char kChromeUISandboxHost[] = "sandbox";
#endif

#if defined(OS_CHROMEOS)
const char kChromeUIActivationMessageHost[] = "activationmessage";
const char kChromeUIActiveDownloadsHost[] = "active-downloads";
const char kChromeUIChooseMobileNetworkHost[] = "choose-mobile-network";
const char kChromeUICryptohomeHost[] = "cryptohome";
const char kChromeUIDiscardsHost[] = "discards";
const char kChromeUIImageBurnerHost[] = "imageburner";
const char kChromeUIKeyboardOverlayHost[] = "keyboardoverlay";
const char kChromeUILockScreenHost[] = "lock";
const char kChromeUILoginContainerHost[] = "login-container";
const char kChromeUILoginHost[] = "login";
const char kChromeUIMediaplayerHost[] = "mediaplayer";
const char kChromeUIMobileSetupHost[] = "mobilesetup";
const char kChromeUINetworkHost[] = "network";
const char kChromeUIOobeHost[] = "oobe";
const char kChromeUIOSCreditsHost[] = "os-credits";
const char kChromeUIProxySettingsHost[] = "proxy-settings";
const char kChromeUIRegisterPageHost[] = "register";
const char kChromeUIRotateHost[] = "rotate";
const char kChromeUISimUnlockHost[] = "sim-unlock";
const char kChromeUISlideshowHost[] = "slideshow";
const char kChromeUISystemInfoHost[] = "system";
const char kChromeUIUserImageHost[] = "userimage";

const char kChromeUIMenu[] = "menu";
const char kChromeUINetworkMenu[] = "network-menu";
const char kChromeUIWrenchMenu[] = "wrench-menu";

const char kEULAPathFormat[] = "/usr/share/chromeos-assets/eula/%s/eula.html";
const char kOemEulaURLPath[] = "oem";
#endif

#if defined(FILE_MANAGER_EXTENSION)
const char kChromeUIFileManagerHost[] = "files";
#endif

#if (defined(OS_LINUX) && defined(TOOLKIT_VIEWS)) || defined(USE_AURA)
const char kChromeUICollectedCookiesHost[] = "collected-cookies";
const char kChromeUIHttpAuthHost[] = "http-auth";
const char kChromeUITabModalConfirmDialogHost[] = "tab-modal-confirm-dialog";
#endif

// Option sub pages.
// Add sub page paths to kChromeSettingsSubPages in builtin_provider.cc to be
// listed by the built-in AutocompleteProvider.
const char kAdvancedOptionsSubPage[] = "advanced";
const char kAutofillSubPage[] = "autofill";
const char kBrowserOptionsSubPage[] = "browser";
const char kClearBrowserDataSubPage[] = "clearBrowserData";
const char kContentSettingsExceptionsSubPage[] = "contentExceptions";
const char kContentSettingsSubPage[] = "content";
const char kExtensionsSubPage[] = "extensions";
const char kHandlerSettingsSubPage[] = "handlers";
const char kImportDataSubPage[] = "importData";
const char kInstantConfirmPage[] = "instantConfirm";
const char kLanguageOptionsSubPage[] = "languages";
const char kManageProfileSubPage[] = "manageProfile";
const char kPasswordManagerSubPage[] = "passwords";
const char kPersonalOptionsSubPage[] = "personal";
const char kSearchEnginesSubPage[] = "searchEngines";
const char kSyncSetupSubPage[] = "syncSetup";
#if defined(OS_CHROMEOS)
const char kAboutOptionsSubPage[] = "about";
const char kInternetOptionsSubPage[] = "internet";
const char kSystemOptionsSubPage[] = "system";
#endif

const char kSyncGoogleDashboardURL[] = "https://www.google.com/dashboard/";

const char kPasswordManagerLearnMoreURL[] =
#if defined(OS_CHROMEOS)
    "https://support.google.com/chromeos/?p=settings_password";
#else
    "https://support.google.com/chrome/?p=settings_password";
#endif

const char kChromeHelpURL[] =
#if defined(OS_CHROMEOS)
#if defined(OFFICIAL_BUILD)
    "chrome-extension://honijodknafkokifofgiaalefdiedpko/main.html";
#else
    "https://support.google.com/chromeos/?p=wrench";
#endif  // defined(OFFICIAL_BUILD
#else
    "https://support.google.com/chrome/?p=wrench";
#endif

const char kSettingsSearchHelpURL[] =
#if defined(OS_CHROMEOS)
    "https://support.google.com/chromeos/?p=settings_search_help";
#else
    "https://support.google.com/chrome/?p=settings_search_help";
#endif

const char kAboutGoogleTranslateURL[] =
#if defined(OS_CHROMEOS)
    "https://support.google.com/chromeos/?p=ib_translation_bar";
#else
    "https://support.google.com/chrome/?p=ib_translation_bar";
#endif

const char kAutofillHelpURL[] =
#if defined(OS_CHROMEOS)
    "https://support.google.com/chromeos/?p=settings_autofill";
#else
    "https://support.google.com/chrome/?p=settings_autofill";
#endif

const char kInstantLearnMoreURL[] =
#if defined(OS_CHROMEOS)
    "https://support.google.com/chromeos/?p=settings_instant_policy";
#else
    "https://support.google.com/chrome/?p=settings_instant_policy";
#endif

const char kPageInfoHelpCenterURL[] =
#if defined(OS_CHROMEOS)
    "https://support.google.com/chromeos/?p=ui_security_indicator";
#else
    "https://support.google.com/chrome/?p=ui_security_indicator";
#endif

const char kCrashReasonURL[] =
#if defined(OS_CHROMEOS)
    "https://support.google.com/chromeos/?p=e_awsnap";
#else
    "https://support.google.com/chrome/?p=e_awsnap";
#endif

const char kKillReasonURL[] =
#if defined(OS_CHROMEOS)
    "https://support.google.com/chromeos/?p=e_deadjim";
#else
    "https://support.google.com/chrome/?p=e_deadjim";
#endif

const char kPrivacyLearnMoreURL[] =
#if defined(OS_CHROMEOS)
    "https://support.google.com/chromeos/?p=settings_privacy";
#else
    "https://support.google.com/chrome/?p=settings_privacy";
#endif

const char kChromiumProjectURL[] = "http://code.google.com/chromium/";

const char kLearnMoreReportingURL[] =
    "https://support.google.com/chrome/?p=ui_usagestat";

const char kOutdatedPluginLearnMoreURL[] =
    "https://support.google.com/chrome/?p=ib_outdated_plugin";

const char kBlockedPluginLearnMoreURL[] =
    "https://support.google.com/chrome/?p=ib_blocked_plugin";

const char kSpeechInputAboutURL[] =
    "https://support.google.com/chrome/?p=ui_speech_input";

const char kLearnMoreRegisterProtocolHandlerURL[] =
    "https://support.google.com/chrome/?p=ib_protocol_handler";

const char kSyncLearnMoreURL[] =
    "https://support.google.com/chrome/?p=settings_sign_in";

const char kDownloadScanningLearnMoreURL[] =
    "https://support.google.com/chrome/?p=ib_download_scan";

const char kSyncEverythingLearnMoreURL[] =
    "https://support.google.com/chrome/?p=settings_sync_all";

const char kCloudPrintLearnMoreURL[] =
#if defined(OS_CHROMEOS)
    "https://support.google.com/chromeos/?p=settings_cloud_print";
#else
    "https://support.google.com/chrome/?p=settings_cloud_print";
#endif

const char kInvalidPasswordHelpURL[] =
    "https://support.google.com/accounts/bin/answer.py?ctx=ch&answer=27444";

const char kCanNotAccessAccountURL[] =
    "https://support.google.com/accounts/bin/answer.py?answer=48598";

const char kSyncEncryptionHelpURL[] =
#if defined(OS_CHROMEOS)
    "https://support.google.com/chromeos/?p=settings_encryption";
#else
    "https://support.google.com/chrome/?p=settings_encryption";
#endif

const char kSyncErrorsHelpURL[] =
    "https://support.google.com/chrome/?p=settings_sync_error";

const char kSyncCreateNewAccountURL[] =
    "https://www.google.com/accounts/NewAccount?service=chromiumsync";

const char* const kChromeDebugURLs[] = {
  kChromeUICrashURL,
  kChromeUIKillURL,
  kChromeUIHangURL,
  kChromeUIShorthangURL,
  kChromeUIGpuCleanURL,
  kChromeUIGpuCrashURL,
  kChromeUIGpuHangURL,
};
int kNumberOfChromeDebugURLs = static_cast<int>(arraysize(kChromeDebugURLs));

const char kExtensionScheme[] = "chrome-extension";

void RegisterChromeSchemes() {
  url_util::AddStandardScheme(kExtensionScheme);
#if defined(OS_CHROMEOS)
  url_util::AddStandardScheme(kCrosScheme);
#endif

  // This call will also lock the list of standard schemes.
  RegisterContentSchemes(kSavableSchemes);
}

}  // namespace chrome
