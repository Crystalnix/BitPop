// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/common/content_constants.h"

namespace content {

// This number used to be limited to 32 in the past (see b/535234).
const unsigned int kMaxRendererProcessCount = 42;
const int kMaxSessionHistoryEntries = 50;
const size_t kMaxTitleChars = 4 * 1024;
const size_t kMaxURLChars = 2 * 1024 * 1024;
const size_t kMaxURLDisplayChars = 32 * 1024;

#if defined(GOOGLE_CHROME_BUILD)
const wchar_t kBrowserAppName[] = L"Chrome";
const char    kStatsFilename[] = "ChromeStats2";
#else
const wchar_t kBrowserAppName[] = L"Chromium";
const char    kStatsFilename[] = "ChromiumStats2";
#endif

const int kStatsMaxThreads = 32;
const int kStatsMaxCounters = 3000;

}  // namespace content
