// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/support/test_webplugin_page_delegate.h"

#include "third_party/WebKit/Source/WebKit/chromium/public/WebKit.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebKitPlatformSupport.h"

namespace webkit_support {

webkit::npapi::WebPluginDelegate*
TestWebPluginPageDelegate::CreatePluginDelegate(
    const FilePath& file_path,
    const std::string& mime_type) {
  // We don't need a valid native window handle in layout tests.
  // So just passing 0.
  return webkit::npapi::WebPluginDelegateImpl::Create(
      file_path, mime_type, 0);
}

WebKit::WebPlugin* TestWebPluginPageDelegate::CreatePluginReplacement(
    const FilePath& file_path) {
  return NULL;
}

WebKit::WebCookieJar* TestWebPluginPageDelegate::GetCookieJar() {
  return WebKit::webKitPlatformSupport()->cookieJar();
}

}  // namespace webkit_support

