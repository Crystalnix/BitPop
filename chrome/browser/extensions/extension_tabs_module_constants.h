// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Constants used for the Tabs API and the Windows API.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_TABS_MODULE_CONSTANTS_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_TABS_MODULE_CONSTANTS_H_
#pragma once

namespace extension_tabs_module_constants {

// Keys used in serializing tab data & events.
extern const char kActiveKey[];
extern const char kAllFramesKey[];
extern const char kBypassCache[];
extern const char kCodeKey[];
extern const char kDrawAttentionKey[];
extern const char kFaviconUrlKey[];
extern const char kFileKey[];
extern const char kFocusedKey[];
extern const char kFormatKey[];
extern const char kFromIndexKey[];
extern const char kHeightKey[];
extern const char kIdKey[];
extern const char kIndexKey[];
extern const char kLeftKey[];
extern const char kNewPositionKey[];
extern const char kNewWindowIdKey[];
extern const char kOldPositionKey[];
extern const char kOldWindowIdKey[];
extern const char kOpenerTabIdKey[];
extern const char kPinnedKey[];
extern const char kPopulateKey[];
extern const char kQualityKey[];
extern const char kHighlightedKey[];
extern const char kSelectedKey[];
extern const char kShowStateKey[];
extern const char kStatusKey[];
extern const char kTabIdKey[];
extern const char kTabIdsKey[];
extern const char kTabsKey[];
extern const char kTabUrlKey[];
extern const char kTitleKey[];
extern const char kToIndexKey[];
extern const char kTopKey[];
extern const char kUrlKey[];
extern const char kWindowClosing[];
extern const char kWidthKey[];
extern const char kWindowIdKey[];
extern const char kIncognitoKey[];
extern const char kWindowTypeKey[];
extern const char kWindowTypeLongKey[];

// Value consts.
extern const char kCanOnlyMoveTabsWithinNormalWindowsError[];
extern const char kCanOnlyMoveTabsWithinSameProfileError[];
extern const char kFormatValueJpeg[];
extern const char kFormatValuePng[];
extern const char kMimeTypeJpeg[];
extern const char kMimeTypePng[];
extern const char kShowStateValueNormal[];
extern const char kShowStateValueMinimized[];
extern const char kShowStateValueMaximized[];
extern const char kStatusValueComplete[];
extern const char kStatusValueLoading[];
extern const char kWindowTypeValueNormal[];
extern const char kWindowTypeValuePopup[];
extern const char kWindowTypeValuePanel[];
extern const char kWindowTypeValueApp[];

// Error messages.
extern const char kNoCrashBrowserError[];
extern const char kNoCurrentWindowError[];
extern const char kNoLastFocusedWindowError[];
extern const char kWindowNotFoundError[];
extern const char kTabIndexNotFoundError[];
extern const char kTabNotFoundError[];
extern const char kTabStripNotEditableError[];
extern const char kNoHighlightedTabError[];
extern const char kNoSelectedTabError[];
extern const char kIncognitoModeIsDisabled[];
extern const char kIncognitoModeIsForced[];
extern const char kURLsNotAllowedInIncognitoError[];
extern const char kInvalidUrlError[];
extern const char kInternalVisibleTabCaptureError[];
extern const char kNotImplementedError[];
extern const char kSupportedInWindowsOnlyError[];
extern const char kInvalidWindowTypeError[];
extern const char kInvalidWindowStateError[];

extern const char kNoCodeOrFileToExecuteError[];
extern const char kMoreThanOneValuesError[];
extern const char kLoadFileError[];
extern const char kCannotDetermineLanguageOfUnloadedTab[];

};  // namespace extension_tabs_module_constants

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_TABS_MODULE_CONSTANTS_H_
