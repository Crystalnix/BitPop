// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Constants used for the WebNavigation API.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_WEBNAVIGATION_API_CONSTANTS_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_WEBNAVIGATION_API_CONSTANTS_H_
#pragma once

namespace extension_webnavigation_api_constants {

// Keys.
extern const char kErrorKey[];
extern const char kErrorOccurredKey[];
extern const char kFrameIdKey[];
extern const char kSourceTabIdKey[];
extern const char kSourceFrameIdKey[];
extern const char kTabIdKey[];
extern const char kTimeStampKey[];
extern const char kTransitionTypeKey[];
extern const char kTransitionQualifiersKey[];
extern const char kUrlKey[];

// Events.
extern const char kOnBeforeNavigate[];
extern const char kOnCommitted[];
extern const char kOnCompleted[];
extern const char kOnCreatedNavigationTarget[];
extern const char kOnDOMContentLoaded[];
extern const char kOnErrorOccurred[];
extern const char kOnReferenceFragmentUpdated[];

}  // namespace extension_webnavigation_api_constants

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_WEBNAVIGATION_API_CONSTANTS_H_
