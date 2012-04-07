// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/autofill/address.h"

#include <stddef.h>

#include "base/basictypes.h"
#include "base/logging.h"
#include "base/string_util.h"
#include "chrome/browser/autofill/autofill_country.h"
#include "chrome/browser/autofill/autofill_type.h"
#include "chrome/browser/autofill/field_types.h"

namespace {

const char16 kAddressSplitChars[] = {'-', ',', '#', '.', ' ', 0};

// Returns the country code corresponding to |country|, which should be a
// localized country name.
std::string ToCountryCode(const string16& country) {
  std::string app_locale = AutofillCountry::ApplicationLocale();
  return AutofillCountry::GetCountryCode(country, app_locale);
}

}  // namespace

Address::Address() {}

Address::Address(const Address& address) : FormGroup() {
  *this = address;
}

Address::~Address() {}

Address& Address::operator=(const Address& address) {
  if (this == &address)
    return *this;

  line1_ = address.line1_;
  line2_ = address.line2_;
  city_ = address.city_;
  state_ = address.state_;
  country_code_ = address.country_code_;
  zip_code_ = address.zip_code_;
  return *this;
}

void Address::GetSupportedTypes(FieldTypeSet* supported_types) const {
  supported_types->insert(ADDRESS_HOME_LINE1);
  supported_types->insert(ADDRESS_HOME_LINE2);
  supported_types->insert(ADDRESS_HOME_CITY);
  supported_types->insert(ADDRESS_HOME_STATE);
  supported_types->insert(ADDRESS_HOME_ZIP);
  supported_types->insert(ADDRESS_HOME_COUNTRY);
}

string16 Address::GetInfo(AutofillFieldType type) const {
  if (type == ADDRESS_HOME_LINE1)
    return line1_;

  if (type == ADDRESS_HOME_LINE2)
    return line2_;

  if (type == ADDRESS_HOME_CITY)
    return city_;

  if (type == ADDRESS_HOME_STATE)
    return state_;

  if (type ==  ADDRESS_HOME_ZIP)
    return zip_code_;

  if (type == ADDRESS_HOME_COUNTRY)
    return Country();

  return string16();
}

void Address::SetInfo(AutofillFieldType type, const string16& value) {
  type = AutofillType::GetEquivalentFieldType(type);
  if (type == ADDRESS_HOME_LINE1)
    line1_ = value;
  else if (type == ADDRESS_HOME_LINE2)
    line2_ = value;
  else if (type == ADDRESS_HOME_CITY)
    city_ = value;
  else if (type == ADDRESS_HOME_STATE)
    state_ = value;
  else if (type == ADDRESS_HOME_COUNTRY)
    country_code_ = ToCountryCode(value);
  else if (type == ADDRESS_HOME_ZIP)
    zip_code_ = value;
  else
    NOTREACHED();
}

void Address::GetMatchingTypes(const string16& text,
                               FieldTypeSet* matching_types) const {
  FormGroup::GetMatchingTypes(text, matching_types);

  // Check to see if the |text| canonicalized as a country name is a match.
  std::string country_code = ToCountryCode(text);
  if (!country_code.empty() && country_code_ == country_code)
    matching_types->insert(ADDRESS_HOME_COUNTRY);
}

string16 Address::Country() const {
  if (country_code().empty())
    return string16();

  std::string app_locale = AutofillCountry::ApplicationLocale();
  return AutofillCountry(country_code(), app_locale).name();
}
