// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_function_registry.h"

#include "chrome/browser/accessibility/accessibility_extension_api.h"
#include "chrome/browser/bookmarks/bookmark_extension_api.h"
#include "chrome/browser/bookmarks/bookmark_manager_extension_api.h"
#include "chrome/browser/extensions/api/app/app_api.h"
#include "chrome/browser/extensions/api/browsing_data/browsing_data_api.h"
#include "chrome/browser/extensions/api/cloud_print_private/cloud_print_private_api.h"
#include "chrome/browser/extensions/api/content_settings/content_settings_api.h"
#include "chrome/browser/extensions/api/context_menu/context_menu_api.h"
#include "chrome/browser/extensions/api/cookies/cookies_api.h"
#include "chrome/browser/extensions/api/debugger/debugger_api.h"
#include "chrome/browser/extensions/api/declarative/declarative_api.h"
#include "chrome/browser/extensions/api/extension_action/extension_browser_actions_api.h"
#include "chrome/browser/extensions/api/extension_action/extension_page_actions_api.h"
#include "chrome/browser/extensions/api/extension_action/extension_script_badge_api.h"
#include "chrome/browser/extensions/api/identity/identity_api.h"
#include "chrome/browser/extensions/api/i18n/i18n_api.h"
#include "chrome/browser/extensions/api/idle/idle_api.h"
#include "chrome/browser/extensions/api/managed_mode/managed_mode_api.h"
#include "chrome/browser/extensions/api/management/management_api.h"
#include "chrome/browser/extensions/api/metrics/metrics.h"
#include "chrome/browser/extensions/api/offscreen_tabs/offscreen_tabs_api.h"
#include "chrome/browser/extensions/api/omnibox/omnibox_api.h"
#include "chrome/browser/extensions/api/page_capture/page_capture_api.h"
#include "chrome/browser/extensions/api/permissions/permissions_api.h"
#include "chrome/browser/extensions/api/record/record_api.h"
#include "chrome/browser/extensions/api/runtime/runtime_api.h"
#include "chrome/browser/extensions/api/serial/serial_api.h"
#include "chrome/browser/extensions/api/socket/socket_api.h"
#include "chrome/browser/extensions/api/tabs/execute_code_in_tab_function.h"
#include "chrome/browser/extensions/api/tabs/tabs.h"
#include "chrome/browser/extensions/api/test/test_api.h"
#include "chrome/browser/extensions/api/web_navigation/web_navigation_api.h"
#include "chrome/browser/extensions/api/web_request/web_request_api.h"
#include "chrome/browser/extensions/api/web_socket_proxy_private/web_socket_proxy_private_api.h"
#include "chrome/browser/extensions/api/webstore_private/webstore_private_api.h"
#include "chrome/browser/extensions/extension_font_settings_api.h"
#include "chrome/browser/extensions/extension_module.h"
#include "chrome/browser/extensions/extension_preference_api.h"
#include "chrome/browser/extensions/extension_processes_api.h"
#include "chrome/browser/extensions/settings/settings_api.h"
#include "chrome/browser/extensions/system/system_api.h"
#include "chrome/browser/history/history_extension_api.h"
#include "chrome/browser/history/top_sites_extension_api.h"
#include "chrome/browser/infobars/infobar_extension_api.h"
#include "chrome/browser/rlz/rlz_extension_api.h"
#include "chrome/browser/speech/extension_api/tts_engine_extension_api.h"
#include "chrome/browser/speech/extension_api/tts_extension_api.h"
#include "chrome/browser/speech/speech_input_extension_api.h"
#include "chrome/common/extensions/api/generated_api.h"

#if defined(TOOLKIT_VIEWS)
#include "chrome/browser/extensions/extension_input_api.h"
#endif

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/extensions/echo_private_api.h"
#include "chrome/browser/chromeos/extensions/file_browser_handler_api.h"
#include "chrome/browser/chromeos/extensions/file_browser_private_api.h"
#include "chrome/browser/chromeos/extensions/wallpaper_private_api.h"
#include "chrome/browser/chromeos/media/media_player_extension_api.h"
#include "chrome/browser/extensions/api/input_ime/input_ime_api.h"
#include "chrome/browser/extensions/api/terminal/terminal_private_api.h"
#include "chrome/browser/extensions/extension_info_private_api_chromeos.h"
#include "chrome/browser/extensions/extension_input_method_api.h"
#endif

// static
ExtensionFunctionRegistry* ExtensionFunctionRegistry::GetInstance() {
  return Singleton<ExtensionFunctionRegistry>::get();
}

ExtensionFunctionRegistry::ExtensionFunctionRegistry() {
  ResetFunctions();
}

ExtensionFunctionRegistry::~ExtensionFunctionRegistry() {
}

void ExtensionFunctionRegistry::ResetFunctions() {
#if defined(ENABLE_EXTENSIONS)

  // Register all functions here.

  // Windows
  RegisterFunction<GetWindowFunction>();
  RegisterFunction<GetCurrentWindowFunction>();
  RegisterFunction<GetLastFocusedWindowFunction>();
  RegisterFunction<GetAllWindowsFunction>();
  RegisterFunction<CreateWindowFunction>();
  RegisterFunction<UpdateWindowFunction>();
  RegisterFunction<RemoveWindowFunction>();

  // Tabs
  RegisterFunction<GetTabFunction>();
  RegisterFunction<GetCurrentTabFunction>();
  RegisterFunction<GetSelectedTabFunction>();
  RegisterFunction<GetAllTabsInWindowFunction>();
  RegisterFunction<QueryTabsFunction>();
  RegisterFunction<HighlightTabsFunction>();
  RegisterFunction<CreateTabFunction>();
  RegisterFunction<UpdateTabFunction>();
  RegisterFunction<MoveTabsFunction>();
  RegisterFunction<ReloadTabFunction>();
  RegisterFunction<RemoveTabsFunction>();
  RegisterFunction<DetectTabLanguageFunction>();
  RegisterFunction<CaptureVisibleTabFunction>();
  RegisterFunction<TabsExecuteScriptFunction>();
  RegisterFunction<TabsInsertCSSFunction>();

  // Page Actions.
  RegisterFunction<EnablePageActionsFunction>();
  RegisterFunction<DisablePageActionsFunction>();
  RegisterFunction<PageActionShowFunction>();
  RegisterFunction<PageActionHideFunction>();
  RegisterFunction<PageActionSetIconFunction>();
  RegisterFunction<PageActionSetTitleFunction>();
  RegisterFunction<PageActionSetPopupFunction>();
  RegisterFunction<PageActionGetTitleFunction>();
  RegisterFunction<PageActionGetPopupFunction>();

  // Browser Actions.
  RegisterFunction<BrowserActionSetIconFunction>();
  RegisterFunction<BrowserActionSetTitleFunction>();
  RegisterFunction<BrowserActionSetBadgeTextFunction>();
  RegisterFunction<BrowserActionSetBadgeBackgroundColorFunction>();
  RegisterFunction<BrowserActionSetPopupFunction>();
  RegisterFunction<BrowserActionGetTitleFunction>();
  RegisterFunction<BrowserActionGetBadgeTextFunction>();
  RegisterFunction<BrowserActionGetBadgeBackgroundColorFunction>();
  RegisterFunction<BrowserActionGetPopupFunction>();
  RegisterFunction<BrowserActionEnableFunction>();
  RegisterFunction<BrowserActionDisableFunction>();

  // Script Badges.
  RegisterFunction<ScriptBadgeGetAttentionFunction>();
  RegisterFunction<ScriptBadgeGetPopupFunction>();
  RegisterFunction<ScriptBadgeSetPopupFunction>();

  // Browsing Data.
  RegisterFunction<RemoveBrowsingDataFunction>();
  RegisterFunction<RemoveAppCacheFunction>();
  RegisterFunction<RemoveCacheFunction>();
  RegisterFunction<RemoveCookiesFunction>();
  RegisterFunction<RemoveDownloadsFunction>();
  RegisterFunction<RemoveFileSystemsFunction>();
  RegisterFunction<RemoveFormDataFunction>();
  RegisterFunction<RemoveHistoryFunction>();
  RegisterFunction<RemoveIndexedDBFunction>();
  RegisterFunction<RemoveLocalStorageFunction>();
  RegisterFunction<RemoveServerBoundCertsFunction>();
  RegisterFunction<RemovePluginDataFunction>();
  RegisterFunction<RemovePasswordsFunction>();
  RegisterFunction<RemoveWebSQLFunction>();

  // Bookmarks.
  RegisterFunction<GetBookmarksFunction>();
  RegisterFunction<GetBookmarkChildrenFunction>();
  RegisterFunction<GetBookmarkRecentFunction>();
  RegisterFunction<GetBookmarkTreeFunction>();
  RegisterFunction<GetBookmarkSubTreeFunction>();
  RegisterFunction<SearchBookmarksFunction>();
  RegisterFunction<RemoveBookmarkFunction>();
  RegisterFunction<RemoveTreeBookmarkFunction>();
  RegisterFunction<CreateBookmarkFunction>();
  RegisterFunction<MoveBookmarkFunction>();
  RegisterFunction<UpdateBookmarkFunction>();

  // Infobars.
  RegisterFunction<ShowInfoBarFunction>();

  // BookmarkManager
  RegisterFunction<CopyBookmarkManagerFunction>();
  RegisterFunction<CutBookmarkManagerFunction>();
  RegisterFunction<PasteBookmarkManagerFunction>();
  RegisterFunction<CanPasteBookmarkManagerFunction>();
  RegisterFunction<ImportBookmarksFunction>();
  RegisterFunction<ExportBookmarksFunction>();
  RegisterFunction<SortChildrenBookmarkManagerFunction>();
  RegisterFunction<BookmarkManagerGetStringsFunction>();
  RegisterFunction<StartDragBookmarkManagerFunction>();
  RegisterFunction<DropBookmarkManagerFunction>();
  RegisterFunction<GetSubtreeBookmarkManagerFunction>();
  RegisterFunction<CanEditBookmarkManagerFunction>();
  RegisterFunction<CanOpenNewWindowsBookmarkFunction>();

  // History
  RegisterFunction<AddUrlHistoryFunction>();
  RegisterFunction<DeleteAllHistoryFunction>();
  RegisterFunction<DeleteRangeHistoryFunction>();
  RegisterFunction<DeleteUrlHistoryFunction>();
  RegisterFunction<GetVisitsHistoryFunction>();
  RegisterFunction<SearchHistoryFunction>();

  // Idle
  RegisterFunction<extensions::ExtensionIdleQueryStateFunction>();

  // I18N.
  RegisterFunction<GetAcceptLanguagesFunction>();

  // Processes.
  RegisterFunction<GetProcessIdForTabFunction>();
  RegisterFunction<TerminateFunction>();
  RegisterFunction<GetProcessInfoFunction>();

  // Metrics.
  RegisterFunction<extensions::MetricsRecordUserActionFunction>();
  RegisterFunction<extensions::MetricsRecordValueFunction>();
  RegisterFunction<extensions::MetricsRecordPercentageFunction>();
  RegisterFunction<extensions::MetricsRecordCountFunction>();
  RegisterFunction<extensions::MetricsRecordSmallCountFunction>();
  RegisterFunction<extensions::MetricsRecordMediumCountFunction>();
  RegisterFunction<extensions::MetricsRecordTimeFunction>();
  RegisterFunction<extensions::MetricsRecordMediumTimeFunction>();
  RegisterFunction<extensions::MetricsRecordLongTimeFunction>();

  // RLZ.
#if defined(OS_WIN) || defined(OS_MACOSX)
  RegisterFunction<RlzRecordProductEventFunction>();
  RegisterFunction<RlzGetAccessPointRlzFunction>();
  RegisterFunction<RlzSendFinancialPingFunction>();
  RegisterFunction<RlzClearProductStateFunction>();
#endif

  // Cookies.
  RegisterFunction<extensions::GetCookieFunction>();
  RegisterFunction<extensions::GetAllCookiesFunction>();
  RegisterFunction<extensions::SetCookieFunction>();
  RegisterFunction<extensions::RemoveCookieFunction>();
  RegisterFunction<extensions::GetAllCookieStoresFunction>();

  // Test.
  RegisterFunction<extensions::TestNotifyPassFunction>();
  RegisterFunction<extensions::TestFailFunction>();
  RegisterFunction<extensions::TestLogFunction>();
  RegisterFunction<extensions::TestResetQuotaFunction>();
  RegisterFunction<extensions::TestCreateIncognitoTabFunction>();
  RegisterFunction<extensions::TestSendMessageFunction>();
  RegisterFunction<extensions::TestGetConfigFunction>();

  // Record.
  RegisterFunction<extensions::CaptureURLsFunction>();
  RegisterFunction<extensions::ReplayURLsFunction>();

  // Accessibility.
  RegisterFunction<GetFocusedControlFunction>();
  RegisterFunction<SetAccessibilityEnabledFunction>();
  RegisterFunction<GetAlertsForTabFunction>();

  // Text-to-speech.
  RegisterFunction<ExtensionTtsEngineSendTtsEventFunction>();
  RegisterFunction<ExtensionTtsGetVoicesFunction>();
  RegisterFunction<ExtensionTtsIsSpeakingFunction>();
  RegisterFunction<ExtensionTtsSpeakFunction>();
  RegisterFunction<ExtensionTtsStopSpeakingFunction>();

  // Context Menus.
  RegisterFunction<extensions::CreateContextMenuFunction>();
  RegisterFunction<extensions::UpdateContextMenuFunction>();
  RegisterFunction<extensions::RemoveContextMenuFunction>();
  RegisterFunction<extensions::RemoveAllContextMenusFunction>();

  // Omnibox.
  RegisterFunction<extensions::OmniboxSendSuggestionsFunction>();
  RegisterFunction<extensions::OmniboxSetDefaultSuggestionFunction>();

#if defined(ENABLE_INPUT_SPEECH)
  // Speech input.
  RegisterFunction<StartSpeechInputFunction>();
  RegisterFunction<StopSpeechInputFunction>();
  RegisterFunction<IsRecordingSpeechInputFunction>();
#endif

#if defined(TOOLKIT_VIEWS)
  // Input.
  RegisterFunction<SendKeyboardEventInputFunction>();
#endif

#if defined(OS_CHROMEOS)
  // IME
  RegisterFunction<extensions::SetCompositionFunction>();
  RegisterFunction<extensions::ClearCompositionFunction>();
  RegisterFunction<extensions::CommitTextFunction>();
  RegisterFunction<extensions::SetCandidateWindowPropertiesFunction>();
  RegisterFunction<extensions::SetCandidatesFunction>();
  RegisterFunction<extensions::SetCursorPositionFunction>();
  RegisterFunction<extensions::SetMenuItemsFunction>();
  RegisterFunction<extensions::UpdateMenuItemsFunction>();

  RegisterFunction<extensions::InputEventHandled>();
#endif

  // Managed mode.
  RegisterFunction<extensions::GetManagedModeFunction>();
  RegisterFunction<extensions::EnterManagedModeFunction>();
  RegisterFunction<extensions::GetPolicyFunction>();
  RegisterFunction<extensions::SetPolicyFunction>();

  // Management.
  RegisterFunction<GetAllExtensionsFunction>();
  RegisterFunction<GetExtensionByIdFunction>();
  RegisterFunction<GetPermissionWarningsByIdFunction>();
  RegisterFunction<GetPermissionWarningsByManifestFunction>();
  RegisterFunction<LaunchAppFunction>();
  RegisterFunction<SetEnabledFunction>();
  RegisterFunction<UninstallFunction>();

  // Extension module.
  RegisterFunction<SetUpdateUrlDataFunction>();
  RegisterFunction<IsAllowedIncognitoAccessFunction>();
  RegisterFunction<IsAllowedFileSchemeAccessFunction>();

  // WebstorePrivate.
  RegisterFunction<extensions::GetBrowserLoginFunction>();
  RegisterFunction<extensions::GetStoreLoginFunction>();
  RegisterFunction<extensions::SetStoreLoginFunction>();
  RegisterFunction<extensions::InstallBundleFunction>();
  RegisterFunction<extensions::BeginInstallWithManifestFunction>();
  RegisterFunction<extensions::CompleteInstallFunction>();
  RegisterFunction<extensions::SilentlyInstallFunction>();
  RegisterFunction<extensions::GetWebGLStatusFunction>();

  // WebNavigation.
  RegisterFunction<extensions::GetFrameFunction>();
  RegisterFunction<extensions::GetAllFramesFunction>();

  // WebRequest.
  RegisterFunction<WebRequestAddEventListener>();
  RegisterFunction<WebRequestEventHandled>();
  RegisterFunction<WebRequestHandlerBehaviorChanged>();

  // Preferences.
  RegisterFunction<GetPreferenceFunction>();
  RegisterFunction<SetPreferenceFunction>();
  RegisterFunction<ClearPreferenceFunction>();

  // ChromeOS-specific part of the API.
#if defined(OS_CHROMEOS)
  // Device Customization.
  RegisterFunction<GetChromeosInfoFunction>();

  // FileBrowserPrivate functions.
  // TODO(jamescook): Expose these on non-ChromeOS platforms so we can use
  // the extension-based file picker on Aura. crbug.com/97424
  RegisterFunction<CancelFileDialogFunction>();
  RegisterFunction<ExecuteTasksFileBrowserFunction>();
  RegisterFunction<SetDefaultTaskFileBrowserFunction>();
  RegisterFunction<FileDialogStringsFunction>();
  RegisterFunction<GetFileTasksFileBrowserFunction>();
  RegisterFunction<GetVolumeMetadataFunction>();
  RegisterFunction<RequestLocalFileSystemFunction>();
  RegisterFunction<AddFileWatchBrowserFunction>();
  RegisterFunction<RemoveFileWatchBrowserFunction>();
  RegisterFunction<SelectFileFunction>();
  RegisterFunction<SelectFilesFunction>();
  RegisterFunction<AddMountFunction>();
  RegisterFunction<RemoveMountFunction>();
  RegisterFunction<GetMountPointsFunction>();
  RegisterFunction<GetSizeStatsFunction>();
  RegisterFunction<FormatDeviceFunction>();
  RegisterFunction<ViewFilesFunction>();
  RegisterFunction<ToggleFullscreenFunction>();
  RegisterFunction<IsFullscreenFunction>();
  RegisterFunction<GetGDataFilePropertiesFunction>();
  RegisterFunction<PinGDataFileFunction>();
  RegisterFunction<GetFileLocationsFunction>();
  RegisterFunction<GetGDataFilesFunction>();
  RegisterFunction<GetFileTransfersFunction>();
  RegisterFunction<CancelFileTransfersFunction>();
  RegisterFunction<TransferFileFunction>();
  RegisterFunction<GetGDataPreferencesFunction>();
  RegisterFunction<SetGDataPreferencesFunction>();
  RegisterFunction<SearchDriveFunction>();
  RegisterFunction<ClearDriveCacheFunction>();
  RegisterFunction<GetNetworkConnectionStateFunction>();
  RegisterFunction<RequestDirectoryRefreshFunction>();

  // FileBrowserHandler.
  RegisterFunction<FileHandlerSelectFileFunction>();

  // Mediaplayer
  RegisterFunction<PlayMediaplayerFunction>();
  RegisterFunction<GetPlaylistMediaplayerFunction>();
  RegisterFunction<SetWindowHeightMediaplayerFunction>();
  RegisterFunction<CloseWindowMediaplayerFunction>();

  // WallpaperManagerPrivate functions.
  RegisterFunction<WallpaperStringsFunction>();
  RegisterFunction<WallpaperSetWallpaperFunction>();

  // InputMethod
  RegisterFunction<GetInputMethodFunction>();

  // Echo
  RegisterFunction<GetRegistrationCodeFunction>();

  // Terminal
  RegisterFunction<OpenTerminalProcessFunction>();
  RegisterFunction<SendInputToTerminalProcessFunction>();
  RegisterFunction<CloseTerminalProcessFunction>();
  RegisterFunction<OnTerminalResizeFunction>();
#endif

  // Websocket to TCP proxy. Currently noop on anything other than ChromeOS.
  RegisterFunction<
      extensions::WebSocketProxyPrivateGetPassportForTCPFunction>();
  RegisterFunction<extensions::WebSocketProxyPrivateGetURLForTCPFunction>();

  // Debugger
  RegisterFunction<AttachDebuggerFunction>();
  RegisterFunction<DetachDebuggerFunction>();
  RegisterFunction<SendCommandDebuggerFunction>();

  // Settings
  RegisterFunction<extensions::GetSettingsFunction>();
  RegisterFunction<extensions::SetSettingsFunction>();
  RegisterFunction<extensions::RemoveSettingsFunction>();
  RegisterFunction<extensions::ClearSettingsFunction>();
  RegisterFunction<extensions::GetBytesInUseSettingsFunction>();

  // Content settings.
  RegisterFunction<extensions::GetResourceIdentifiersFunction>();
  RegisterFunction<extensions::ClearContentSettingsFunction>();
  RegisterFunction<extensions::GetContentSettingFunction>();
  RegisterFunction<extensions::SetContentSettingFunction>();

  // Font settings.
  RegisterFunction<GetFontListFunction>();
  RegisterFunction<ClearFontFunction>();
  RegisterFunction<GetFontFunction>();
  RegisterFunction<SetFontFunction>();
  RegisterFunction<ClearDefaultFontSizeFunction>();
  RegisterFunction<GetDefaultFontSizeFunction>();
  RegisterFunction<SetDefaultFontSizeFunction>();
  RegisterFunction<ClearDefaultFixedFontSizeFunction>();
  RegisterFunction<GetDefaultFixedFontSizeFunction>();
  RegisterFunction<SetDefaultFixedFontSizeFunction>();
  RegisterFunction<ClearMinimumFontSizeFunction>();
  RegisterFunction<GetMinimumFontSizeFunction>();
  RegisterFunction<SetMinimumFontSizeFunction>();

  // CloudPrint settings.
  RegisterFunction<extensions::CloudPrintSetCredentialsFunction>();

  // Experimental App API.
  RegisterFunction<extensions::AppNotifyFunction>();
  RegisterFunction<extensions::AppClearAllNotificationsFunction>();

  // Permissions
  RegisterFunction<ContainsPermissionsFunction>();
  RegisterFunction<GetAllPermissionsFunction>();
  RegisterFunction<RemovePermissionsFunction>();
  RegisterFunction<RequestPermissionsFunction>();

  // PageCapture
  RegisterFunction<extensions::PageCaptureSaveAsMHTMLFunction>();

  // TopSites
  RegisterFunction<GetTopSitesFunction>();

  // Serial
  RegisterFunction<extensions::SerialOpenFunction>();
  RegisterFunction<extensions::SerialCloseFunction>();
  RegisterFunction<extensions::SerialReadFunction>();
  RegisterFunction<extensions::SerialWriteFunction>();

  // Sockets
  RegisterFunction<extensions::SocketCreateFunction>();
  RegisterFunction<extensions::SocketDestroyFunction>();
  RegisterFunction<extensions::SocketConnectFunction>();
  RegisterFunction<extensions::SocketDisconnectFunction>();
  RegisterFunction<extensions::SocketReadFunction>();
  RegisterFunction<extensions::SocketWriteFunction>();

  // System
  RegisterFunction<extensions::GetIncognitoModeAvailabilityFunction>();
  RegisterFunction<extensions::GetUpdateStatusFunction>();

  // Net
  RegisterFunction<extensions::AddRulesFunction>();
  RegisterFunction<extensions::RemoveRulesFunction>();
  RegisterFunction<extensions::GetRulesFunction>();

  // Experimental Offscreen Tabs
  RegisterFunction<CreateOffscreenTabFunction>();
  RegisterFunction<GetOffscreenTabFunction>();
  RegisterFunction<GetAllOffscreenTabFunction>();
  RegisterFunction<RemoveOffscreenTabFunction>();
  RegisterFunction<SendKeyboardEventOffscreenTabFunction>();
  RegisterFunction<SendMouseEventOffscreenTabFunction>();
  RegisterFunction<ToDataUrlOffscreenTabFunction>();
  RegisterFunction<UpdateOffscreenTabFunction>();

  // Runtime
  RegisterFunction<extensions::RuntimeGetBackgroundPageFunction>();

  // Generated APIs
  extensions::api::GeneratedFunctionRegistry::RegisterAll(this);
#endif  // defined(ENABLE_EXTENSIONS)
}

void ExtensionFunctionRegistry::GetAllNames(std::vector<std::string>* names) {
  for (FactoryMap::iterator iter = factories_.begin();
       iter != factories_.end(); ++iter) {
    names->push_back(iter->first);
  }
}

bool ExtensionFunctionRegistry::OverrideFunction(
    const std::string& name,
    ExtensionFunctionFactory factory) {
  FactoryMap::iterator iter = factories_.find(name);
  if (iter == factories_.end()) {
    return false;
  } else {
    iter->second = factory;
    return true;
  }
}

ExtensionFunction* ExtensionFunctionRegistry::NewFunction(
    const std::string& name) {
  FactoryMap::iterator iter = factories_.find(name);
  DCHECK(iter != factories_.end());
  ExtensionFunction* function = iter->second();
  function->set_name(name);
  return function;
}
