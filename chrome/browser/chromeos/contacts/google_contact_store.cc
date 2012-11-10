// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/contacts/google_contact_store.h"

#include <algorithm>

#include "base/bind.h"
#include "base/file_path.h"
#include "base/logging.h"
#include "chrome/browser/chromeos/contacts/contact.pb.h"
#include "chrome/browser/chromeos/contacts/contact_database.h"
#include "chrome/browser/chromeos/contacts/contact_store_observer.h"
#include "chrome/browser/chromeos/gdata/gdata_contacts_service.h"
#include "chrome/browser/chromeos/gdata/gdata_system_service.h"
#include "chrome/browser/chromeos/gdata/gdata_util.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace contacts {

namespace {

// Name of the directory within the profile directory where the contact database
// is stored.
const FilePath::CharType kDatabaseDirectoryName[] =
    FILE_PATH_LITERAL("Google Contacts");

// We wait this long after the last update has completed successfully before
// updating again.
// TODO(derat): Decide what this should be.
const int kUpdateIntervalSec = 600;

// https://developers.google.com/google-apps/contacts/v3/index says that deleted
// contact (groups?) will only be returned for 30 days after deletion when the
// "showdeleted" parameter is set. If it's been longer than that since the last
// successful update, we do a full refresh to make sure that we haven't missed
// any deletions. Use 29 instead to make sure that we don't run afoul of
// daylight saving time shenanigans or minor skew in the system clock.
const int kForceFullUpdateDays = 29;

// When an update fails, we initially wait this many seconds before retrying.
// The delay increases exponentially in response to repeated failures.
const int kUpdateFailureInitialRetrySec = 5;

// Amount by which |update_delay_on_next_failure_| is multiplied on failure.
const int kUpdateFailureBackoffFactor = 2;

}  // namespace

GoogleContactStore::TestAPI::TestAPI(GoogleContactStore* store)
    : store_(store) {
  DCHECK(store);
}

GoogleContactStore::TestAPI::~TestAPI() {
  store_ = NULL;
}

void GoogleContactStore::TestAPI::SetDatabase(ContactDatabaseInterface* db) {
  store_->DestroyDatabase();
  store_->db_ = db;
}

void GoogleContactStore::TestAPI::SetGDataService(
    gdata::GDataContactsServiceInterface* service) {
  store_->gdata_service_for_testing_.reset(service);
}

void GoogleContactStore::TestAPI::DoUpdate() {
  store_->UpdateContacts();
}

GoogleContactStore::GoogleContactStore(Profile* profile)
    : profile_(profile),
      contacts_deleter_(&contacts_),
      db_(new ContactDatabase),
      update_delay_on_next_failure_(
          base::TimeDelta::FromSeconds(kUpdateFailureInitialRetrySec)),
      ALLOW_THIS_IN_INITIALIZER_LIST(weak_ptr_factory_(this)) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
}

GoogleContactStore::~GoogleContactStore() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  weak_ptr_factory_.InvalidateWeakPtrs();
  DestroyDatabase();
}

void GoogleContactStore::Init() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  FilePath db_path = profile_->GetPath().Append(kDatabaseDirectoryName);
  VLOG(1) << "Initializing contact database \"" << db_path.value() << "\" for "
          << profile_->GetProfileName();
  db_->Init(db_path,
            base::Bind(&GoogleContactStore::OnDatabaseInitialized,
                       weak_ptr_factory_.GetWeakPtr()));
}

void GoogleContactStore::AppendContacts(ContactPointers* contacts_out) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(contacts_out);
  for (ContactMap::const_iterator it = contacts_.begin();
       it != contacts_.end(); ++it) {
    if (!it->second->deleted())
      contacts_out->push_back(it->second);
  }
}

const Contact* GoogleContactStore::GetContactByProviderId(
    const std::string& provider_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  ContactMap::const_iterator it = contacts_.find(provider_id);
  return (it != contacts_.end() && !it->second->deleted()) ? it->second : NULL;
}

void GoogleContactStore::AddObserver(ContactStoreObserver* observer) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(observer);
  observers_.AddObserver(observer);
}

void GoogleContactStore::RemoveObserver(ContactStoreObserver* observer) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(observer);
  observers_.RemoveObserver(observer);
}

base::Time GoogleContactStore::GetCurrentTime() const {
  return !current_time_for_testing_.is_null() ?
         current_time_for_testing_ :
         base::Time::Now();
}

void GoogleContactStore::DestroyDatabase() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (db_) {
    db_->DestroyOnUIThread();
    db_ = NULL;
  }
}

void GoogleContactStore::UpdateContacts() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  base::Time min_update_time;
  base::TimeDelta time_since_last_update =
      last_successful_update_start_time_.is_null() ?
      base::TimeDelta() :
      GetCurrentTime() - last_successful_update_start_time_;

  if (!last_contact_update_time_.is_null() &&
      time_since_last_update <
      base::TimeDelta::FromDays(kForceFullUpdateDays)) {
    // TODO(derat): I'm adding one millisecond to the last update time here as I
    // don't want to re-download the same most-recently-updated contact each
    // time, but what happens if within the same millisecond, contact A is
    // updated, we do a sync, and then contact B is updated? I'm probably being
    // overly paranoid about this.
    min_update_time =
        last_contact_update_time_ + base::TimeDelta::FromMilliseconds(1);
  }
  if (min_update_time.is_null()) {
    VLOG(1) << "Downloading all contacts for " << profile_->GetProfileName();
  } else {
    VLOG(1) << "Downloading contacts updated since "
            << gdata::util::FormatTimeAsString(min_update_time) << " for "
            << profile_->GetProfileName();
  }

  gdata::GDataContactsServiceInterface* service =
      gdata_service_for_testing_.get() ?
      gdata_service_for_testing_.get() :
      gdata::GDataSystemServiceFactory::GetForProfile(profile_)->
          contacts_service();
  DCHECK(service);
  service->DownloadContacts(
      base::Bind(&GoogleContactStore::OnDownloadSuccess,
                 weak_ptr_factory_.GetWeakPtr(),
                 min_update_time.is_null(),
                 GetCurrentTime()),
      base::Bind(&GoogleContactStore::OnDownloadFailure,
                 weak_ptr_factory_.GetWeakPtr()),
      min_update_time);
}

void GoogleContactStore::ScheduleUpdate(bool last_update_was_successful) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  base::TimeDelta delay;
  if (last_update_was_successful) {
    delay = base::TimeDelta::FromSeconds(kUpdateIntervalSec);
    update_delay_on_next_failure_ =
        base::TimeDelta::FromSeconds(kUpdateFailureInitialRetrySec);
  } else {
    delay = update_delay_on_next_failure_;
    update_delay_on_next_failure_ = std::min(
        update_delay_on_next_failure_ * kUpdateFailureBackoffFactor,
        base::TimeDelta::FromSeconds(kUpdateIntervalSec));
  }
  VLOG(1) << "Scheduling update of " << profile_->GetProfileName()
          << " in " << delay.InSeconds() << " second(s)";
  update_timer_.Start(
      FROM_HERE, delay, this, &GoogleContactStore::UpdateContacts);
}

void GoogleContactStore::MergeContacts(
    bool is_full_update,
    scoped_ptr<ScopedVector<Contact> > updated_contacts) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (is_full_update) {
    STLDeleteValues(&contacts_);
    contacts_.clear();
  }

  for (ScopedVector<Contact>::iterator it = updated_contacts->begin();
       it != updated_contacts->end(); ++it) {
    Contact* contact = *it;
    VLOG(1) << "Updating " << contact->provider_id();
    ContactMap::iterator map_it = contacts_.find(contact->provider_id());
    if (map_it == contacts_.end()) {
      contacts_[contact->provider_id()] = contact;
    } else {
      delete map_it->second;
      map_it->second = contact;
    }
  }

  // Make sure that the Contact objects won't be destroyed when
  // |updated_contacts| is destroyed.
  size_t num_updated_contacts = updated_contacts->size();
  updated_contacts->weak_clear();

  if (is_full_update || num_updated_contacts > 0) {
    // Find the latest update time.
    last_contact_update_time_ = base::Time();
    for (ContactMap::const_iterator it = contacts_.begin();
         it != contacts_.end(); ++it) {
      const Contact* contact = it->second;
      base::Time update_time =
          base::Time::FromInternalValue(contact->update_time());

      if (!update_time.is_null() &&
          (last_contact_update_time_.is_null() ||
           last_contact_update_time_ < update_time)) {
        last_contact_update_time_ = update_time;
      }
    }
  }
  VLOG(1) << "Last contact update time is "
          << (last_contact_update_time_.is_null() ?
              std::string("null") :
              gdata::util::FormatTimeAsString(last_contact_update_time_));
}

void GoogleContactStore::OnDownloadSuccess(
    bool is_full_update,
    const base::Time& update_start_time,
    scoped_ptr<ScopedVector<Contact> > updated_contacts) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  VLOG(1) << "Got " << updated_contacts->size() << " contact(s) for "
          << profile_->GetProfileName();

  // Copy the pointers so we can update just these contacts in the database.
  scoped_ptr<ContactPointers> contacts_to_save_to_db(new ContactPointers);
  if (db_) {
    for (size_t i = 0; i < updated_contacts->size(); ++i)
      contacts_to_save_to_db->push_back((*updated_contacts)[i]);
  }
  bool got_updates = !updated_contacts->empty();

  MergeContacts(is_full_update, updated_contacts.Pass());
  last_successful_update_start_time_ = update_start_time;

  if (is_full_update || got_updates > 0) {
    FOR_EACH_OBSERVER(ContactStoreObserver,
                      observers_,
                      OnContactsUpdated(this));
  }

  if (db_) {
    VLOG(1) << "Saving " << contacts_to_save_to_db->size() << " contact(s) to "
            << "database as " << (is_full_update ? "full" : "partial")
            << " update";
    scoped_ptr<UpdateMetadata> metadata(new UpdateMetadata);
    metadata->set_last_update_start_time(update_start_time.ToInternalValue());
    db_->SaveContacts(
        contacts_to_save_to_db.Pass(),
        metadata.Pass(),
        is_full_update,
        base::Bind(&GoogleContactStore::OnDatabaseContactsSaved,
                   weak_ptr_factory_.GetWeakPtr()));

    // We'll schedule an update from OnDatabaseContactsSaved() after we're done
    // writing to the database -- we don't want to modify the contacts while
    // they're being used by the database.
  } else {
    ScheduleUpdate(true);
  }
}

void GoogleContactStore::OnDownloadFailure() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  LOG(WARNING) << "Contacts download failed for " << profile_->GetProfileName();
  ScheduleUpdate(false);
}

void GoogleContactStore::OnDatabaseInitialized(bool success) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (success) {
    VLOG(1) << "Contact database initialized for "
            << profile_->GetProfileName();
    db_->LoadContacts(base::Bind(&GoogleContactStore::OnDatabaseContactsLoaded,
                                 weak_ptr_factory_.GetWeakPtr()));
  } else {
    LOG(WARNING) << "Failed to initialize contact database for "
                 << profile_->GetProfileName();
    // Limp along as best as we can: throw away the database and do an update,
    // which will schedule further updates.
    DestroyDatabase();
    UpdateContacts();
  }
}

void GoogleContactStore::OnDatabaseContactsLoaded(
    bool success,
    scoped_ptr<ScopedVector<Contact> > contacts,
    scoped_ptr<UpdateMetadata> metadata) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (success) {
    VLOG(1) << "Loaded " << contacts->size() << " contact(s) from database";
    MergeContacts(true, contacts.Pass());
    last_successful_update_start_time_ =
        base::Time::FromInternalValue(metadata->last_update_start_time());

    if (!contacts_.empty()) {
      FOR_EACH_OBSERVER(ContactStoreObserver,
                        observers_,
                        OnContactsUpdated(this));
    }
  } else {
    LOG(WARNING) << "Failed to load contacts from database";
  }
  UpdateContacts();
}

void GoogleContactStore::OnDatabaseContactsSaved(bool success) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (!success)
    LOG(WARNING) << "Failed to save contacts to database";

  // We only update the database when we've successfully downloaded contacts, so
  // report success to ScheduleUpdate() even if the database update failed.
  ScheduleUpdate(true);
}

GoogleContactStoreFactory::GoogleContactStoreFactory() {
}

GoogleContactStoreFactory::~GoogleContactStoreFactory() {
}

bool GoogleContactStoreFactory::CanCreateContactStoreForProfile(
    Profile* profile) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(profile);
  return gdata::util::IsGDataAvailable(profile);
}

ContactStore* GoogleContactStoreFactory::CreateContactStore(Profile* profile) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(CanCreateContactStoreForProfile(profile));
  return new GoogleContactStore(profile);
}

}  // namespace contacts
