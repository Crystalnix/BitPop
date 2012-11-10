// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_GDATA_GDATA_CONTACTS_SERVICE_H_
#define CHROME_BROWSER_CHROMEOS_GDATA_GDATA_CONTACTS_SERVICE_H_

#include <set>
#include <vector>

#include "base/basictypes.h"
#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/scoped_vector.h"
#include "base/time.h"
#include "chrome/browser/chromeos/gdata/gdata_errorcode.h"
#include "googleurl/src/gurl.h"

class Profile;

namespace base {
class Value;
}

namespace contacts {
class Contact;
}

namespace gdata {

class GDataAuthService;
class GDataOperationRunner;

// Interface for fetching a user's Google contacts via the Contacts API
// (described at https://developers.google.com/google-apps/contacts/v3/).
class GDataContactsServiceInterface {
 public:
  typedef base::Callback<void(scoped_ptr<ScopedVector<contacts::Contact> >)>
      SuccessCallback;
  typedef base::Closure FailureCallback;

  virtual ~GDataContactsServiceInterface() {}

  virtual void Initialize() = 0;

  // Downloads all contacts changed at or after |min_update_time| and invokes
  // the appropriate callback asynchronously on the UI thread when complete.  If
  // min_update_time.is_null() is true, all contacts will be returned.
  virtual void DownloadContacts(SuccessCallback success_callback,
                                FailureCallback failure_callback,
                                const base::Time& min_update_time) = 0;

 protected:
  GDataContactsServiceInterface() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(GDataContactsServiceInterface);
};

class GDataContactsService : public GDataContactsServiceInterface {
 public:
  typedef base::Callback<std::string(const std::string&)>
      RewritePhotoUrlCallback;

  explicit GDataContactsService(Profile* profile);
  virtual ~GDataContactsService();

  GDataAuthService* auth_service_for_testing();

  void set_max_simultaneous_photo_downloads_for_testing(int max_downloads) {
    max_simultaneous_photo_downloads_ = max_downloads;
  }
  void set_feed_url_for_testing(const GURL& url) {
    feed_url_for_testing_ = url;
  }
  void set_rewrite_photo_url_callback_for_testing(RewritePhotoUrlCallback cb) {
    rewrite_photo_url_callback_for_testing_ = cb;
  }

  // Overridden from GDataContactsServiceInterface:
  virtual void Initialize() OVERRIDE;
  virtual void DownloadContacts(SuccessCallback success_callback,
                                FailureCallback failure_callback,
                                const base::Time& min_update_time) OVERRIDE;

 private:
  class DownloadContactsRequest;

  // Invoked by a download request once it's finished (either successfully or
  // unsuccessfully).
  void OnRequestComplete(DownloadContactsRequest* request);

  Profile* profile_;  // not owned

  scoped_ptr<GDataOperationRunner> runner_;

  // In-progress download requests.  Pointers are owned by this class.
  std::set<DownloadContactsRequest*> requests_;

  // If non-empty, URL that will be used to fetch the feed.  URLs contained
  // within the feed will also be modified to use the host and port from this
  // member.
  GURL feed_url_for_testing_;

  // Maximum number of photos we'll try to download at once (per
  // DownloadContacts() request).
  int max_simultaneous_photo_downloads_;

  // Callback that's invoked to rewrite photo URLs for tests.
  // This is needed for tests that serve static feed data from a host/port
  // that's only known at runtime.
  RewritePhotoUrlCallback rewrite_photo_url_callback_for_testing_;

  DISALLOW_COPY_AND_ASSIGN(GDataContactsService);
};

}  // namespace gdata

#endif  // CHROME_BROWSER_CHROMEOS_GDATA_GDATA_CONTACTS_SERVICE_H_
