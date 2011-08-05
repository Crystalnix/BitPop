// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// We handle some special browser-level URLs (like "about:version")
// before they're handed to a renderer.  This lets us do the URL handling
// on the browser side (which has access to more information than the
// renderers do) as well as sidestep the risk of exposing data to
// random web pages (because from the resource loader's perspective, these
// URL schemes don't exist).

#ifndef CHROME_BROWSER_BROWSER_URL_HANDLER_H_
#define CHROME_BROWSER_BROWSER_URL_HANDLER_H_
#pragma once

#include <vector>
#include <utility>

#include "base/gtest_prod_util.h"
#include "base/memory/singleton.h"

class GURL;
class Profile;

// BrowserURLHandler manages the list of all special URLs and manages
// dispatching the URL handling to registered handlers.
class BrowserURLHandler {
 public:
  // The type of functions that can process a URL.
  // If a handler handles |url|, it should :
  // - optionally modify |url| to the URL that should be sent to the renderer
  // If the URL is not handled by a handler, it should return false.
  typedef bool (*URLHandler)(GURL* url, Profile* profile);

  // Returns the singleton instance.
  static BrowserURLHandler* GetInstance();

  // RewriteURLIfNecessary gives all registered URLHandlers a shot at processing
  // the given URL, and modifies it in place.
  // If the original URL needs to be adjusted if the modified URL is redirected,
  // this function sets |reverse_on_redirect| to true.
  void RewriteURLIfNecessary(GURL* url, Profile* profile,
                             bool* reverse_on_redirect);

  // Reverses the rewriting that was done for |original| using the new |url|.
  bool ReverseURLRewrite(GURL* url, const GURL& original,
                         Profile* profile);

  // We initialize the list of url_handlers_ lazily the first time
  // RewriteURLIfNecessary is called.
  void InitURLHandlers();

 private:
  // This object is a singleton:
  BrowserURLHandler();
  ~BrowserURLHandler();
  friend struct DefaultSingletonTraits<BrowserURLHandler>;

  // The list of known URLHandlers, optionally with reverse-rewriters.
  typedef std::pair<URLHandler, URLHandler> HandlerPair;
  std::vector<HandlerPair> url_handlers_;

  // Returns the null handler for use with |AddHandlerPair()|.
  static URLHandler null_handler();

  // Add the specified handler pair to the list of URL handlers.
  void AddHandlerPair(URLHandler handler, URLHandler reverse_handler);

  FRIEND_TEST_ALL_PREFIXES(BrowserURLHandlerTest, BasicRewriteAndReverse);
  FRIEND_TEST_ALL_PREFIXES(BrowserURLHandlerTest, NullHandlerReverse);

  DISALLOW_COPY_AND_ASSIGN(BrowserURLHandler);
};

#endif  // CHROME_BROWSER_BROWSER_URL_HANDLER_H_
