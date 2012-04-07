// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_TEST_TEST_BROWSER_CONTEXT_H_
#define CONTENT_TEST_TEST_BROWSER_CONTEXT_H_
#pragma once

#include "base/compiler_specific.h"
#include "base/file_path.h"
#include "base/gtest_prod_util.h"
#include "base/memory/ref_counted.h"
#include "base/scoped_temp_dir.h"
#include "content/public/browser/browser_context.h"

class WebKitContext;

class TestBrowserContext : public content::BrowserContext {
 public:
  TestBrowserContext();
  virtual ~TestBrowserContext();

  virtual FilePath GetPath() OVERRIDE;
  virtual bool IsOffTheRecord() OVERRIDE;
  virtual SSLHostState* GetSSLHostState() OVERRIDE;
  virtual content::DownloadManager* GetDownloadManager() OVERRIDE;
  virtual net::URLRequestContextGetter* GetRequestContext() OVERRIDE;
  virtual net::URLRequestContextGetter* GetRequestContextForRenderProcess(
      int renderer_child_id) OVERRIDE;
  virtual net::URLRequestContextGetter* GetRequestContextForMedia() OVERRIDE;
  virtual const content::ResourceContext& GetResourceContext() OVERRIDE;
  virtual content::HostZoomMap* GetHostZoomMap() OVERRIDE;
  virtual content::GeolocationPermissionContext*
      GetGeolocationPermissionContext() OVERRIDE;
  virtual SpeechInputPreferences* GetSpeechInputPreferences() OVERRIDE;
  virtual bool DidLastSessionExitCleanly() OVERRIDE;
  virtual quota::QuotaManager* GetQuotaManager() OVERRIDE;
  virtual WebKitContext* GetWebKitContext() OVERRIDE;
  virtual webkit_database::DatabaseTracker* GetDatabaseTracker() OVERRIDE;
  virtual ChromeBlobStorageContext* GetBlobStorageContext() OVERRIDE;
  virtual ChromeAppCacheService* GetAppCacheService() OVERRIDE;
  virtual fileapi::FileSystemContext* GetFileSystemContext() OVERRIDE;

 private:
  FRIEND_TEST_ALL_PREFIXES(DOMStorageTest, SessionOnly);
  FRIEND_TEST_ALL_PREFIXES(DOMStorageTest, SaveSessionState);

  // WebKitContext, lazily initialized by GetWebKitContext().
  scoped_refptr<WebKitContext> webkit_context_;

  ScopedTempDir browser_context_dir_;

  DISALLOW_COPY_AND_ASSIGN(TestBrowserContext);
};

#endif  // CONTENT_TEST_TEST_BROWSER_CONTEXT_H_
