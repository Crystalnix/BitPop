// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/gdata/gdata_contacts_service.h"

#include <cstring>
#include <string>
#include <map>
#include <utility>

#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/memory/weak_ptr.h"
#include "base/stl_util.h"
#include "base/values.h"
#include "chrome/browser/chromeos/contacts/contact.pb.h"
#include "chrome/browser/chromeos/gdata/gdata_operation_registry.h"
#include "chrome/browser/chromeos/gdata/gdata_operation_runner.h"
#include "chrome/browser/chromeos/gdata/gdata_operations.h"
#include "chrome/browser/chromeos/gdata/gdata_params.h"
#include "chrome/browser/chromeos/gdata/gdata_util.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace gdata {

namespace {

// Maximum number of profile photos that we'll download at once.
const int kMaxSimultaneousPhotoDownloads = 10;

// Field in the top-level object containing the contacts feed.
const char kFeedField[] = "feed";

// Field in the contacts feed containing a list of category information, along
// with fields within the dictionaries contained in the list and expected
// values.
const char kCategoryField[] = "category";
const char kCategorySchemeField[] = "scheme";
const char kCategorySchemeValue[] = "http://schemas.google.com/g/2005#kind";
const char kCategoryTermField[] = "term";
const char kCategoryTermValue[] =
    "http://schemas.google.com/contact/2008#contact";

// Field in the contacts feed containing a list of contact entries.
const char kEntryField[] = "entry";

// Top-level fields in contact entries.
const char kIdField[] = "id.$t";
const char kDeletedField[] = "gd$deleted";
const char kFullNameField[] = "gd$name.gd$fullName.$t";
const char kGivenNameField[] = "gd$name.gd$givenName.$t";
const char kAdditionalNameField[] = "gd$name.gd$additionalName.$t";
const char kFamilyNameField[] = "gd$name.gd$familyName.$t";
const char kNamePrefixField[] = "gd$name.gd$namePrefix.$t";
const char kNameSuffixField[] = "gd$name.gd$nameSuffix.$t";
const char kEmailField[] = "gd$email";
const char kPhoneField[] = "gd$phoneNumber";
const char kPostalAddressField[] = "gd$structuredPostalAddress";
const char kInstantMessagingField[] = "gd$im";
const char kLinkField[] = "link";
const char kUpdatedField[] = "updated.$t";

// Fields in entries in the |kEmailField| list.
const char kEmailAddressField[] = "address";

// Fields in entries in the |kPhoneField| list.
const char kPhoneNumberField[] = "$t";

// Fields in entries in the |kPostalAddressField| list.
const char kPostalAddressFormattedField[] = "gd$formattedAddress.$t";

// Fields in entries in the |kInstantMessagingField| list.
const char kInstantMessagingAddressField[] = "address";
const char kInstantMessagingProtocolField[] = "protocol";
const char kInstantMessagingProtocolAimValue[] =
    "http://schemas.google.com/g/2005#AIM";
const char kInstantMessagingProtocolMsnValue[] =
    "http://schemas.google.com/g/2005#MSN";
const char kInstantMessagingProtocolYahooValue[] =
    "http://schemas.google.com/g/2005#YAHOO";
const char kInstantMessagingProtocolSkypeValue[] =
    "http://schemas.google.com/g/2005#SKYPE";
const char kInstantMessagingProtocolQqValue[] =
    "http://schemas.google.com/g/2005#QQ";
const char kInstantMessagingProtocolGoogleTalkValue[] =
    "http://schemas.google.com/g/2005#GOOGLE_TALK";
const char kInstantMessagingProtocolIcqValue[] =
    "http://schemas.google.com/g/2005#ICQ";
const char kInstantMessagingProtocolJabberValue[] =
    "http://schemas.google.com/g/2005#JABBER";

// Generic fields shared between address-like items (email, postal, etc.).
const char kAddressPrimaryField[] = "primary";
const char kAddressPrimaryTrueValue[] = "true";
const char kAddressRelField[] = "rel";
const char kAddressRelHomeValue[] = "http://schemas.google.com/g/2005#home";
const char kAddressRelWorkValue[] = "http://schemas.google.com/g/2005#work";
const char kAddressRelMobileValue[] = "http://schemas.google.com/g/2005#mobile";
const char kAddressLabelField[] = "label";

// Fields in entries in the |kLinkField| list.
const char kLinkHrefField[] = "href";
const char kLinkRelField[] = "rel";
const char kLinkETagField[] = "gd$etag";
const char kLinkRelPhotoValue[] =
    "http://schemas.google.com/contacts/2008/rel#photo";

// Returns a string containing a pretty-printed JSON representation of |value|.
std::string PrettyPrintValue(const base::Value& value) {
  std::string out;
  base::JSONWriter::WriteWithOptions(
      &value, base::JSONWriter::OPTIONS_PRETTY_PRINT, &out);
  return out;
}

// Returns whether an address is primary, given a dictionary representing a
// single address.
bool IsAddressPrimary(const DictionaryValue& address_dict) {
  std::string primary;
  address_dict.GetString(kAddressPrimaryField, &primary);
  return primary == kAddressPrimaryTrueValue;
}

// Initializes an AddressType message given a dictionary representing a single
// address.
void InitAddressType(const DictionaryValue& address_dict,
                     contacts::Contact_AddressType* type) {
  DCHECK(type);
  type->Clear();

  std::string rel;
  address_dict.GetString(kAddressRelField, &rel);
  if (rel == kAddressRelHomeValue)
    type->set_relation(contacts::Contact_AddressType_Relation_HOME);
  else if (rel == kAddressRelWorkValue)
    type->set_relation(contacts::Contact_AddressType_Relation_WORK);
  else if (rel == kAddressRelMobileValue)
    type->set_relation(contacts::Contact_AddressType_Relation_MOBILE);
  else
    type->set_relation(contacts::Contact_AddressType_Relation_OTHER);

  address_dict.GetString(kAddressLabelField, type->mutable_label());
}

// Maps the protocol from a dictionary representing a contact's IM address to a
// contacts::Contact_InstantMessagingAddress_Protocol value.
contacts::Contact_InstantMessagingAddress_Protocol
GetInstantMessagingProtocol(const DictionaryValue& im_dict) {
  std::string protocol;
  im_dict.GetString(kInstantMessagingProtocolField, &protocol);
  if (protocol == kInstantMessagingProtocolAimValue)
    return contacts::Contact_InstantMessagingAddress_Protocol_AIM;
  else if (protocol == kInstantMessagingProtocolMsnValue)
    return contacts::Contact_InstantMessagingAddress_Protocol_MSN;
  else if (protocol == kInstantMessagingProtocolYahooValue)
    return contacts::Contact_InstantMessagingAddress_Protocol_YAHOO;
  else if (protocol == kInstantMessagingProtocolSkypeValue)
    return contacts::Contact_InstantMessagingAddress_Protocol_SKYPE;
  else if (protocol == kInstantMessagingProtocolQqValue)
    return contacts::Contact_InstantMessagingAddress_Protocol_QQ;
  else if (protocol == kInstantMessagingProtocolGoogleTalkValue)
    return contacts::Contact_InstantMessagingAddress_Protocol_GOOGLE_TALK;
  else if (protocol == kInstantMessagingProtocolIcqValue)
    return contacts::Contact_InstantMessagingAddress_Protocol_ICQ;
  else if (protocol == kInstantMessagingProtocolJabberValue)
    return contacts::Contact_InstantMessagingAddress_Protocol_JABBER;
  else
    return contacts::Contact_InstantMessagingAddress_Protocol_OTHER;
}

// Gets the photo URL from a contact's dictionary (within the "entry" list).
// Returns an empty string if no photo was found.
std::string GetPhotoUrl(const DictionaryValue& dict) {
  const ListValue* link_list = NULL;
  if (!dict.GetList(kLinkField, &link_list))
    return std::string();

  for (size_t i = 0; i < link_list->GetSize(); ++i) {
    const DictionaryValue* link_dict = NULL;
    if (!link_list->GetDictionary(i, &link_dict))
      continue;

    std::string rel;
    if (!link_dict->GetString(kLinkRelField, &rel))
      continue;
    if (rel != kLinkRelPhotoValue)
      continue;

    // From https://goo.gl/7T6Od: "If a contact does not have a photo, then the
    // photo link element has no gd:etag attribute."
    std::string etag;
    if (!link_dict->GetString(kLinkETagField, &etag))
      continue;

    std::string url;
    if (link_dict->GetString(kLinkHrefField, &url))
      return url;
  }
  return std::string();
}

// Fills a Contact's fields using an entry from a GData feed.
bool FillContactFromDictionary(const base::DictionaryValue& dict,
                               contacts::Contact* contact) {
  DCHECK(contact);
  contact->Clear();

  if (!dict.GetString(kIdField, contact->mutable_provider_id()))
    return false;

  std::string updated;
  if (dict.GetString(kUpdatedField, &updated)) {
    base::Time update_time;
    if (!util::GetTimeFromString(updated, &update_time)) {
      LOG(WARNING) << "Unable to parse time \"" << updated << "\"";
      return false;
    }
    contact->set_update_time(update_time.ToInternalValue());
  }

  const base::Value* deleted_value = NULL;
  contact->set_deleted(dict.Get(kDeletedField, &deleted_value));
  if (contact->deleted())
    return true;

  dict.GetString(kFullNameField, contact->mutable_full_name());
  dict.GetString(kGivenNameField, contact->mutable_given_name());
  dict.GetString(kAdditionalNameField, contact->mutable_additional_name());
  dict.GetString(kFamilyNameField, contact->mutable_family_name());
  dict.GetString(kNamePrefixField, contact->mutable_name_prefix());
  dict.GetString(kNameSuffixField, contact->mutable_name_suffix());

  const ListValue* email_list = NULL;
  if (dict.GetList(kEmailField, &email_list)) {
    for (size_t i = 0; i < email_list->GetSize(); ++i) {
      const DictionaryValue* email_dict = NULL;
      if (!email_list->GetDictionary(i, &email_dict))
        return false;

      contacts::Contact_EmailAddress* email = contact->add_email_addresses();
      if (!email_dict->GetString(kEmailAddressField, email->mutable_address()))
        return false;
      email->set_primary(IsAddressPrimary(*email_dict));
      InitAddressType(*email_dict, email->mutable_type());
    }
  }

  const ListValue* phone_list = NULL;
  if (dict.GetList(kPhoneField, &phone_list)) {
    for (size_t i = 0; i < phone_list->GetSize(); ++i) {
      const DictionaryValue* phone_dict = NULL;
      if (!phone_list->GetDictionary(i, &phone_dict))
        return false;

      contacts::Contact_PhoneNumber* phone = contact->add_phone_numbers();
      if (!phone_dict->GetString(kPhoneNumberField, phone->mutable_number()))
        return false;
      phone->set_primary(IsAddressPrimary(*phone_dict));
      InitAddressType(*phone_dict, phone->mutable_type());
    }
  }

  const ListValue* address_list = NULL;
  if (dict.GetList(kPostalAddressField, &address_list)) {
    for (size_t i = 0; i < address_list->GetSize(); ++i) {
      const DictionaryValue* address_dict = NULL;
      if (!address_list->GetDictionary(i, &address_dict))
        return false;

      contacts::Contact_PostalAddress* address =
          contact->add_postal_addresses();
      if (!address_dict->GetString(kPostalAddressFormattedField,
                                   address->mutable_address())) {
        return false;
      }
      address->set_primary(IsAddressPrimary(*address_dict));
      InitAddressType(*address_dict, address->mutable_type());
    }
  }

  const ListValue* im_list = NULL;
  if (dict.GetList(kInstantMessagingField, &im_list)) {
    for (size_t i = 0; i < im_list->GetSize(); ++i) {
      const DictionaryValue* im_dict = NULL;
      if (!im_list->GetDictionary(i, &im_dict))
        return false;

      contacts::Contact_InstantMessagingAddress* im =
          contact->add_instant_messaging_addresses();
      if (!im_dict->GetString(kInstantMessagingAddressField,
                              im->mutable_address())) {
        return false;
      }
      im->set_primary(IsAddressPrimary(*im_dict));
      InitAddressType(*im_dict, im->mutable_type());
      im->set_protocol(GetInstantMessagingProtocol(*im_dict));
    }
  }

  return true;
}

}  // namespace

// This class handles a single request to download all of a user's contacts.
//
// First, the contacts feed is downloaded via GetContactsOperation and parsed.
// Individual contacts::Contact objects are created using the data from the
// feed.  Next, GetContactPhotoOperations are created and used to start
// downloading contacts' photos in parallel.  When all photos have been
// downloaded, the contacts are passed to the passed-in callback.
class GDataContactsService::DownloadContactsRequest
    : public base::SupportsWeakPtr<DownloadContactsRequest> {
 public:
  DownloadContactsRequest(GDataContactsService* service,
                          GDataOperationRunner* runner,
                          SuccessCallback success_callback,
                          FailureCallback failure_callback,
                          const base::Time& min_update_time,
                          int max_simultaneous_photo_downloads)
      : service_(service),
        runner_(runner),
        success_callback_(success_callback),
        failure_callback_(failure_callback),
        min_update_time_(min_update_time),
        contacts_(new ScopedVector<contacts::Contact>),
        max_simultaneous_photo_downloads_(max_simultaneous_photo_downloads),
        num_in_progress_photo_downloads_(0),
        photo_download_failed_(false) {
    DCHECK(service_);
    DCHECK(runner_);
  }

  ~DownloadContactsRequest() {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    service_ = NULL;
    runner_ = NULL;
  }

  // Issues the initial request to download the contact feed.
  void Run() {
    GetContactsOperation* operation =
        new GetContactsOperation(
            runner_->operation_registry(),
            min_update_time_,
            base::Bind(&DownloadContactsRequest::HandleFeedData,
                       base::Unretained(this)));
    if (!service_->feed_url_for_testing_.is_empty())
      operation->set_feed_url_for_testing(service_->feed_url_for_testing_);

    runner_->StartOperationWithRetry(operation);
  }

 private:
  // Callback for GetContactsOperation calls.
  void HandleFeedData(GDataErrorCode error,
                      scoped_ptr<base::Value> feed_data) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    if (error != HTTP_SUCCESS) {
      LOG(WARNING) << "Got error " << error << " while downloading contacts";
      failure_callback_.Run();
      service_->OnRequestComplete(this);
      return;
    }

    VLOG(2) << "Got feed data:\n" << PrettyPrintValue(*(feed_data.get()));
    if (!ProcessFeedData(*feed_data.get())) {
      LOG(WARNING) << "Unable to process feed data";
      failure_callback_.Run();
      service_->OnRequestComplete(this);
      return;
    }

    CheckCompletion();
  }

  // Processes the raw contacts feed from |feed_data| and fills |contacts_|.
  // Returns true on success.
  bool ProcessFeedData(const base::Value& feed_data) {
    const DictionaryValue* toplevel_dict = NULL;
    if (!feed_data.GetAsDictionary(&toplevel_dict)) {
      LOG(WARNING) << "Top-level object is not a dictionary";
      return false;
    }

    const DictionaryValue* feed_dict = NULL;
    if (!toplevel_dict->GetDictionary(kFeedField, &feed_dict)) {
      LOG(WARNING) << "Feed dictionary missing";
      return false;
    }

    // Check the category field to confirm that this is actually a contact feed.
    const ListValue* category_list = NULL;
    if (!feed_dict->GetList(kCategoryField, &category_list)) {
      LOG(WARNING) << "Category list missing";
      return false;
    }
    const DictionaryValue* category_dict = NULL;
    if (!category_list->GetSize() == 1 ||
        !category_list->GetDictionary(0, &category_dict)) {
      LOG(WARNING) << "Unable to get dictionary from category list of size "
                   << category_list->GetSize();
      return false;
    }
    std::string category_scheme, category_term;
    if (!category_dict->GetString(kCategorySchemeField, &category_scheme) ||
        !category_dict->GetString(kCategoryTermField, &category_term) ||
        category_scheme != kCategorySchemeValue ||
        category_term != kCategoryTermValue) {
      LOG(WARNING) << "Unexpected category (scheme was \"" << category_scheme
                   << "\", term was \"" << category_term << "\")";
      return false;
    }

    // A missing entry list means no entries (maybe we're doing an incremental
    // update and nothing has changed).
    const ListValue* entry_list = NULL;
    if (!feed_dict->GetList(kEntryField, &entry_list))
      return true;

    contacts_needing_photo_downloads_.reserve(entry_list->GetSize());

    for (ListValue::const_iterator entry_it = entry_list->begin();
         entry_it != entry_list->end(); ++entry_it) {
      const size_t index = (entry_it - entry_list->begin());
      const DictionaryValue* contact_dict = NULL;
      if (!(*entry_it)->GetAsDictionary(&contact_dict)) {
        LOG(WARNING) << "Entry " << index << " isn't a dictionary";
        return false;
      }

      scoped_ptr<contacts::Contact> contact(new contacts::Contact);
      if (!FillContactFromDictionary(*contact_dict, contact.get())) {
        LOG(WARNING) << "Unable to fill entry " << index;
        return false;
      }

      VLOG(1) << "Got contact " << index << ":"
              << " id=" << contact->provider_id()
              << " full_name=\"" << contact->full_name() << "\""
              << " update_time=" << contact->update_time();

      std::string photo_url = GetPhotoUrl(*contact_dict);
      if (!photo_url.empty()) {
        if (!service_->rewrite_photo_url_callback_for_testing_.is_null()) {
          photo_url =
              service_->rewrite_photo_url_callback_for_testing_.Run(photo_url);
        }
        contact_photo_urls_[contact.get()] = photo_url;
        contacts_needing_photo_downloads_.push_back(contact.get());
      }

      contacts_->push_back(contact.release());
    }

    return true;
  }

  // If we're done downloading photos, invokes a callback and deletes |this|.
  // Otherwise, starts one or more downloads of URLs from
  // |contacts_needing_photo_downloads_|.
  void CheckCompletion() {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    if (contacts_needing_photo_downloads_.empty() &&
        num_in_progress_photo_downloads_ == 0) {
      VLOG(1) << "Done downloading photos; invoking callback";
      if (photo_download_failed_)
        failure_callback_.Run();
      else
        success_callback_.Run(contacts_.Pass());
      service_->OnRequestComplete(this);
      return;
    }

    while (!contacts_needing_photo_downloads_.empty() &&
           (num_in_progress_photo_downloads_ <
            max_simultaneous_photo_downloads_)) {
      contacts::Contact* contact = contacts_needing_photo_downloads_.back();
      contacts_needing_photo_downloads_.pop_back();
      DCHECK(contact_photo_urls_.count(contact));
      std::string url = contact_photo_urls_[contact];

      VLOG(1) << "Starting download of photo " << url << " for "
              << contact->provider_id();
      runner_->StartOperationWithRetry(
          new GetContactPhotoOperation(
              runner_->operation_registry(),
              GURL(url),
              base::Bind(&DownloadContactsRequest::HandlePhotoData,
                         AsWeakPtr(), contact)));
      num_in_progress_photo_downloads_++;
    }
  }

  // Callback for GetContactPhotoOperation calls.  Updates the associated
  // Contact and checks for completion.
  void HandlePhotoData(contacts::Contact* contact,
                       GDataErrorCode error,
                       scoped_ptr<std::string> download_data) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    VLOG(1) << "Got photo data for " << contact->provider_id()
            << " (error=" << error << " size=" << download_data->size() << ")";
    num_in_progress_photo_downloads_--;

    if (error != HTTP_SUCCESS) {
      LOG(WARNING) << "Got error " << error << " while downloading photo "
                   << "for " << contact->provider_id();
      // TODO(derat): Retry several times for temporary failures?
      photo_download_failed_ = true;
      // Make sure we don't start any more downloads.
      contacts_needing_photo_downloads_.clear();
      CheckCompletion();
      return;
    }

    contact->set_raw_untrusted_photo(*download_data);
    CheckCompletion();
  }

 private:
  typedef std::map<contacts::Contact*, std::string> ContactPhotoUrls;

  GDataContactsService* service_;  // not owned
  GDataOperationRunner* runner_;  // not owned

  SuccessCallback success_callback_;
  FailureCallback failure_callback_;

  base::Time min_update_time_;

  scoped_ptr<ScopedVector<contacts::Contact> > contacts_;

  // Map from a contact to the URL at which its photo is located.
  // Contacts without photos do not appear in this map.
  ContactPhotoUrls contact_photo_urls_;

  // Contacts that have photos that we still need to start downloading.
  // When we start a download, the contact is removed from this list.
  std::vector<contacts::Contact*> contacts_needing_photo_downloads_;

  // Maximum number of photos we'll try to download at once.
  int max_simultaneous_photo_downloads_;

  // Number of in-progress photo downloads.
  int num_in_progress_photo_downloads_;

  // Did we encounter a fatal error while downloading a photo?
  bool photo_download_failed_;

  DISALLOW_COPY_AND_ASSIGN(DownloadContactsRequest);
};

GDataContactsService::GDataContactsService(Profile* profile)
    : runner_(new GDataOperationRunner(profile)),
      max_simultaneous_photo_downloads_(kMaxSimultaneousPhotoDownloads) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
}

GDataContactsService::~GDataContactsService() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  runner_->CancelAll();
  STLDeleteContainerPointers(requests_.begin(), requests_.end());
  requests_.clear();
}

GDataAuthService* GDataContactsService::auth_service_for_testing() {
  return runner_->auth_service();
}

void GDataContactsService::Initialize() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  runner_->Initialize();
}

void GDataContactsService::DownloadContacts(SuccessCallback success_callback,
                                            FailureCallback failure_callback,
                                            const base::Time& min_update_time) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DownloadContactsRequest* request =
      new DownloadContactsRequest(this,
                                  runner_.get(),
                                  success_callback,
                                  failure_callback,
                                  min_update_time,
                                  max_simultaneous_photo_downloads_);
  VLOG(1) << "Starting contacts download with request " << request;
  requests_.insert(request);
  request->Run();
}

void GDataContactsService::OnRequestComplete(DownloadContactsRequest* request) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(request);
  VLOG(1) << "Download request " << request << " complete";
  requests_.erase(request);
  delete request;
}

}  // namespace contacts
