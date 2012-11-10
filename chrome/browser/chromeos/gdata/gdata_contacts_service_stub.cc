// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/gdata/gdata_contacts_service_stub.h"

#include "chrome/browser/chromeos/contacts/contact.pb.h"
#include "chrome/browser/chromeos/contacts/contact_test_util.h"
#include "chrome/browser/chromeos/gdata/gdata_util.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace gdata {

GDataContactsServiceStub::GDataContactsServiceStub()
    : download_should_succeed_(true) {
}

GDataContactsServiceStub::~GDataContactsServiceStub() {
}

void GDataContactsServiceStub::SetContacts(
    const contacts::ContactPointers& contacts,
    const base::Time& expected_min_update_time) {
  contacts::test::CopyContacts(contacts, &contacts_);
  expected_min_update_time_ = expected_min_update_time;
}

void GDataContactsServiceStub::Initialize() {
}

void GDataContactsServiceStub::DownloadContacts(
    SuccessCallback success_callback,
    FailureCallback failure_callback,
    const base::Time& min_update_time) {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (!download_should_succeed_) {
    failure_callback.Run();
    return;
  }

  if (min_update_time != expected_min_update_time_) {
    LOG(ERROR) << "Actual minimum update time ("
               << util::FormatTimeAsString(min_update_time) << ") "
               << "differed from expected ("
               << util::FormatTimeAsString(expected_min_update_time_)
               << "); not returning any contacts";
    failure_callback.Run();
    return;
  }

  scoped_ptr<ScopedVector<contacts::Contact> > contacts(
      new ScopedVector<contacts::Contact>());
  contacts::test::CopyContacts(contacts_, contacts.get());
  success_callback.Run(contacts.Pass());
}

}  // namespace contacts
