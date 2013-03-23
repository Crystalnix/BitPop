// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_API_WEBDATA_AUTOFILL_WEB_DATA_SERVICE_H_
#define CHROME_BROWSER_API_WEBDATA_AUTOFILL_WEB_DATA_SERVICE_H_

#include "chrome/browser/api/webdata/autofill_web_data.h"

namespace content {
class BrowserContext;
}

// API for Autofill web data.
class AutofillWebDataService
    : public AutofillWebData,
      public WebDataServiceBase {
 public:
  // Retrieve an AutofillWebDataService for the given context.
  //
  // Can return NULL in some contexts.
  static scoped_ptr<AutofillWebDataService> FromBrowserContext(
      content::BrowserContext* context);

  virtual ~AutofillWebDataService() {}
};

#endif  // CHROME_BROWSER_API_WEBDATA_AUTOFILL_WEB_DATA_SERVICE_H_
