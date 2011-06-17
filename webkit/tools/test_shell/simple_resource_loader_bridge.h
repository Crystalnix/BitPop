// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_TOOLS_TEST_SHELL_SIMPLE_RESOURCE_LOADER_BRIDGE_H__
#define WEBKIT_TOOLS_TEST_SHELL_SIMPLE_RESOURCE_LOADER_BRIDGE_H__

#include <string>
#include "base/message_loop_proxy.h"
#include "net/http/http_cache.h"

class FilePath;
class GURL;
class TestShellRequestContext;

class SimpleResourceLoaderBridge {
 public:
  // Call this function to initialize the simple resource loader bridge.
  // It is safe to call this function multiple times.
  //
  // NOTE: If this function is not called, then a default request context will
  // be initialized lazily.
  //
  static void Init(const FilePath& cache_path,
                   net::HttpCache::Mode cache_mode,
                   bool no_proxy);

  // Call this function to shutdown the simple resource loader bridge.
  static void Shutdown();

  // May only be called after Init.
  static void SetCookie(const GURL& url,
                        const GURL& first_party_for_cookies,
                        const std::string& cookie);
  static std::string GetCookies(const GURL& url,
                                const GURL& first_party_for_cookies);
  static bool EnsureIOThread();
  static void SetAcceptAllCookies(bool accept_all_cookies);

  // These methods should only be called after Init(), and before
  // Shutdown(). The MessageLoops get replaced upon each call to
  // Init(), and destroyed upon a call to ShutDown().
  static scoped_refptr<base::MessageLoopProxy> GetCacheThread();
  static scoped_refptr<base::MessageLoopProxy> GetIoThread();
};

#endif  // WEBKIT_TOOLS_TEST_SHELL_SIMPLE_RESOURCE_LOADER_BRIDGE_H__
