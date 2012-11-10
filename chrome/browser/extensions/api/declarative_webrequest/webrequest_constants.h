// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Constants used for the WebRequest API.

#ifndef CHROME_BROWSER_EXTENSIONS_API_DECLARATIVE_WEBREQUEST_WEBREQUEST_CONSTANTS_H_
#define CHROME_BROWSER_EXTENSIONS_API_DECLARATIVE_WEBREQUEST_WEBREQUEST_CONSTANTS_H_

namespace extensions {
namespace declarative_webrequest_constants {

// Signals to which WebRequestRulesRegistries are registered.
extern const char kOnRequest[];

// Keys of dictionaries.
extern const char kCookieKey[];
extern const char kContentTypeKey[];
extern const char kDirectionKey[];
extern const char kDomainKey[];
extern const char kExpiresKey[];
extern const char kFilterKey[];
extern const char kFromKey[];
extern const char kHttpOnlyKey[];
extern const char kInstanceTypeKey[];
extern const char kLowerPriorityThanKey[];
extern const char kMaxAgeKey[];
extern const char kModificationKey[];
extern const char kNameKey[];
extern const char kPathKey[];
extern const char kRedirectUrlKey[];
extern const char kResourceTypeKey[];
extern const char kSecureKey[];
extern const char kToKey[];
extern const char kUrlKey[];
extern const char kValueKey[];

// Values of dictionaries, in particular instance types
extern const char kAddRequestCookieType[];
extern const char kAddResponseCookieType[];
extern const char kAddResponseHeaderType[];
extern const char kCancelRequestType[];
extern const char kEditRequestCookieType[];
extern const char kEditResponseCookieType[];
extern const char kIgnoreRulesType[];
extern const char kRedirectByRegExType[];
extern const char kRedirectRequestType[];
extern const char kRedirectToEmptyDocumentType[];
extern const char kRedirectToTransparentImageType[];
extern const char kRemoveRequestCookieType[];
extern const char kRemoveRequestHeaderType[];
extern const char kRemoveResponseCookieType[];
extern const char kRemoveResponseHeaderType[];
extern const char kRequestMatcherType[];
extern const char kSetRequestHeaderType[];

}  // namespace declarative_webrequest_constants
}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_DECLARATIVE_WEBREQUEST_WEBREQUEST_CONSTANTS_H_
