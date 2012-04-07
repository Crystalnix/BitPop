// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/chrome_blob_storage_context.h"

#include "webkit/blob/blob_storage_controller.h"

using content::BrowserThread;
using webkit_blob::BlobStorageController;

ChromeBlobStorageContext::ChromeBlobStorageContext() {
}

void ChromeBlobStorageContext::InitializeOnIOThread() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  controller_.reset(new BlobStorageController());
}

ChromeBlobStorageContext::~ChromeBlobStorageContext() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
}
