// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Constants for the names of various preferences, for easier changing.

#ifndef CHROME_COMMON_PREF_NAMES_H_
#define CHROME_COMMON_PREF_NAMES_H_
#pragma once

#include <stddef.h>

#include "build/build_config.h"

namespace prefs {

// Profile prefs. Please add Local State prefs below instead.
extern const char kAppsPromoCounter[];
extern const char kDefaultApps[];
extern const char kDefaultAppsInstalled[];
extern const char kHomePageIsNewTabPage[];
extern const char kHomePage[];
extern const char kHomePageChanged[];
extern const char kSessionExitedCleanly[];
extern const char kRestoreOnStartup[];
extern const char kURLsToRestoreOnStartup[];

// For OS_CHROMEOS we maintain kApplicationLocale property in both local state
// and user's profile.  Global property determines locale of login screen,
// while user's profile determines his personal locale preference.
extern const char kApplicationLocale[];
#if defined(OS_CHROMEOS)
extern const char kApplicationLocaleBackup[];
extern const char kApplicationLocaleAccepted[];
extern const char kOwnerLocale[];
#endif

extern const char kGlobalDefaultCharset[];
extern const char kDefaultCharset[];
extern const char kAcceptLanguages[];
extern const char kStaticEncodings[];
extern const char kPopupWhitelistedHosts[];
extern const char kShowBookmarkBar[];
extern const char kBookmarkEditorExpandedNodes[];
extern const char kWebKitGlobalStandardFontFamily[];
extern const char kWebKitStandardFontFamily[];
extern const char kWebKitGlobalFixedFontFamily[];
extern const char kWebKitFixedFontFamily[];
extern const char kWebKitGlobalSerifFontFamily[];
extern const char kWebKitSerifFontFamily[];
extern const char kWebKitGlobalSansSerifFontFamily[];
extern const char kWebKitSansSerifFontFamily[];
extern const char kWebKitGlobalCursiveFontFamily[];
extern const char kWebKitCursiveFontFamily[];
extern const char kWebKitGlobalFantasyFontFamily[];
extern const char kWebKitFantasyFontFamily[];
extern const char kWebKitStandardFontFamilyMap[];
extern const char kWebKitFixedFontFamilyMap[];
extern const char kWebKitSerifFontFamilyMap[];
extern const char kWebKitSansSerifFontFamilyMap[];
extern const char kWebKitCursiveFontFamilyMap[];
extern const char kWebKitFantasyFontFamilyMap[];

// ISO 15924 four-letter script codes that per-script font prefs are supported
// for.
extern const char* const kWebKitScriptsForFontFamilyMaps[];
extern const size_t kWebKitScriptsForFontFamilyMapsLength;

// Per-script font prefs that have defaults, for easy reference when registering
// the defaults.
extern const char kWebKitStandardFontFamilyArabic[];
extern const char kWebKitFixedFontFamilyArabic[];
extern const char kWebKitSerifFontFamilyArabic[];
extern const char kWebKitSansSerifFontFamilyArabic[];
extern const char kWebKitStandardFontFamilyJapanese[];
extern const char kWebKitFixedFontFamilyJapanese[];
extern const char kWebKitSerifFontFamilyJapanese[];
extern const char kWebKitSansSerifFontFamilyJapanese[];
extern const char kWebKitStandardFontFamilyKorean[];
extern const char kWebKitFixedFontFamilyKorean[];
extern const char kWebKitSerifFontFamilyKorean[];
extern const char kWebKitSansSerifFontFamilyKorean[];
extern const char kWebKitCursiveFontFamilyKorean[];
extern const char kWebKitStandardFontFamilySimplifiedHan[];
extern const char kWebKitFixedFontFamilySimplifiedHan[];
extern const char kWebKitSerifFontFamilySimplifiedHan[];
extern const char kWebKitSansSerifFontFamilySimplifiedHan[];
extern const char kWebKitStandardFontFamilyTraditionalHan[];
extern const char kWebKitFixedFontFamilyTraditionalHan[];
extern const char kWebKitSerifFontFamilyTraditionalHan[];
extern const char kWebKitSansSerifFontFamilyTraditionalHan[];

extern const char kWebKitGlobalDefaultFontSize[];
extern const char kWebKitDefaultFontSize[];
extern const char kWebKitGlobalDefaultFixedFontSize[];
extern const char kWebKitDefaultFixedFontSize[];
extern const char kWebKitGlobalMinimumFontSize[];
extern const char kWebKitMinimumFontSize[];
extern const char kWebKitGlobalMinimumLogicalFontSize[];
extern const char kWebKitMinimumLogicalFontSize[];
extern const char kWebKitGlobalJavascriptEnabled[];
extern const char kWebKitJavascriptEnabled[];
extern const char kWebKitWebSecurityEnabled[];
extern const char kWebKitGlobalJavascriptCanOpenWindowsAutomatically[];
extern const char kWebKitJavascriptCanOpenWindowsAutomatically[];
extern const char kWebKitGlobalLoadsImagesAutomatically[];
extern const char kWebKitLoadsImagesAutomatically[];
extern const char kWebKitImagesEnabled[];
extern const char kWebKitGlobalPluginsEnabled[];
extern const char kWebKitPluginsEnabled[];
extern const char kWebKitDomPasteEnabled[];
extern const char kWebKitShrinksStandaloneImagesToFit[];
extern const char kWebKitInspectorSettings[];
extern const char kWebKitUsesUniversalDetector[];
extern const char kWebKitTextAreasAreResizable[];
extern const char kWebKitJavaEnabled[];
extern const char kWebkitTabsToLinks[];
extern const char kWebKitAllowDisplayingInsecureContent[];
extern const char kWebKitAllowRunningInsecureContent[];
extern const char kPasswordManagerEnabled[];
extern const char kPasswordManagerAllowShowPasswords[];
extern const char kAutologinEnabled[];
extern const char kReverseAutologinEnabled[];
extern const char kSafeBrowsingEnabled[];
extern const char kSafeBrowsingReportingEnabled[];
extern const char kIncognitoModeAvailability[];
extern const char kSearchSuggestEnabled[];
extern const char kConfirmToQuitEnabled[];
extern const char kCookieBehavior[];  // OBSOLETE
extern const char kSyncedDefaultSearchProviderGUID[];
extern const char kDefaultSearchProviderEnabled[];
extern const char kDefaultSearchProviderSearchURL[];
extern const char kDefaultSearchProviderSuggestURL[];
extern const char kDefaultSearchProviderInstantURL[];
extern const char kDefaultSearchProviderIconURL[];
extern const char kDefaultSearchProviderEncodings[];
extern const char kDefaultSearchProviderName[];
extern const char kDefaultSearchProviderKeyword[];
extern const char kDefaultSearchProviderID[];
extern const char kDefaultSearchProviderPrepopulateID[];
extern const char kSearchProviderOverrides[];
extern const char kSearchProviderOverridesVersion[];
extern const char kPromptForDownload[];
extern const char kAlternateErrorPagesEnabled[];
extern const char kDnsStartupPrefetchList[];  // OBSOLETE
extern const char kDnsPrefetchingStartupList[];
extern const char kDnsHostReferralList[];  // OBSOLETE
extern const char kDnsPrefetchingHostReferralList[];
extern const char kDisableSpdy[];
extern const char kHttpServerProperties[];
extern const char kSpdyServers[];
extern const char kAlternateProtocolServers[];
extern const char kDisabledSchemes[];
extern const char kUrlBlacklist[];
extern const char kUrlWhitelist[];
extern const char kInstantConfirmDialogShown[];
extern const char kInstantEnabled[];
extern const char kInstantEnabledOnce[];
extern const char kInstantEnabledTime[];
extern const char kInstantPromo[];
extern const char kMultipleProfilePrefMigration[];
extern const char kNetworkPredictionEnabled[];
extern const char kDefaultAppsInstallState[];
#if defined(OS_CHROMEOS)
extern const char kAudioMute[];
extern const char kAudioVolume[];
extern const char kTapToClickEnabled[];
extern const char kPrimaryMouseButtonRight[];
extern const char kTouchpadSensitivity[];
extern const char kUse24HourClock[];
extern const char kLanguageCurrentInputMethod[];
extern const char kLanguagePreviousInputMethod[];
extern const char kLanguageHotkeyNextEngineInMenu[];
extern const char kLanguageHotkeyPreviousEngine[];
extern const char kLanguagePreferredLanguages[];
extern const char kLanguagePreloadEngines[];
extern const char kLanguageChewingAutoShiftCur[];
extern const char kLanguageChewingAddPhraseDirection[];
extern const char kLanguageChewingEasySymbolInput[];
extern const char kLanguageChewingEscCleanAllBuf[];
extern const char kLanguageChewingForceLowercaseEnglish[];
extern const char kLanguageChewingPlainZhuyin[];
extern const char kLanguageChewingPhraseChoiceRearward[];
extern const char kLanguageChewingSpaceAsSelection[];
extern const char kLanguageChewingMaxChiSymbolLen[];
extern const char kLanguageChewingCandPerPage[];
extern const char kLanguageChewingKeyboardType[];
extern const char kLanguageChewingSelKeys[];
extern const char kLanguageChewingHsuSelKeyType[];
extern const char kLanguageHangulKeyboard[];
extern const char kLanguageHangulHanjaBindingKeys[];
extern const char kLanguagePinyinCorrectPinyin[];
extern const char kLanguagePinyinFuzzyPinyin[];
extern const char kLanguagePinyinLookupTablePageSize[];
extern const char kLanguagePinyinShiftSelectCandidate[];
extern const char kLanguagePinyinMinusEqualPage[];
extern const char kLanguagePinyinCommaPeriodPage[];
extern const char kLanguagePinyinAutoCommit[];
extern const char kLanguagePinyinDoublePinyin[];
extern const char kLanguagePinyinDoublePinyinSchema[];
extern const char kLanguagePinyinInitChinese[];
extern const char kLanguagePinyinInitFull[];
extern const char kLanguagePinyinInitFullPunct[];
extern const char kLanguagePinyinInitSimplifiedChinese[];
extern const char kLanguagePinyinTradCandidate[];
extern const char kLanguageMozcPreeditMethod[];
extern const char kLanguageMozcSessionKeymap[];
extern const char kLanguageMozcPunctuationMethod[];
extern const char kLanguageMozcSymbolMethod[];
extern const char kLanguageMozcSpaceCharacterForm[];
extern const char kLanguageMozcHistoryLearningLevel[];
extern const char kLanguageMozcSelectionShortcut[];
extern const char kLanguageMozcShiftKeyModeSwitch[];
extern const char kLanguageMozcNumpadCharacterForm[];
extern const char kLanguageMozcIncognitoMode[];
extern const char kLanguageMozcUseAutoImeTurnOff[];
extern const char kLanguageMozcUseHistorySuggest[];
extern const char kLanguageMozcUseDictionarySuggest[];
extern const char kLanguageMozcSuggestionsSize[];
extern const char kLanguageXkbRemapSearchKeyTo[];
extern const char kLanguageXkbRemapControlKeyTo[];
extern const char kLanguageXkbRemapAltKeyTo[];
extern const char kLanguageXkbAutoRepeatEnabled[];
extern const char kLanguageXkbAutoRepeatDelay[];
extern const char kLanguageXkbAutoRepeatInterval[];
extern const char kLanguagePreferredVirtualKeyboard[];
extern const char kSpokenFeedbackEnabled[];
extern const char kHighContrastEnabled[];
extern const char kScreenMagnifierEnabled[];
extern const char kVirtualKeyboardEnabled[];
extern const char kLabsAdvancedFilesystemEnabled[];
extern const char kLabsMediaplayerEnabled[];
extern const char kEnableScreenLock[];
extern const char kShowPlanNotifications[];
extern const char kShow3gPromoNotification[];
extern const char kUseSharedProxies[];
extern const char kOAuth1Token[];
extern const char kOAuth1Secret[];
#endif  // defined(OS_CHROMEOS)
extern const char kIpcDisabledMessages[];
extern const char kShowHomeButton[];
extern const char kRecentlySelectedEncoding[];
extern const char kDeleteBrowsingHistory[];
extern const char kDeleteDownloadHistory[];
extern const char kDeleteCache[];
extern const char kDeleteCookies[];
extern const char kDeletePasswords[];
extern const char kDeleteFormData[];
extern const char kEnableSpellCheck[];
extern const char kSpeechInputFilterProfanities[];
extern const char kSpeechInputTrayNotificationShown[];
extern const char kEnabledLabsExperiments[];
extern const char kEnableAutoSpellCorrect[];
extern const char kSavingBrowserHistoryDisabled[];
extern const char kDeleteTimePeriod[];
#if defined(TOOLKIT_USES_GTK)
extern const char kUsesSystemTheme[];
#endif
extern const char kCurrentThemePackFilename[];
extern const char kCurrentThemeID[];
extern const char kCurrentThemeImages[];
extern const char kCurrentThemeColors[];
extern const char kCurrentThemeTints[];
extern const char kCurrentThemeDisplayProperties[];
extern const char kExtensionsUIDeveloperMode[];
extern const char kExtensionToolbarSize[];
extern const char kPluginsLastInternalDirectory[];
extern const char kPluginsPluginsList[];
extern const char kPluginsDisabledPlugins[];
extern const char kPluginsDisabledPluginsExceptions[];
extern const char kPluginsEnabledPlugins[];
extern const char kPluginsEnabledInternalPDF[];
extern const char kPluginsEnabledNaCl[];
extern const char kPluginsShowSetReaderDefaultInfobar[];
extern const char kPluginsShowDetails[];
extern const char kPluginsAllowOutdated[];
extern const char kPluginsAlwaysAuthorize[];
extern const char kCheckDefaultBrowser[];
extern const char kDefaultBrowserSettingEnabled[];
#if defined(OS_MACOSX)
extern const char kShowUpdatePromotionInfoBar[];
#endif
extern const char kUseCustomChromeFrame[];
extern const char kShowOmniboxSearchHint[];
extern const char kDesktopNotificationDefaultContentSetting[];  // OBSOLETE
extern const char kDesktopNotificationAllowedOrigins[];  // OBSOLETE
extern const char kDesktopNotificationDeniedOrigins[];  // OBSOLETE
extern const char kDesktopNotificationPosition[];
extern const char kDefaultContentSettings[];
extern const char kPerHostContentSettings[];  // OBSOLETE
extern const char kContentSettingsVersion[];
extern const char kContentSettingsPatterns[];  // OBSOLETE
extern const char kContentSettingsPatternPairs[];
extern const char kContentSettingsDefaultWhitelistVersion[];
extern const char kContentSettingsPluginWhitelist[];
extern const char kBlockThirdPartyCookies[];
extern const char kClearSiteDataOnExit[];
extern const char kDefaultZoomLevel[];
extern const char kPerHostZoomLevels[];
extern const char kProfileShortcutCreated[];
extern const char kAutofillEnabled[];
extern const char kAutofillAuxiliaryProfilesEnabled[];
extern const char kAutofillPositiveUploadRate[];
extern const char kAutofillNegativeUploadRate[];
extern const char kAutofillPersonalDataManagerFirstRun[];
extern const char kEditBookmarksEnabled[];

extern const char kEnableTranslate[];
extern const char kPinnedTabs[];
extern const char kHttpThrottlingEnabled[];
extern const char kHttpThrottlingMayExperiment[];

extern const char kDisable3DAPIs[];
extern const char kEnableHyperlinkAuditing[];
extern const char kEnableReferrers[];

#if defined(OS_MACOSX)
extern const char kPresentationModeEnabled[];
#endif

extern const char kImportBookmarks[];
extern const char kImportHistory[];
extern const char kImportHomepage[];
extern const char kImportSearchEngine[];
extern const char kImportSavedPasswords[];

extern const char kEnterpriseWebStoreURL[];
extern const char kEnterpriseWebStoreName[];

#if !defined(OS_MACOSX) && !defined(OS_CHROMEOS) && defined(OS_POSIX)
extern const char kLocalProfileId[];
extern const char kPasswordsUseLocalProfileId[];
#endif

extern const char kProfileAvatarIndex[];
extern const char kProfileName[];

// Local state prefs. Please add Profile prefs above instead.
extern const char kCertRevocationCheckingEnabled[];
extern const char kSSL3Enabled[];
extern const char kTLS1Enabled[];
extern const char kCipherSuiteBlacklist[];
extern const char kEnableOriginBoundCerts[];
extern const char kDisableSSLRecordSplitting[];
extern const char kEnableMemoryInfo[];

extern const char kMetricsClientID[];
extern const char kMetricsSessionID[];
extern const char kMetricsClientIDTimestamp[];
extern const char kMetricsReportingEnabled[];
extern const char kMetricsInitialLogs[];
extern const char kMetricsOngoingLogs[];

extern const char kProfileLastUsed[];
extern const char kProfilesLastActive[];
extern const char kProfilesNumCreated[];
extern const char kProfileInfoCache[];

extern const char kProfileMetrics[];
extern const char kProfilePrefix[];

extern const char kStabilityExitedCleanly[];
extern const char kStabilityStatsVersion[];
extern const char kStabilityStatsBuildTime[];
extern const char kStabilitySessionEndCompleted[];
extern const char kStabilityLaunchCount[];
extern const char kStabilityCrashCount[];
extern const char kStabilityIncompleteSessionEndCount[];
extern const char kStabilityPageLoadCount[];
extern const char kStabilityRendererCrashCount[];
extern const char kStabilityExtensionRendererCrashCount[];
extern const char kStabilityLaunchTimeSec[];
extern const char kStabilityLastTimestampSec[];
extern const char kStabilityRendererHangCount[];
extern const char kStabilityChildProcessCrashCount[];
extern const char kStabilityOtherUserCrashCount[];
extern const char kStabilityKernelCrashCount[];
extern const char kStabilitySystemUncleanShutdownCount[];

extern const char kStabilityBreakpadRegistrationSuccess[];
extern const char kStabilityBreakpadRegistrationFail[];
extern const char kStabilityDebuggerPresent[];
extern const char kStabilityDebuggerNotPresent[];

extern const char kStabilityPluginStats[];
extern const char kStabilityPluginName[];
extern const char kStabilityPluginLaunches[];
extern const char kStabilityPluginInstances[];
extern const char kStabilityPluginCrashes[];

extern const char kUninstallMetricsPageLoadCount[];
extern const char kUninstallLaunchCount[];

extern const char kUninstallMetricsInstallDate[];
extern const char kUninstallMetricsUptimeSec[];
extern const char kUninstallLastLaunchTimeSec[];
extern const char kUninstallLastObservedRunTimeSec[];

extern const char kBrowserWindowPlacement[];
extern const char kTaskManagerWindowPlacement[];
extern const char kKeywordEditorWindowPlacement[];
extern const char kPreferencesWindowPlacement[];
extern const char kMemoryCacheSize[];

extern const char kDownloadDefaultDirectory[];
extern const char kDownloadExtensionsToOpen[];
extern const char kDownloadDirUpgraded[];

extern const char kSaveFileDefaultDirectory[];
extern const char kSaveFileType[];

extern const char kAllowFileSelectionDialogs[];
extern const char kLastUsedFileBrowserHandlers[];

extern const char kSelectFileLastDirectory[];

extern const char kHungPluginDetectFrequency[];
extern const char kPluginMessageResponseTimeout[];

extern const char kSpellCheckDictionary[];
extern const char kSpellCheckUseSpellingService[];

extern const char kExcludedSchemes[];

extern const char kSafeBrowsingClientKey[];
extern const char kSafeBrowsingWrappedKey[];

extern const char kOptionsWindowLastTabIndex[];
extern const char kContentSettingsWindowLastTabIndex[];
extern const char kCertificateManagerWindowLastTabIndex[];
extern const char kShouldShowFirstRunBubble[];
extern const char kShouldShowWelcomePage[];

extern const char kLastKnownGoogleURL[];
extern const char kLastPromptedGoogleURL[];
extern const char kLastKnownIntranetRedirectOrigin[];

extern const char kCountryIDAtInstall[];
extern const char kGeoIDAtInstall[];  // OBSOLETE

extern const char kShutdownType[];
extern const char kShutdownNumProcesses[];
extern const char kShutdownNumProcessesSlow[];

extern const char kRestartLastSessionOnShutdown[];
extern const char kWasRestarted[];

extern const char kNumBookmarksOnBookmarkBar[];
extern const char kNumFoldersOnBookmarkBar[];
extern const char kNumBookmarksInOtherBookmarkFolder[];
extern const char kNumFoldersInOtherBookmarkFolder[];

extern const char kNumKeywords[];

extern const char kDisableVideoAndChat[];

extern const char kDisableExtensions[];
extern const char kDisablePluginFinder[];
extern const char kBrowserActionContainerWidth[];

extern const char kLastExtensionsUpdateCheck[];
extern const char kNextExtensionsUpdateCheck[];

extern const char kExtensionInstallAllowList[];
extern const char kExtensionInstallDenyList[];

extern const char kExtensionAlertsInitializedPref[];

extern const char kExtensionInstallForceList[];

extern const char kExtensionBlacklistUpdateVersion[];

extern const char kNTPTipsResourceServer[];

extern const char kNTP4IntroDisplayCount[];
extern const char kNTPMostVisitedURLsBlacklist[];
extern const char kNTPMostVisitedPinnedURLs[];
extern const char kNTPPromoResourceCache[];
extern const char kNTPPromoResourceCacheUpdate[];
extern const char kNTPPromoIsLoggedInToPlus[];
extern const char kNTPPromoFeatureMask[];
extern const char kNTPPromoResourceServer[];
extern const char kNTPDateResourceServer[];
extern const char kNTPShownBookmarksFolder[];
extern const char kNTPShownPage[];
extern const char kNTPCustomLogoStart[];
extern const char kNTPCustomLogoEnd[];
extern const char kNTPPromoVersion[];
extern const char kNTPPromoLocale[];
extern const char kNTPPromoStart[];
extern const char kNTPPromoEnd[];
extern const char kNTPPromoLine[];
extern const char kNTPPromoClosed[];
extern const char kNTPPromoGroup[];
extern const char kNTPPromoGroupTimeSlice[];
extern const char kNTPPromoGroupMax[];
extern const char kNTPPromoViews[];
extern const char kNTPPromoViewsMax[];
extern const char kNTPPromoPlatform[];
extern const char kNTPPromoBuild[];
extern const char kNTPSignInPromoGroup[];
extern const char kNTPSignInPromoGroupMax[];
extern const char kNTPWebStoreEnabled[];
extern const char kNTPWebStorePromoLastId[];
extern const char kNTPWebStorePromoId[];
extern const char kNTPWebStorePromoHeader[];
extern const char kNTPWebStorePromoButton[];
extern const char kNTPWebStorePromoLink[];
extern const char kNTPWebStorePromoLogo[];
extern const char kNTPWebStorePromoLogoSource[];
extern const char kNTPWebStorePromoExpire[];
extern const char kNTPWebStorePromoUserGroup[];
extern const char kNTPAppPageNames[];
extern const char kNTPHideWebStorePromo[];

extern const char kDevToolsDisabled[];
extern const char kDevToolsOpenDocked[];
extern const char kDevToolsDockSide[];
extern const char kDevToolsSplitLocation[];

extern const char kSyncLastSyncedTime[];
extern const char kSyncHasSetupCompleted[];
extern const char kSyncKeepEverythingSynced[];
extern const char kSyncBookmarks[];
extern const char kSyncPasswords[];
extern const char kSyncPreferences[];
extern const char kSyncAppNotifications[];
extern const char kSyncAppSettings[];
extern const char kSyncApps[];
extern const char kSyncAutofill[];
extern const char kSyncAutofillProfile[];
extern const char kSyncThemes[];
extern const char kSyncTypedUrls[];
extern const char kSyncExtensions[];
extern const char kSyncExtensionSettings[];
extern const char kSyncManaged[];
extern const char kSyncSearchEngines[];
extern const char kSyncSessions[];
extern const char kSyncSuppressStart[];
extern const char kGoogleServicesUsername[];
extern const char kSyncUsingOAuth[];
extern const char kSyncUsingSecondaryPassphrase[];
extern const char kSyncEncryptionBootstrapToken[];
extern const char kSyncAcknowledgedSyncTypes[];
extern const char kSyncMaxInvalidationVersions[];

extern const char kSyncPromoStartupCount[];
extern const char kSyncPromoViewCount[];
extern const char kSyncPromoUserSkipped[];
extern const char kSyncPromoShowOnFirstRunAllowed[];
extern const char kSyncPromoShowNTPBubble[];

extern const char kProfileGAIAInfoUpdateTime[];
extern const char kProfileGAIAInfoPictureURL[];

extern const char kWebAppCreateOnDesktop[];
extern const char kWebAppCreateInAppsMenu[];
extern const char kWebAppCreateInQuickLaunchBar[];

extern const char kGeolocationAccessToken[];
extern const char kGeolocationDefaultContentSetting[];
extern const char kGeolocationContentSettings[];

extern const char kLoginDatabaseMigrated[];

extern const char kRemoteAccessHostFirewallTraversal[];

extern const char kPrintingEnabled[];
extern const char kPrintPreviewDisabled[];

extern const char kCloudPrintServiceURL[];
extern const char kCloudPrintSigninURL[];
extern const char kCloudPrintDialogWidth[];
extern const char kCloudPrintDialogHeight[];
extern const char kCloudPrintSigninDialogWidth[];
extern const char kCloudPrintSigninDialogHeight[];
extern const char kCloudPrintProxyEnabled[];
extern const char kCloudPrintProxyId[];
extern const char kCloudPrintAuthToken[];
extern const char kCloudPrintXMPPAuthToken[];
extern const char kCloudPrintEmail[];
extern const char kCloudPrintPrintSystemSettings[];
extern const char kCloudPrintEnableJobPoll[];
extern const char kCloudPrintRobotRefreshToken[];
extern const char kCloudPrintRobotEmail[];
extern const char kVirtualPrinterDriverEnabled[];
extern const char kCloudPrintSubmitEnabled[];

extern const char kProxy[];
extern const char kMaxConnectionsPerProxy[];

extern const char kManagedDefaultCookiesSetting[];
extern const char kManagedDefaultImagesSetting[];
extern const char kManagedDefaultJavaScriptSetting[];
extern const char kManagedDefaultPluginsSetting[];
extern const char kManagedDefaultPopupsSetting[];
extern const char kManagedDefaultGeolocationSetting[];
extern const char kManagedDefaultNotificationsSetting[];

extern const char kManagedCookiesAllowedForUrls[];
extern const char kManagedCookiesBlockedForUrls[];
extern const char kManagedCookiesSessionOnlyForUrls[];
extern const char kManagedImagesAllowedForUrls[];
extern const char kManagedImagesBlockedForUrls[];
extern const char kManagedJavaScriptAllowedForUrls[];
extern const char kManagedJavaScriptBlockedForUrls[];
extern const char kManagedPluginsAllowedForUrls[];
extern const char kManagedPluginsBlockedForUrls[];
extern const char kManagedPopupsAllowedForUrls[];
extern const char kManagedPopupsBlockedForUrls[];
extern const char kManagedNotificationsAllowedForUrls[];
extern const char kManagedNotificationsBlockedForUrls[];
extern const char kManagedAutoSelectCertificateForUrls[];

#if defined(OS_CHROMEOS)
extern const char kSignedSettingsCache[];
extern const char kHardwareKeyboardLayout[];
extern const char kCarrierDealPromoShown[];
extern const char kShouldAutoEnroll[];
extern const char kAutoEnrollmentPowerLimit[];
extern const char kReportDeviceVersionInfo[];
extern const char kReportDeviceActivityTimes[];
#endif

extern const char kClearPluginLSODataEnabled[];
extern const char kDiskCacheDir[];
extern const char kDiskCacheSize[];
extern const char kMediaCacheSize[];

extern const char kChromeOsReleaseChannel[];

extern const char kRegisteredBackgroundContents[];

extern const char kShownAutoLaunchInfobar[];

extern const char kAuthSchemes[];
extern const char kDisableAuthNegotiateCnameLookup[];
extern const char kEnableAuthNegotiatePort[];
extern const char kAuthServerWhitelist[];
extern const char kAuthNegotiateDelegateWhitelist[];
extern const char kGSSAPILibraryName[];
extern const char kAllowCrossOriginAuthPrompt[];

extern const char kRegisteredProtocolHandlers[];
extern const char kIgnoredProtocolHandlers[];
extern const char kCustomHandlersEnabled[];

extern const char kUserCreatedLoginItem[];
extern const char kBackgroundModeEnabled[];

extern const char kDevicePolicyRefreshRate[];
extern const char kUserPolicyRefreshRate[];

extern const char kRecoveryComponentVersion[];
extern const char kComponentUpdaterState[];

}  // namespace prefs

#endif  // CHROME_COMMON_PREF_NAMES_H_
