// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_BROWSER_WEB_UI_MESSAGE_HANDLER_H_
#define CONTENT_PUBLIC_BROWSER_WEB_UI_MESSAGE_HANDLER_H_
#pragma once

#include "base/basictypes.h"
#include "base/string16.h"
#include "content/common/content_export.h"
#include "testing/gtest/include/gtest/gtest_prod.h"

class GURL;
class WebUIImpl;
class WebUIBrowserTest;

namespace base {
class DictionaryValue;
class ListValue;
}

namespace content {

class WebUI;

// Messages sent from the DOM are forwarded via the WebUI to handler
// classes. These objects are owned by WebUI and destroyed when the
// host is destroyed.
class CONTENT_EXPORT WebUIMessageHandler {
 public:
  WebUIMessageHandler() : web_ui_(NULL) {}
  virtual ~WebUIMessageHandler() {}

 protected:
  // Helper methods:

  // Adds "url" and "title" keys on incoming dictionary, setting title
  // as the url as a fallback on empty title.
  static void SetURLAndTitle(base::DictionaryValue* dictionary,
                             string16 title,
                             const GURL& gurl);

  // Extract an integer value from a list Value.
  static bool ExtractIntegerValue(const base::ListValue* value, int* out_int);

  // Extract a floating point (double) value from a list Value.
  static bool ExtractDoubleValue(const base::ListValue* value,
                                 double* out_value);

  // Extract a string value from a list Value.
  static string16 ExtractStringValue(const base::ListValue* value);

  // This is where subclasses specify which messages they'd like to handle and
  // perform any additional initialization.. At this point web_ui() will return
  // the associated WebUI object.
  virtual void RegisterMessages() = 0;

  // Returns the attached WebUI for this handler.
  WebUI* web_ui() const { return web_ui_; }

 private:
  friend class ::WebUIImpl;
  friend class ::WebUIBrowserTest;
  FRIEND_TEST(WebUIMessageHandlerTest, ExtractIntegerValue);
  FRIEND_TEST(WebUIMessageHandlerTest, ExtractDoubleValue);
  FRIEND_TEST(WebUIMessageHandlerTest, ExtractStringValue);

  void set_web_ui(WebUI* web_ui) { web_ui_ = web_ui; }

  WebUI* web_ui_;
};

}  // namespace content

#endif  // CONTENT_PUBLIC_BROWSER_WEB_UI_MESSAGE_HANDLER_H_

