// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_ANDROID_COOKIE_GETTER_IMPL_H_
#define CONTENT_BROWSER_ANDROID_COOKIE_GETTER_IMPL_H_

#include <string>

#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/synchronization/waitable_event.h"
#include "media/base/android/cookie_getter.h"
#include "net/cookies/canonical_cookie.h"

namespace net {
class URLRequestContextGetter;
}

namespace content {

class BrowserContext;
class ResourceContext;

// This class implements media::CookieGetter to retrive cookies
// asynchronously on the UI thread.
class CookieGetterImpl : public media::CookieGetter {
 public:
  // Construct a CookieGetterImpl by passing the BrowserContext reference
  // and renderer_id to retrieve the CookieStore later.
  CookieGetterImpl(
      BrowserContext* browser_context, int renderer_id, int routing_id);
  virtual ~CookieGetterImpl();

  // media::CookieGetter implementation.
  // Must be called on the UI thread.
  virtual void GetCookies(const std::string& url,
                          const std::string& first_party_for_cookies,
                          const GetCookieCB& callback) OVERRIDE;

 private:
  // Called when GetCookies() finishes.
  void GetCookiesCallback(
      const GetCookieCB& callback, const std::string& cookies);

  // BrowserContext to retrieve URLRequestContext and ResourceContext.
  BrowserContext* browser_context_;

  // Used to post tasks.
  base::WeakPtrFactory<CookieGetterImpl> weak_this_;

  // Render process id, used to check whether the process can access cookies.
  int renderer_id_;

  // Routing id for the render view, used to check tab specific cookie policy.
  int routing_id_;

  DISALLOW_COPY_AND_ASSIGN(CookieGetterImpl);
};

}  // namespace content

#endif  // CONTENT_BROWSER_ANDROID_COOKIE_GETTER_IMPL_H_
