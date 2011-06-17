// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/autofill/personal_data_manager.h"

#include <algorithm>
#include <iterator>

#include "base/logging.h"
#include "base/string_number_conversions.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/autofill/autofill-inl.h"
#include "chrome/browser/autofill/autofill_field.h"
#include "chrome/browser/autofill/autofill_metrics.h"
#include "chrome/browser/autofill/form_structure.h"
#include "chrome/browser/autofill/phone_number.h"
#include "chrome/browser/autofill/select_control_handler.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "chrome/browser/webdata/web_data_service.h"
#include "chrome/common/pref_names.h"
#include "content/browser/browser_thread.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebRegularExpression.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebString.h"

namespace {

// The minimum number of fields that must contain relevant user data before
// Autofill will attempt to import the data into a credit card.
const int kMinCreditCardImportSize = 2;

template<typename T>
class FormGroupMatchesByGUIDFunctor {
 public:
  explicit FormGroupMatchesByGUIDFunctor(const std::string& guid)
      : guid_(guid) {
  }

  bool operator()(const T& form_group) {
    return form_group.guid() == guid_;
  }

  bool operator()(const T* form_group) {
    return form_group->guid() == guid_;
  }

 private:
  std::string guid_;
};

template<typename T, typename C>
bool FindByGUID(const C& container, const std::string& guid) {
  return std::find_if(
      container.begin(),
      container.end(),
      FormGroupMatchesByGUIDFunctor<T>(guid)) != container.end();
}

template<typename T>
class DereferenceFunctor {
 public:
  template<typename T_Iterator>
  const T& operator()(const T_Iterator& iterator) {
    return *iterator;
  }
};

template<typename T>
T* address_of(T& v) {
  return &v;
}

bool IsValidEmail(const string16& value) {
  // This regex is more permissive than the official rfc2822 spec on the
  // subject, but it does reject obvious non-email addresses.
  const string16 kEmailPattern =
      ASCIIToUTF16("^[^@]+@[^@]+\\.[a-z]{2,6}$");
  WebKit::WebRegularExpression re(WebKit::WebString(kEmailPattern),
                                  WebKit::WebTextCaseInsensitive);
  return re.match(WebKit::WebString(StringToLowerASCII(value))) != -1;
}

// Valid for US zip codes only.
bool IsValidZip(const string16& value) {
  // Basic US zip code matching.
  const string16 kZipPattern = ASCIIToUTF16("^\\d{5}(-\\d{4})?$");
  WebKit::WebRegularExpression re(WebKit::WebString(kZipPattern),
                                  WebKit::WebTextCaseInsensitive);
  return re.match(WebKit::WebString(StringToLowerASCII(value))) != -1;
}

// Returns true if minimum requirements for import of a given |profile| have
// been met.  An address submitted via a form must have at least these fields
// filled.  No verification of validity of the contents is preformed.  This is
// and existence check only.
bool IsMinimumAddress(const AutofillProfile& profile) {
  return !profile.GetInfo(ADDRESS_HOME_LINE1).empty() &&
         !profile.GetInfo(ADDRESS_HOME_CITY).empty() &&
         !profile.GetInfo(ADDRESS_HOME_STATE).empty() &&
         !profile.GetInfo(ADDRESS_HOME_ZIP).empty();
}

}  // namespace

PersonalDataManager::~PersonalDataManager() {
  CancelPendingQuery(&pending_profiles_query_);
  CancelPendingQuery(&pending_creditcards_query_);
}

void PersonalDataManager::OnWebDataServiceRequestDone(
    WebDataService::Handle h,
    const WDTypedResult* result) {
  DCHECK(pending_profiles_query_ || pending_creditcards_query_);

  if (!result) {
    // Error from the web database.
    if (h == pending_creditcards_query_)
      pending_creditcards_query_ = 0;
    else if (h == pending_profiles_query_)
      pending_profiles_query_ = 0;
    return;
  }

  DCHECK(result->GetType() == AUTOFILL_PROFILES_RESULT ||
         result->GetType() == AUTOFILL_CREDITCARDS_RESULT);

  switch (result->GetType()) {
    case AUTOFILL_PROFILES_RESULT:
      ReceiveLoadedProfiles(h, result);
      break;
    case AUTOFILL_CREDITCARDS_RESULT:
      ReceiveLoadedCreditCards(h, result);
      break;
    default:
      NOTREACHED();
  }

  // If both requests have responded, then all personal data is loaded.
  if (pending_profiles_query_ == 0 && pending_creditcards_query_ == 0) {
    is_data_loaded_ = true;
    std::vector<AutofillProfile*> profile_pointers(web_profiles_.size());
    std::copy(web_profiles_.begin(), web_profiles_.end(),
              profile_pointers.begin());
    AutofillProfile::AdjustInferredLabels(&profile_pointers);
    FOR_EACH_OBSERVER(Observer, observers_, OnPersonalDataLoaded());
  }
}

/////////////////////////////////////////////////////////////////////////////
// PersonalDataManager,
// views::ButtonListener implementations
void PersonalDataManager::SetObserver(PersonalDataManager::Observer* observer) {
  // TODO: RemoveObserver is for compatibility with old code, it should be
  // nuked.
  observers_.RemoveObserver(observer);
  observers_.AddObserver(observer);
}

void PersonalDataManager::RemoveObserver(
    PersonalDataManager::Observer* observer) {
  observers_.RemoveObserver(observer);
}

// The |PersonalDataManager| is set up as a listener of the sync service in
// |EmptyMigrationTrash| in the case where sync is not yet ready to receive
// changes.  This method, |OnStateChange| acts as a deferred call to
// |EmptyMigrationTrash| once the sync service becomes available.
void PersonalDataManager::OnStateChanged() {
  if (!profile_ || profile_->IsOffTheRecord())
    return;

  WebDataService* web_data_service =
      profile_->GetWebDataService(Profile::EXPLICIT_ACCESS);
  if (!web_data_service) {
    NOTREACHED();
    return;
  }

  ProfileSyncService* sync_service = profile_->GetProfileSyncService();
  if (!sync_service)
    return;

  if (sync_service->ShouldPushChanges()) {
    web_data_service->EmptyMigrationTrash(true);
    sync_service->RemoveObserver(this);
  }
}

bool PersonalDataManager::ImportFormData(
    const std::vector<const FormStructure*>& form_structures,
    const CreditCard** imported_credit_card) {
  scoped_ptr<AutofillProfile> imported_profile(new AutofillProfile);
  scoped_ptr<CreditCard> local_imported_credit_card(new CreditCard);

  // Parse the form and construct a profile based on the information that is
  // possible to import.
  int importable_credit_card_fields = 0;
  std::vector<const FormStructure*>::const_iterator iter;

  // Detect and discard forms with multiple fields of the same type.
  std::set<AutofillFieldType> types_seen;

  for (iter = form_structures.begin(); iter != form_structures.end(); ++iter) {
    const FormStructure* form = *iter;
    for (size_t i = 0; i < form->field_count(); ++i) {
      const AutofillField* field = form->field(i);
      string16 value = CollapseWhitespace(field->value, false);

      // If we don't know the type of the field, or the user hasn't entered any
      // information into the field, then skip it.
      if (!field->IsFieldFillable() || value.empty())
        continue;

      AutofillFieldType field_type = field->type();
      FieldTypeGroup group(AutofillType(field_type).group());

      // Abandon the import if two fields of the same type are encountered.
      // This indicates ambiguous data or miscategorization of types.
      // Make an exception for PHONE_HOME_NUMBER however as both prefix and
      // suffix are stored against this type.
      if (types_seen.count(field_type) &&
          field_type != PHONE_HOME_NUMBER  &&
          field_type != PHONE_FAX_NUMBER) {
        imported_profile.reset();
        local_imported_credit_card.reset();
        break;
      } else {
        types_seen.insert(field_type);
      }

      if (group == AutofillType::CREDIT_CARD) {
        // If the user has a password set, we have no way of setting credit
        // card numbers.
        if (!HasPassword()) {
          if (LowerCaseEqualsASCII(field->form_control_type, "month")) {
            DCHECK_EQ(CREDIT_CARD_EXP_MONTH, field_type);
            local_imported_credit_card->SetInfoForMonthInputType(value);
          } else {
            if (field_type == CREDIT_CARD_NUMBER) {
              // Clean up any imported credit card numbers.
              value = CreditCard::StripSeparators(value);
            }
            local_imported_credit_card->SetInfo(field_type, value);
          }
          ++importable_credit_card_fields;
        }
      } else {
        // In the case of a phone number, if the whole phone number was entered
        // into a single field, then parse it and set the sub components.
        if (AutofillType(field_type).subgroup() ==
                AutofillType::PHONE_WHOLE_NUMBER) {
          string16 number;
          string16 city_code;
          string16 country_code;
          PhoneNumber::ParsePhoneNumber(value,
                                        &number,
                                        &city_code,
                                        &country_code);
          if (number.empty())
            continue;

          if (group == AutofillType::PHONE_HOME) {
            imported_profile->SetInfo(PHONE_HOME_COUNTRY_CODE, country_code);
            imported_profile->SetInfo(PHONE_HOME_CITY_CODE, city_code);
            imported_profile->SetInfo(PHONE_HOME_NUMBER, number);
          } else if (group == AutofillType::PHONE_FAX) {
            imported_profile->SetInfo(PHONE_FAX_COUNTRY_CODE, country_code);
            imported_profile->SetInfo(PHONE_FAX_CITY_CODE, city_code);
            imported_profile->SetInfo(PHONE_FAX_NUMBER, number);
          }

          continue;
        }

        // Phone and fax numbers can be split across multiple fields, so we
        // might have already stored the prefix, and now be at the suffix.
        // If so, combine them to form the full number.
        if (group == AutofillType::PHONE_HOME ||
            group == AutofillType::PHONE_FAX) {
          AutofillFieldType number_type = PHONE_HOME_NUMBER;
          if (group == AutofillType::PHONE_FAX)
            number_type = PHONE_FAX_NUMBER;

          string16 stored_number = imported_profile->GetInfo(number_type);
          if (stored_number.size() ==
                  static_cast<size_t>(PhoneNumber::kPrefixLength) &&
              value.size() == static_cast<size_t>(PhoneNumber::kSuffixLength)) {
            value = stored_number + value;
          }
        }

        imported_profile->SetInfo(field_type, value);

        // Reject profiles with invalid country information.
        if (field_type == ADDRESS_HOME_COUNTRY &&
            !value.empty() && imported_profile->CountryCode().empty()) {
          imported_profile.reset();
          break;
        }
      }
    }
  }

  // Reject the profile if minimum address and validation requirements are not
  // met.
  if (imported_profile.get() && !IsValidLearnableProfile(*imported_profile))
    imported_profile.reset();

  // Reject the credit card if we did not detect enough filled credit card
  // fields or if the credit card number does not seem to be valid.
  if (local_imported_credit_card.get() &&
      (importable_credit_card_fields < kMinCreditCardImportSize ||
       !CreditCard::IsValidCreditCardNumber(
           local_imported_credit_card->GetInfo(CREDIT_CARD_NUMBER)))) {
    local_imported_credit_card.reset();
  }

  // Don't import if we already have this info.
  if (local_imported_credit_card.get()) {
    for (std::vector<CreditCard*>::const_iterator iter = credit_cards_.begin();
         iter != credit_cards_.end();
         ++iter) {
      if (local_imported_credit_card->IsSubsetOf(**iter)) {
        local_imported_credit_card.reset();
        break;
      }
    }
  }

  if (imported_profile.get()) {
    // We always save imported profiles.
    SaveImportedProfile(*imported_profile);
  }
  *imported_credit_card = local_imported_credit_card.release();

  return imported_profile.get() || *imported_credit_card;
}

void PersonalDataManager::SetProfiles(std::vector<AutofillProfile>* profiles) {
  if (profile_->IsOffTheRecord())
    return;

  // Remove empty profiles from input.
  profiles->erase(
      std::remove_if(profiles->begin(), profiles->end(),
                     std::mem_fun_ref(&AutofillProfile::IsEmpty)),
      profiles->end());

  // Ensure that profile labels are up to date.  Currently, sync relies on
  // labels to identify a profile.
  // TODO(dhollowa): We need to deprecate labels and update the way sync
  // identifies profiles.
  std::vector<AutofillProfile*> profile_pointers(profiles->size());
  std::transform(profiles->begin(), profiles->end(), profile_pointers.begin(),
      address_of<AutofillProfile>);
  AutofillProfile::AdjustInferredLabels(&profile_pointers);

  WebDataService* wds = profile_->GetWebDataService(Profile::EXPLICIT_ACCESS);
  if (!wds)
    return;

  // Any profiles that are not in the new profile list should be removed from
  // the web database.
  for (std::vector<AutofillProfile*>::const_iterator iter =
           web_profiles_.begin();
       iter != web_profiles_.end(); ++iter) {
    if (!FindByGUID<AutofillProfile>(*profiles, (*iter)->guid()))
      wds->RemoveAutofillProfile((*iter)->guid());
  }

  // Update the web database with the existing profiles.
  for (std::vector<AutofillProfile>::iterator iter = profiles->begin();
       iter != profiles->end(); ++iter) {
    if (FindByGUID<AutofillProfile>(web_profiles_, iter->guid()))
      wds->UpdateAutofillProfile(*iter);
  }

  // Add the new profiles to the web database.  Don't add a duplicate.
  for (std::vector<AutofillProfile>::iterator iter = profiles->begin();
       iter != profiles->end(); ++iter) {
    if (!FindByGUID<AutofillProfile>(web_profiles_, iter->guid()) &&
        !FindByContents(web_profiles_, *iter))
      wds->AddAutofillProfile(*iter);
  }

  // Copy in the new profiles.
  web_profiles_.reset();
  for (std::vector<AutofillProfile>::iterator iter = profiles->begin();
       iter != profiles->end(); ++iter) {
    web_profiles_.push_back(new AutofillProfile(*iter));
  }

  // Read our writes to ensure consistency with the database.
  Refresh();

  FOR_EACH_OBSERVER(Observer, observers_, OnPersonalDataChanged());
}

void PersonalDataManager::SetCreditCards(
    std::vector<CreditCard>* credit_cards) {
  if (profile_->IsOffTheRecord())
    return;

  // Remove empty credit cards from input.
  credit_cards->erase(
      std::remove_if(
          credit_cards->begin(), credit_cards->end(),
          std::mem_fun_ref(&CreditCard::IsEmpty)),
      credit_cards->end());

  WebDataService* wds = profile_->GetWebDataService(Profile::EXPLICIT_ACCESS);
  if (!wds)
    return;

  // Any credit cards that are not in the new credit card list should be
  // removed.
  for (std::vector<CreditCard*>::const_iterator iter = credit_cards_.begin();
       iter != credit_cards_.end(); ++iter) {
    if (!FindByGUID<CreditCard>(*credit_cards, (*iter)->guid()))
      wds->RemoveCreditCard((*iter)->guid());
  }

  // Update the web database with the existing credit cards.
  for (std::vector<CreditCard>::iterator iter = credit_cards->begin();
       iter != credit_cards->end(); ++iter) {
    if (FindByGUID<CreditCard>(credit_cards_, iter->guid()))
      wds->UpdateCreditCard(*iter);
  }

  // Add the new credit cards to the web database.  Don't add a duplicate.
  for (std::vector<CreditCard>::iterator iter = credit_cards->begin();
       iter != credit_cards->end(); ++iter) {
    if (!FindByGUID<CreditCard>(credit_cards_, iter->guid()) &&
        !FindByContents(credit_cards_, *iter))
      wds->AddCreditCard(*iter);
  }

  // Copy in the new credit cards.
  credit_cards_.reset();
  for (std::vector<CreditCard>::iterator iter = credit_cards->begin();
       iter != credit_cards->end(); ++iter) {
    credit_cards_.push_back(new CreditCard(*iter));
  }

  // Read our writes to ensure consistency with the database.
  Refresh();

  FOR_EACH_OBSERVER(Observer, observers_, OnPersonalDataChanged());
}

// TODO(jhawkins): Refactor SetProfiles so this isn't so hacky.
void PersonalDataManager::AddProfile(const AutofillProfile& profile) {
  // Don't save a web profile if the data in the profile is a subset of an
  // auxiliary profile.
  for (std::vector<AutofillProfile*>::const_iterator iter =
           auxiliary_profiles_.begin();
       iter != auxiliary_profiles_.end(); ++iter) {
    if (profile.IsSubsetOf(**iter))
      return;
  }

  std::vector<AutofillProfile> profiles;
  MergeProfile(profile, web_profiles_.get(), &profiles);
  SetProfiles(&profiles);
}

void PersonalDataManager::UpdateProfile(const AutofillProfile& profile) {
  WebDataService* wds = profile_->GetWebDataService(Profile::EXPLICIT_ACCESS);
  if (!wds)
    return;

  // Update the cached profile.
  for (std::vector<AutofillProfile*>::iterator iter = web_profiles_->begin();
       iter != web_profiles_->end(); ++iter) {
    if ((*iter)->guid() == profile.guid()) {
      delete *iter;
      *iter = new AutofillProfile(profile);
      break;
    }
  }

  // Ensure that profile labels are up to date.
  AutofillProfile::AdjustInferredLabels(&web_profiles_.get());

  wds->UpdateAutofillProfile(profile);
  FOR_EACH_OBSERVER(Observer, observers_, OnPersonalDataChanged());
}

void PersonalDataManager::RemoveProfile(const std::string& guid) {
  // TODO(jhawkins): Refactor SetProfiles so this isn't so hacky.
  std::vector<AutofillProfile> profiles(web_profiles_.size());
  std::transform(web_profiles_.begin(), web_profiles_.end(),
                 profiles.begin(),
                 DereferenceFunctor<AutofillProfile>());

  // Remove the profile that matches |guid|.
  profiles.erase(
      std::remove_if(profiles.begin(), profiles.end(),
                     FormGroupMatchesByGUIDFunctor<AutofillProfile>(guid)),
      profiles.end());

  SetProfiles(&profiles);
}

AutofillProfile* PersonalDataManager::GetProfileByGUID(
    const std::string& guid) {
  for (std::vector<AutofillProfile*>::iterator iter = web_profiles_->begin();
       iter != web_profiles_->end(); ++iter) {
    if ((*iter)->guid() == guid)
      return *iter;
  }
  return NULL;
}

// TODO(jhawkins): Refactor SetCreditCards so this isn't so hacky.
void PersonalDataManager::AddCreditCard(const CreditCard& credit_card) {
  std::vector<CreditCard> credit_cards(credit_cards_.size());
  std::transform(credit_cards_.begin(), credit_cards_.end(),
                 credit_cards.begin(),
                 DereferenceFunctor<CreditCard>());

  credit_cards.push_back(credit_card);
  SetCreditCards(&credit_cards);
}

void PersonalDataManager::UpdateCreditCard(const CreditCard& credit_card) {
  WebDataService* wds = profile_->GetWebDataService(Profile::EXPLICIT_ACCESS);
  if (!wds)
    return;

  // Update the cached credit card.
  for (std::vector<CreditCard*>::iterator iter = credit_cards_->begin();
       iter != credit_cards_->end(); ++iter) {
    if ((*iter)->guid() == credit_card.guid()) {
      delete *iter;
      *iter = new CreditCard(credit_card);
      break;
    }
  }

  wds->UpdateCreditCard(credit_card);
  FOR_EACH_OBSERVER(Observer, observers_, OnPersonalDataChanged());
}

void PersonalDataManager::RemoveCreditCard(const std::string& guid) {
  // TODO(jhawkins): Refactor SetCreditCards so this isn't so hacky.
  std::vector<CreditCard> credit_cards(credit_cards_.size());
  std::transform(credit_cards_.begin(), credit_cards_.end(),
                 credit_cards.begin(),
                 DereferenceFunctor<CreditCard>());

  // Remove the credit card that matches |guid|.
  credit_cards.erase(
      std::remove_if(credit_cards.begin(), credit_cards.end(),
                     FormGroupMatchesByGUIDFunctor<CreditCard>(guid)),
      credit_cards.end());

  SetCreditCards(&credit_cards);
}

CreditCard* PersonalDataManager::GetCreditCardByGUID(const std::string& guid) {
  for (std::vector<CreditCard*>::iterator iter = credit_cards_.begin();
       iter != credit_cards_.end(); ++iter) {
    if ((*iter)->guid() == guid)
      return *iter;
  }
  return NULL;
}

void PersonalDataManager::GetPossibleFieldTypes(const string16& text,
                                                FieldTypeSet* possible_types) {
  string16 clean_info = StringToLowerASCII(CollapseWhitespace(text, false));
  if (clean_info.empty()) {
    possible_types->insert(EMPTY_TYPE);
    return;
  }

  const std::vector<AutofillProfile*>& profiles = this->profiles();
  for (std::vector<AutofillProfile*>::const_iterator iter = profiles.begin();
       iter != profiles.end(); ++iter) {
    const FormGroup* profile = *iter;
    if (!profile) {
      DLOG(ERROR) << "NULL information in profiles list";
      continue;
    }

    profile->GetPossibleFieldTypes(clean_info, possible_types);
  }

  for (ScopedVector<CreditCard>::iterator iter = credit_cards_.begin();
       iter != credit_cards_.end(); ++iter) {
    const FormGroup* credit_card = *iter;
    if (!credit_card) {
      DLOG(ERROR) << "NULL information in credit cards list";
      continue;
    }

    credit_card->GetPossibleFieldTypes(clean_info, possible_types);
  }

  if (possible_types->empty())
    possible_types->insert(UNKNOWN_TYPE);
}

bool PersonalDataManager::HasPassword() {
  return !password_hash_.empty();
}

bool PersonalDataManager::IsDataLoaded() const {
  return is_data_loaded_;
}

const std::vector<AutofillProfile*>& PersonalDataManager::profiles() {
  // |profile_| is NULL in AutofillManagerTest.
  bool auxiliary_profiles_enabled = profile_ ? profile_->GetPrefs()->GetBoolean(
      prefs::kAutofillAuxiliaryProfilesEnabled) : false;
  if (!auxiliary_profiles_enabled)
    return web_profiles();

#if !defined(OS_MACOSX)
  NOTREACHED() << "Auxiliary profiles supported on Mac only";
#endif

  profiles_.clear();

  // Populates |auxiliary_profiles_|.
  LoadAuxiliaryProfiles();

  profiles_.insert(profiles_.end(), web_profiles_.begin(), web_profiles_.end());
  profiles_.insert(profiles_.end(),
      auxiliary_profiles_.begin(), auxiliary_profiles_.end());
  return profiles_;
}

const std::vector<AutofillProfile*>& PersonalDataManager::web_profiles() {
  return web_profiles_.get();
}

const std::vector<CreditCard*>& PersonalDataManager::credit_cards() {
  return credit_cards_.get();
}

void PersonalDataManager::Refresh() {
  LoadProfiles();
  LoadCreditCards();
}

PersonalDataManager::PersonalDataManager()
    : profile_(NULL),
      is_data_loaded_(false),
      pending_profiles_query_(0),
      pending_creditcards_query_(0),
      metric_logger_(new AutofillMetrics),
      has_logged_profile_count_(false) {
}

void PersonalDataManager::Init(Profile* profile) {
  profile_ = profile;
  metric_logger_->LogIsAutofillEnabledAtStartup(IsAutofillEnabled());

  LoadProfiles();
  LoadCreditCards();
}

bool PersonalDataManager::IsAutofillEnabled() const {
  return profile_->GetPrefs()->GetBoolean(prefs::kAutofillEnabled);
}

// static
bool PersonalDataManager::IsValidLearnableProfile(
    const AutofillProfile& profile) {
  if (!IsMinimumAddress(profile))
    return false;

  string16 email = profile.GetInfo(EMAIL_ADDRESS);
  if (!email.empty() && !IsValidEmail(email))
    return false;

  // Reject profiles with invalid US state information.
  string16 state = profile.GetInfo(ADDRESS_HOME_STATE);
  if (profile.CountryCode() == "US" &&
      !state.empty() && !autofill::IsValidState(state)) {
    return false;
  }

  // Reject profiles with invalid US zip information.
  string16 zip = profile.GetInfo(ADDRESS_HOME_ZIP);
  if (profile.CountryCode() == "US" && !zip.empty() && !IsValidZip(zip))
    return false;

  return true;
}

// static
bool PersonalDataManager::MergeProfile(
    const AutofillProfile& profile,
    const std::vector<AutofillProfile*>& existing_profiles,
    std::vector<AutofillProfile>* merged_profiles) {
  DCHECK(merged_profiles);
  merged_profiles->clear();

  // Set to true if |profile| is merged into |existing_profiles|.
  bool merged = false;

  // First preference is to add missing values to an existing profile.
  // Only merge with the first match.
  for (std::vector<AutofillProfile*>::const_iterator iter =
           existing_profiles.begin();
       iter != existing_profiles.end(); ++iter) {
    if (!merged) {
      if (profile.IsSubsetOf(**iter)) {
        // In this case, the existing profile already contains all of the data
        // in |profile|, so consider the profiles already merged.
        merged = true;
      } else if ((*iter)->IntersectionOfTypesHasEqualValues(profile)) {
        // |profile| contains all of the data in this profile, plus more.
        merged = true;
        (*iter)->MergeWith(profile);
      }
    }
    merged_profiles->push_back(**iter);
  }

  // The second preference, if not merged above, is to alter non-primary values
  // where the primary values match.
  // Again, only merge with the first match.
  if (!merged) {
    merged_profiles->clear();
    for (std::vector<AutofillProfile*>::const_iterator iter =
             existing_profiles.begin();
         iter != existing_profiles.end(); ++iter) {
      if (!merged) {
        if (!profile.PrimaryValue().empty() &&
            StringToLowerASCII((*iter)->PrimaryValue()) ==
                StringToLowerASCII(profile.PrimaryValue())) {
          merged = true;
          (*iter)->OverwriteWithOrAddTo(profile);
        }
      }
      merged_profiles->push_back(**iter);
    }
  }

  // Finally, if the new profile was not merged with an existing profile then
  // add the new profile to the list.
  if (!merged)
    merged_profiles->push_back(profile);

  return merged;
}

void PersonalDataManager::LoadProfiles() {
  WebDataService* web_data_service =
      profile_->GetWebDataService(Profile::EXPLICIT_ACCESS);
  if (!web_data_service) {
    NOTREACHED();
    return;
  }

  CancelPendingQuery(&pending_profiles_query_);

  pending_profiles_query_ = web_data_service->GetAutofillProfiles(this);
}

// Win and Linux implementations do nothing.  Mac implementation fills in the
// contents of |auxiliary_profiles_|.
#if !defined(OS_MACOSX)
void PersonalDataManager::LoadAuxiliaryProfiles() {
}
#endif

void PersonalDataManager::LoadCreditCards() {
  WebDataService* web_data_service =
      profile_->GetWebDataService(Profile::EXPLICIT_ACCESS);
  if (!web_data_service) {
    NOTREACHED();
    return;
  }

  CancelPendingQuery(&pending_creditcards_query_);

  pending_creditcards_query_ = web_data_service->GetCreditCards(this);
}

void PersonalDataManager::ReceiveLoadedProfiles(WebDataService::Handle h,
                                                const WDTypedResult* result) {
  DCHECK_EQ(pending_profiles_query_, h);

  pending_profiles_query_ = 0;
  web_profiles_.reset();

  const WDResult<std::vector<AutofillProfile*> >* r =
      static_cast<const WDResult<std::vector<AutofillProfile*> >*>(result);

  std::vector<AutofillProfile*> profiles = r->GetValue();
  for (std::vector<AutofillProfile*>::iterator iter = profiles.begin();
       iter != profiles.end(); ++iter) {
    web_profiles_.push_back(*iter);
  }

  LogProfileCount();
  EmptyMigrationTrash();
}

void PersonalDataManager::ReceiveLoadedCreditCards(
    WebDataService::Handle h, const WDTypedResult* result) {
  DCHECK_EQ(pending_creditcards_query_, h);

  pending_creditcards_query_ = 0;
  credit_cards_.reset();

  const WDResult<std::vector<CreditCard*> >* r =
      static_cast<const WDResult<std::vector<CreditCard*> >*>(result);

  std::vector<CreditCard*> credit_cards = r->GetValue();
  for (std::vector<CreditCard*>::iterator iter = credit_cards.begin();
       iter != credit_cards.end(); ++iter) {
    credit_cards_.push_back(*iter);
  }
}

void PersonalDataManager::CancelPendingQuery(WebDataService::Handle* handle) {
  if (*handle) {
    WebDataService* web_data_service =
        profile_->GetWebDataService(Profile::EXPLICIT_ACCESS);
    if (!web_data_service) {
      NOTREACHED();
      return;
    }
    web_data_service->CancelRequest(*handle);
  }
  *handle = 0;
}

void PersonalDataManager::SaveImportedProfile(
    const AutofillProfile& imported_profile) {
  if (profile_->IsOffTheRecord())
    return;

  AddProfile(imported_profile);
}


void PersonalDataManager::SaveImportedCreditCard(
    const CreditCard& imported_credit_card) {
  if (profile_->IsOffTheRecord())
    return;

  // Set to true if |imported_credit_card| is merged into the credit card list.
  bool merged = false;

  std::vector<CreditCard> creditcards;
  for (std::vector<CreditCard*>::const_iterator iter = credit_cards_.begin();
       iter != credit_cards_.end();
       ++iter) {
    if (imported_credit_card.IsSubsetOf(**iter)) {
      // In this case, the existing credit card already contains all of the data
      // in |imported_credit_card|, so consider the credit cards already
      // merged.
      merged = true;
    } else if ((*iter)->IntersectionOfTypesHasEqualValues(
        imported_credit_card)) {
      // |imported_credit_card| contains all of the data in this credit card,
      // plus more.
      merged = true;
      (*iter)->MergeWith(imported_credit_card);
    } else if (!imported_credit_card.number().empty() &&
               (*iter)->number() == imported_credit_card.number()) {
      merged = true;
      (*iter)->OverwriteWith(imported_credit_card);
    }

    creditcards.push_back(**iter);
  }

  if (!merged)
    creditcards.push_back(imported_credit_card);

  SetCreditCards(&creditcards);
}

void PersonalDataManager::EmptyMigrationTrash() {
  if (!profile_ || profile_->IsOffTheRecord())
    return;

  WebDataService* web_data_service =
      profile_->GetWebDataService(Profile::EXPLICIT_ACCESS);
  if (!web_data_service) {
    NOTREACHED();
    return;
  }

  ProfileSyncService* sync_service = profile_->GetProfileSyncService();
  if (!sync_service)
    return;

  if (!sync_service->HasSyncSetupCompleted()) {
    web_data_service->EmptyMigrationTrash(false);
  } else if (sync_service->ShouldPushChanges()) {
    web_data_service->EmptyMigrationTrash(true);
  } else {
    // Install ourself as a listener so we can empty the trash once the
    // sync service becomes available.
    if (!sync_service->HasObserver(this))
      sync_service->AddObserver(this);
  }
}

void PersonalDataManager::LogProfileCount() const {
  if (!has_logged_profile_count_) {
    metric_logger_->LogStoredProfileCount(web_profiles_.size());
    has_logged_profile_count_ = true;
  }
}

const AutofillMetrics* PersonalDataManager::metric_logger() const {
  return metric_logger_.get();
}

void PersonalDataManager::set_metric_logger(
    const AutofillMetrics* metric_logger) {
  metric_logger_.reset(metric_logger);
}
