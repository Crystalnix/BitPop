// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/autofill/phone_number.h"

#include "base/basictypes.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/autofill/autofill_country.h"
#include "chrome/browser/autofill/autofill_profile.h"
#include "chrome/browser/autofill/autofill_type.h"
#include "chrome/browser/autofill/field_types.h"
#include "chrome/browser/autofill/phone_number_i18n.h"

namespace {

const char16 kPhoneNumberSeparators[] = { ' ', '.', '(', ')', '-', 0 };

// The number of digits in a phone number.
const size_t kPhoneNumberLength = 7;

// The number of digits in an area code.
const size_t kPhoneCityCodeLength = 3;

void StripPunctuation(string16* number) {
  RemoveChars(*number, kPhoneNumberSeparators, number);
}

}  // namespace

PhoneNumber::PhoneNumber(AutofillProfile* profile)
    : profile_(profile) {
}

PhoneNumber::PhoneNumber(const PhoneNumber& number)
    : FormGroup(),
      profile_(NULL) {
  *this = number;
}

PhoneNumber::~PhoneNumber() {}

PhoneNumber& PhoneNumber::operator=(const PhoneNumber& number) {
  if (this == &number)
    return *this;

  number_ = number.number_;
  profile_ = number.profile_;
  cached_parsed_phone_ = number.cached_parsed_phone_;
  return *this;
}

void PhoneNumber::GetSupportedTypes(FieldTypeSet* supported_types) const {
  supported_types->insert(PHONE_HOME_WHOLE_NUMBER);
  supported_types->insert(PHONE_HOME_NUMBER);
  supported_types->insert(PHONE_HOME_CITY_CODE);
  supported_types->insert(PHONE_HOME_CITY_AND_NUMBER);
  supported_types->insert(PHONE_HOME_COUNTRY_CODE);
}

string16 PhoneNumber::GetRawInfo(AutofillFieldType type) const {
  if (type == PHONE_HOME_WHOLE_NUMBER)
    return number_;

  // Only the whole number is available as raw data.  All of the other types are
  // parsed from this raw info, and parsing requires knowledge of the phone
  // number's region, which is only available via GetInfo().
  return string16();
}

void PhoneNumber::SetRawInfo(AutofillFieldType type, const string16& value) {
  if (type != PHONE_HOME_CITY_AND_NUMBER &&
      type != PHONE_HOME_WHOLE_NUMBER) {
    // Only full phone numbers should be set directly.  The remaining field
    // field types are read-only.
    return;
  }

  number_ = value;

  // Invalidate the cached number.
  cached_parsed_phone_ = autofill_i18n::PhoneObject();
}

// Normalize phones if |type| is a whole number:
//   (650)2345678 -> 6502345678
//   1-800-FLOWERS -> 18003569377
// If the phone cannot be normalized, returns the stored value verbatim.
string16 PhoneNumber::GetInfo(AutofillFieldType type,
                              const std::string& app_locale) const {
  if (type == PHONE_HOME_WHOLE_NUMBER) {
    // Whole numbers require special handling: If normalization for the number
    // fails, return the non-normalized number instead.
    string16 phone = GetRawInfo(type);

    // TODO(isherman): Can/should this use the cached_parsed_phone_?
    string16 normalized_phone =
        autofill_i18n::NormalizePhoneNumber(phone, GetRegion(app_locale));
    return !normalized_phone.empty() ? normalized_phone : phone;
  }

  UpdateCacheIfNeeded(app_locale);
  if (!cached_parsed_phone_.IsValidNumber())
    return string16();

  switch (type) {
    case PHONE_HOME_NUMBER:
      return cached_parsed_phone_.GetNumber();

    case PHONE_HOME_CITY_CODE:
      return cached_parsed_phone_.GetCityCode();

    case PHONE_HOME_COUNTRY_CODE:
      return cached_parsed_phone_.GetCountryCode();

    case PHONE_HOME_CITY_AND_NUMBER:
      return
          cached_parsed_phone_.GetCityCode() + cached_parsed_phone_.GetNumber();

    case PHONE_HOME_WHOLE_NUMBER:
      NOTREACHED();  // Should have been handled above.
      return string16();

    default:
      NOTREACHED();
      return string16();
  }
}

bool PhoneNumber::SetInfo(AutofillFieldType type,
                          const string16& value,
                          const std::string& app_locale) {
  string16 number = value;
  StripPunctuation(&number);
  SetRawInfo(type, number);

  if (number_.empty())
    return true;

  // Normalize the phone number by validating and translating it into a
  // digits-only format.
  UpdateCacheIfNeeded(app_locale);
  number_ = cached_parsed_phone_.GetWholeNumber();
  return !number_.empty();
}

void PhoneNumber::GetMatchingTypes(const string16& text,
                                   const std::string& app_locale,
                                   FieldTypeSet* matching_types) const {
  string16 stripped_text = text;
  StripPunctuation(&stripped_text);
  FormGroup::GetMatchingTypes(stripped_text, app_locale, matching_types);

  // For US numbers, also compare to the three-digit prefix and the four-digit
  // suffix, since web sites often split numbers into these two fields.
  string16 number = GetInfo(PHONE_HOME_NUMBER, app_locale);
  if (GetRegion(app_locale) == "US" &&
      number.size() == (kPrefixLength + kSuffixLength)) {
    string16 prefix = number.substr(kPrefixOffset, kPrefixLength);
    string16 suffix = number.substr(kSuffixOffset, kSuffixLength);
    if (text == prefix || text == suffix)
      matching_types->insert(PHONE_HOME_NUMBER);
  }

  string16 whole_number = GetInfo(PHONE_HOME_WHOLE_NUMBER, app_locale);
  if (!whole_number.empty()) {
    string16 normalized_number =
        autofill_i18n::NormalizePhoneNumber(text, GetRegion(app_locale));
    if (normalized_number == whole_number)
      matching_types->insert(PHONE_HOME_WHOLE_NUMBER);
  }
}

std::string PhoneNumber::GetRegion(const std::string& app_locale) const {
  const std::string country_code = profile_->CountryCode();
  if (country_code.empty())
    return AutofillCountry::CountryCodeForLocale(app_locale);

  return country_code;
}

void PhoneNumber::UpdateCacheIfNeeded(const std::string& app_locale) const {
  std::string region = GetRegion(app_locale);
  if (!number_.empty() && cached_parsed_phone_.GetRegion() != region)
    cached_parsed_phone_ = autofill_i18n::PhoneObject(number_, region);
}

PhoneNumber::PhoneCombineHelper::PhoneCombineHelper() {
}

PhoneNumber::PhoneCombineHelper::~PhoneCombineHelper() {
}

bool PhoneNumber::PhoneCombineHelper::SetInfo(AutofillFieldType field_type,
                                              const string16& value) {
  if (field_type == PHONE_HOME_COUNTRY_CODE) {
    country_ = value;
    return true;
  }

  if (field_type == PHONE_HOME_CITY_CODE) {
    city_ = value;
    return true;
  }

  if (field_type == PHONE_HOME_CITY_AND_NUMBER) {
    phone_ = value;
    return true;
  }

  if (field_type == PHONE_HOME_WHOLE_NUMBER) {
    whole_number_ = value;
    return true;
  }

  if (field_type == PHONE_HOME_NUMBER) {
    phone_.append(value);
    return true;
  }

  return false;
}

bool PhoneNumber::PhoneCombineHelper::ParseNumber(const std::string& region,
                                                  string16* value) {
  if (!whole_number_.empty()) {
    *value = whole_number_;
    return true;
  }

  return autofill_i18n::ConstructPhoneNumber(
      country_, city_, phone_, region,
      (country_.empty() ?
          autofill_i18n::NATIONAL : autofill_i18n::INTERNATIONAL),
      value);
}

bool PhoneNumber::PhoneCombineHelper::IsEmpty() const {
  return phone_.empty() && whole_number_.empty();
}
