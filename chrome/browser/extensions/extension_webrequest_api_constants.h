// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Constants used for the WebRequest API.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_WEBREQUEST_API_CONSTANTS_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_WEBREQUEST_API_CONSTANTS_H_
#pragma once

namespace extension_webrequest_api_constants {

// Keys.
extern const char kErrorKey[];
extern const char kIpKey[];
extern const char kMethodKey[];
extern const char kRedirectUrlKey[];
extern const char kRequestIdKey[];
extern const char kStatusCodeKey[];
extern const char kStatusLineKey[];
extern const char kTabIdKey[];
extern const char kTimeStampKey[];
extern const char kTypeKey[];
extern const char kUrlKey[];
extern const char kRequestHeadersKey[];
extern const char kResponseHeadersKey[];
extern const char kHeadersKey[];
extern const char kHeaderNameKey[];
extern const char kHeaderValueKey[];

// Events.
extern const char kOnBeforeRedirect[];
extern const char kOnBeforeRequest[];
extern const char kOnBeforeSendHeaders[];
extern const char kOnCompleted[];
extern const char kOnErrorOccurred[];
extern const char kOnResponseStarted[];
extern const char kOnRequestSent[];

// Error messages.
extern const char kInvalidRedirectUrl[];
extern const char kInvalidBlockingResponse[];
extern const char kInvalidRequestFilterUrl[];

}  // namespace extension_webrequest_api_constants

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_WEBREQUEST_API_CONSTANTS_H_
