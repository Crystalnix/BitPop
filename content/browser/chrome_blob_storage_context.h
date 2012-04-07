// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_CHROME_BLOB_STORAGE_CONTEXT_H_
#define CONTENT_BROWSER_CHROME_BLOB_STORAGE_CONTEXT_H_
#pragma once

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop_helpers.h"
#include "content/common/content_export.h"
#include "content/public/browser/browser_thread.h"

namespace webkit_blob {
class BlobStorageController;
}

// A context class that keeps track of BlobStorageController used by the chrome.
// There is an instance associated with each BrowserContext. There could be
// multiple URLRequestContexts in the same browser context that refers to the
// same instance.
//
// All methods, except the ctor, are expected to be called on
// the IO thread (unless specifically called out in doc comments).
class CONTENT_EXPORT ChromeBlobStorageContext
    : public base::RefCountedThreadSafe<
          ChromeBlobStorageContext, content::BrowserThread::DeleteOnIOThread> {
 public:
  ChromeBlobStorageContext();

  void InitializeOnIOThread();

  webkit_blob::BlobStorageController* controller() const {
    return controller_.get();
  }

 private:
  friend class base::RefCountedThreadSafe<
      ChromeBlobStorageContext, content::BrowserThread::DeleteOnIOThread>;
  friend class content::BrowserThread;
  friend class base::DeleteHelper<ChromeBlobStorageContext>;

  virtual ~ChromeBlobStorageContext();

  scoped_ptr<webkit_blob::BlobStorageController> controller_;
};

#endif  // CONTENT_BROWSER_CHROME_BLOB_STORAGE_CONTEXT_H_
