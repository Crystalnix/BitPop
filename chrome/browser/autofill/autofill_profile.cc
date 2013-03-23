// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/autofill/autofill_profile.h"

#include <algorithm>
#include <functional>
#include <map>
#include <ostream>
#include <set>

#include "base/basictypes.h"
#include "base/guid.h"
#include "base/logging.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/autofill/address.h"
#include "chrome/browser/autofill/autofill_country.h"
#include "chrome/browser/autofill/autofill_field.h"
#include "chrome/browser/autofill/autofill_type.h"
#include "chrome/browser/autofill/contact_info.h"
#include "chrome/browser/autofill/phone_number.h"
#include "chrome/browser/autofill/phone_number_i18n.h"
#include "chrome/common/form_field_data.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

namespace {

// Like |AutofillType::GetEquivalentFieldType()|, but also returns |NAME_FULL|
// for first, middle, and last name field types.
AutofillFieldType GetEquivalentFieldTypeCollapsingNames(
    AutofillFieldType field_type) {
  if (field_type == NAME_FIRST || field_type == NAME_MIDDLE ||
      field_type == NAME_LAST || field_type == NAME_MIDDLE_INITIAL)
    return NAME_FULL;

  return AutofillType::GetEquivalentFieldType(field_type);
}

// Fills |distinguishing_fields| with a list of fields to use when creating
// labels that can help to distinguish between two profiles. Draws fields from
// |suggested_fields| if it is non-NULL; otherwise returns a default list.
// If |suggested_fields| is non-NULL, does not include |excluded_field| in the
// list. Otherwise, |excluded_field| is ignored, and should be set to
// |UNKNOWN_TYPE| by convention. The resulting list of fields is sorted in
// decreasing order of importance.
void GetFieldsForDistinguishingProfiles(
    const std::vector<AutofillFieldType>* suggested_fields,
    AutofillFieldType excluded_field,
    std::vector<AutofillFieldType>* distinguishing_fields) {
  static const AutofillFieldType kDefaultDistinguishingFields[] = {
    NAME_FULL,
    ADDRESS_HOME_LINE1,
    ADDRESS_HOME_LINE2,
    ADDRESS_HOME_CITY,
    ADDRESS_HOME_STATE,
    ADDRESS_HOME_ZIP,
    ADDRESS_HOME_COUNTRY,
    EMAIL_ADDRESS,
    PHONE_HOME_WHOLE_NUMBER,
    COMPANY_NAME,
  };

  if (!suggested_fields) {
    DCHECK_EQ(excluded_field, UNKNOWN_TYPE);
    distinguishing_fields->assign(
        kDefaultDistinguishingFields,
        kDefaultDistinguishingFields + arraysize(kDefaultDistinguishingFields));
    return;
  }

  // Keep track of which fields we've seen so that we avoid duplicate entries.
  // Always ignore fields of unknown type and the excluded field.
  std::set<AutofillFieldType> seen_fields;
  seen_fields.insert(UNKNOWN_TYPE);
  seen_fields.insert(GetEquivalentFieldTypeCollapsingNames(excluded_field));

  distinguishing_fields->clear();
  for (std::vector<AutofillFieldType>::const_iterator it =
           suggested_fields->begin();
       it != suggested_fields->end(); ++it) {
    AutofillFieldType suggested_type =
        GetEquivalentFieldTypeCollapsingNames(*it);
    if (seen_fields.insert(suggested_type).second)
      distinguishing_fields->push_back(suggested_type);
  }

  // Special case: If the excluded field is a partial name (e.g. first name) and
  // the suggested fields include other name fields, include |NAME_FULL| in the
  // list of distinguishing fields as a last-ditch fallback. This allows us to
  // distinguish between profiles that are identical except for the name.
  if (excluded_field != NAME_FULL &&
      GetEquivalentFieldTypeCollapsingNames(excluded_field) == NAME_FULL) {
    for (std::vector<AutofillFieldType>::const_iterator it =
             suggested_fields->begin();
         it != suggested_fields->end(); ++it) {
      if (*it != excluded_field &&
          GetEquivalentFieldTypeCollapsingNames(*it) == NAME_FULL) {
        distinguishing_fields->push_back(NAME_FULL);
        break;
      }
    }
  }
}

// A helper function for string streaming.  Concatenates multi-valued entries
// stored for a given |type| into a single string.  This string is returned.
const string16 MultiString(const AutofillProfile& p, AutofillFieldType type) {
  std::vector<string16> values;
  p.GetRawMultiInfo(type, &values);
  string16 accumulate;
  for (size_t i = 0; i < values.size(); ++i) {
    if (i > 0)
      accumulate += ASCIIToUTF16(" ");
    accumulate += values[i];
  }
  return accumulate;
}

string16 GetFormGroupInfo(const FormGroup& form_group,
                          AutofillFieldType type,
                          const std::string& app_locale) {
  return app_locale.empty() ?
      form_group.GetRawInfo(type) :
      form_group.GetInfo(type, app_locale);
}

template <class T>
void CopyValuesToItems(AutofillFieldType type,
                       const std::vector<string16>& values,
                       std::vector<T>* form_group_items,
                       const T& prototype) {
  form_group_items->resize(values.size(), prototype);
  for (size_t i = 0; i < form_group_items->size(); ++i) {
    (*form_group_items)[i].SetRawInfo(type,
                                      CollapseWhitespace(values[i], false));
  }
  // Must have at least one (possibly empty) element.
  if (form_group_items->empty())
    form_group_items->resize(1, prototype);
}

template <class T>
void CopyItemsToValues(AutofillFieldType type,
                       const std::vector<T>& form_group_items,
                       const std::string& app_locale,
                       std::vector<string16>* values) {
  values->resize(form_group_items.size());
  for (size_t i = 0; i < values->size(); ++i) {
    (*values)[i] = GetFormGroupInfo(form_group_items[i], type, app_locale);
  }
}

// Collapse compound field types to their "full" type.  I.e. First name
// collapses to full name, area code collapses to full phone, etc.
void CollapseCompoundFieldTypes(FieldTypeSet* type_set) {
  FieldTypeSet collapsed_set;
  for (FieldTypeSet::iterator iter = type_set->begin(); iter != type_set->end();
       ++iter) {
    switch (*iter) {
      case NAME_FIRST:
      case NAME_MIDDLE:
      case NAME_LAST:
      case NAME_MIDDLE_INITIAL:
      case NAME_FULL:
      case NAME_SUFFIX:
        collapsed_set.insert(NAME_FULL);
        break;

      case PHONE_HOME_NUMBER:
      case PHONE_HOME_CITY_CODE:
      case PHONE_HOME_COUNTRY_CODE:
      case PHONE_HOME_CITY_AND_NUMBER:
      case PHONE_HOME_WHOLE_NUMBER:
        collapsed_set.insert(PHONE_HOME_WHOLE_NUMBER);
        break;

      default:
        collapsed_set.insert(*iter);
    }
  }
  std::swap(*type_set, collapsed_set);
}

class FindByPhone {
 public:
  FindByPhone(const string16& phone, const std::string& country_code)
      : phone_(phone),
        country_code_(country_code) {
  }

  bool operator()(const string16& phone) {
    return autofill_i18n::PhoneNumbersMatch(phone, phone_, country_code_);
  }

  bool operator()(const string16* phone) {
    return autofill_i18n::PhoneNumbersMatch(*phone, phone_, country_code_);
  }

 private:
  string16 phone_;
  std::string country_code_;
};

// Functor used to check for case-insensitive equality of two strings.
struct CaseInsensitiveStringEquals
    : public std::binary_function<string16, string16, bool>
{
  bool operator()(const string16& x, const string16& y) const {
    return
        x.size() == y.size() && StringToLowerASCII(x) == StringToLowerASCII(y);
  }
};

}  // namespace

AutofillProfile::AutofillProfile(const std::string& guid)
    : guid_(guid),
      name_(1),
      email_(1),
      home_number_(1, PhoneNumber(this)) {
}

AutofillProfile::AutofillProfile()
    : guid_(base::GenerateGUID()),
      name_(1),
      email_(1),
      home_number_(1, PhoneNumber(this)) {
}

AutofillProfile::AutofillProfile(const AutofillProfile& profile)
    : FormGroup() {
  operator=(profile);
}

AutofillProfile::~AutofillProfile() {
}

AutofillProfile& AutofillProfile::operator=(const AutofillProfile& profile) {
  if (this == &profile)
    return *this;

  label_ = profile.label_;
  guid_ = profile.guid_;

  name_ = profile.name_;
  email_ = profile.email_;
  company_ = profile.company_;
  home_number_ = profile.home_number_;

  for (size_t i = 0; i < home_number_.size(); ++i)
    home_number_[i].set_profile(this);

  address_ = profile.address_;

  return *this;
}

std::string AutofillProfile::GetGUID() const {
  return guid();
}

void AutofillProfile::GetMatchingTypes(const string16& text,
                                       const std::string& app_locale,
                                       FieldTypeSet* matching_types) const {
  FormGroupList info = FormGroups();
  for (FormGroupList::const_iterator it = info.begin(); it != info.end(); ++it)
    (*it)->GetMatchingTypes(text, app_locale, matching_types);
}

string16 AutofillProfile::GetRawInfo(AutofillFieldType type) const {
  AutofillFieldType return_type = AutofillType::GetEquivalentFieldType(type);
  const FormGroup* form_group = FormGroupForType(return_type);
  if (!form_group)
    return string16();

  return form_group->GetRawInfo(return_type);
}

void AutofillProfile::SetRawInfo(AutofillFieldType type,
                                 const string16& value) {
  FormGroup* form_group = MutableFormGroupForType(type);
  if (form_group)
    form_group->SetRawInfo(type, CollapseWhitespace(value, false));
}

string16 AutofillProfile::GetInfo(AutofillFieldType type,
                                  const std::string& app_locale) const {
  AutofillFieldType return_type = AutofillType::GetEquivalentFieldType(type);
  const FormGroup* form_group = FormGroupForType(return_type);
  if (!form_group)
    return string16();

  return form_group->GetInfo(return_type, app_locale);
}

bool AutofillProfile::SetInfo(AutofillFieldType type,
                              const string16& value,
                              const std::string& app_locale) {
  FormGroup* form_group = MutableFormGroupForType(type);
  if (!form_group)
    return false;

  return
      form_group->SetInfo(type, CollapseWhitespace(value, false), app_locale);
}

void AutofillProfile::SetRawMultiInfo(AutofillFieldType type,
                                      const std::vector<string16>& values) {
  switch (AutofillType(type).group()) {
    case AutofillType::NAME:
      CopyValuesToItems(type, values, &name_, NameInfo());
      break;
    case AutofillType::EMAIL:
      CopyValuesToItems(type, values, &email_, EmailInfo());
      break;
    case AutofillType::PHONE:
      CopyValuesToItems(type,
                        values,
                        &home_number_,
                        PhoneNumber(this));
      break;
    default:
      if (values.size() == 1) {
        SetRawInfo(type, values[0]);
      } else if (values.size() == 0) {
        SetRawInfo(type, string16());
      } else {
        // Shouldn't attempt to set multiple values on single-valued field.
        NOTREACHED();
      }
      break;
  }
}

void AutofillProfile::GetRawMultiInfo(AutofillFieldType type,
                                      std::vector<string16>* values) const {
  GetMultiInfoImpl(type, std::string(), values);
}

void AutofillProfile::GetMultiInfo(AutofillFieldType type,
                                   const std::string& app_locale,
                                   std::vector<string16>* values) const {
  GetMultiInfoImpl(type, app_locale, values);
}

void AutofillProfile::FillFormField(const AutofillField& field,
                                    size_t variant,
                                    FormFieldData* field_data) const {
  AutofillFieldType type = field.type();
  DCHECK_NE(AutofillType::CREDIT_CARD, AutofillType(type).group());
  DCHECK(field_data);

  if (type == PHONE_HOME_NUMBER) {
    FillPhoneNumberField(field, variant, field_data);
  } else if (field_data->form_control_type == "select-one") {
    FillSelectControl(type, field_data);
  } else {
    std::vector<string16> values;
    GetMultiInfo(type, AutofillCountry::ApplicationLocale(), &values);
    if (variant >= values.size()) {
      // If the variant is unavailable, bail.  This case is reachable, for
      // example if Sync updates a profile during the filling process.
      return;
    }

    field_data->value = values[variant];
  }
}

void AutofillProfile::FillPhoneNumberField(const AutofillField& field,
                                           size_t variant,
                                           FormFieldData* field_data) const {
  std::vector<string16> values;
  GetMultiInfo(field.type(), AutofillCountry::ApplicationLocale(), &values);
  DCHECK(variant < values.size());

  // If we are filling a phone number, check to see if the size field
  // matches the "prefix" or "suffix" sizes and fill accordingly.
  string16 number = values[variant];
  if (number.length() ==
          PhoneNumber::kPrefixLength + PhoneNumber::kSuffixLength) {
    if (field.phone_part() == AutofillField::PHONE_PREFIX ||
        field_data->max_length == PhoneNumber::kPrefixLength) {
      number = number.substr(PhoneNumber::kPrefixOffset,
                             PhoneNumber::kPrefixLength);
    } else if (field.phone_part() == AutofillField::PHONE_SUFFIX ||
               field_data->max_length == PhoneNumber::kSuffixLength) {
      number = number.substr(PhoneNumber::kSuffixOffset,
                             PhoneNumber::kSuffixLength);
    }
  }

  field_data->value = number;
}

const string16 AutofillProfile::Label() const {
  return label_;
}

const std::string AutofillProfile::CountryCode() const {
  return address_.country_code();
}

void AutofillProfile::SetCountryCode(const std::string& country_code) {
  address_.set_country_code(country_code);
}

bool AutofillProfile::IsEmpty() const {
  FieldTypeSet types;
  GetNonEmptyTypes(AutofillCountry::ApplicationLocale(), &types);
  return types.empty();
}

int AutofillProfile::Compare(const AutofillProfile& profile) const {
  const AutofillFieldType single_value_types[] = { COMPANY_NAME,
                                                   ADDRESS_HOME_LINE1,
                                                   ADDRESS_HOME_LINE2,
                                                   ADDRESS_HOME_CITY,
                                                   ADDRESS_HOME_STATE,
                                                   ADDRESS_HOME_ZIP,
                                                   ADDRESS_HOME_COUNTRY };

  for (size_t i = 0; i < arraysize(single_value_types); ++i) {
    int comparison = GetRawInfo(single_value_types[i]).compare(
        profile.GetRawInfo(single_value_types[i]));
    if (comparison != 0)
      return comparison;
  }

  const AutofillFieldType multi_value_types[] = { NAME_FIRST,
                                                  NAME_MIDDLE,
                                                  NAME_LAST,
                                                  EMAIL_ADDRESS,
                                                  PHONE_HOME_WHOLE_NUMBER };

  for (size_t i = 0; i < arraysize(multi_value_types); ++i) {
    std::vector<string16> values_a;
    std::vector<string16> values_b;
    GetRawMultiInfo(multi_value_types[i], &values_a);
    profile.GetRawMultiInfo(multi_value_types[i], &values_b);
    if (values_a.size() < values_b.size())
      return -1;
    if (values_a.size() > values_b.size())
      return 1;
    for (size_t j = 0; j < values_a.size(); ++j) {
      int comparison = values_a[j].compare(values_b[j]);
      if (comparison != 0)
        return comparison;
    }
  }

  return 0;
}

bool AutofillProfile::operator==(const AutofillProfile& profile) const {
  return guid_ == profile.guid_ && Compare(profile) == 0;
}

bool AutofillProfile::operator!=(const AutofillProfile& profile) const {
  return !operator==(profile);
}

const string16 AutofillProfile::PrimaryValue() const {
  return GetRawInfo(ADDRESS_HOME_LINE1) + GetRawInfo(ADDRESS_HOME_CITY);
}

bool AutofillProfile::IsSubsetOf(const AutofillProfile& profile) const {
  FieldTypeSet types;
  GetNonEmptyTypes(AutofillCountry::ApplicationLocale(), &types);

  for (FieldTypeSet::const_iterator iter = types.begin(); iter != types.end();
       ++iter) {
    if (*iter == NAME_FULL) {
      // Ignore the compound "full name" field type.  We are only interested in
      // comparing the constituent parts.  For example, if |this| has a middle
      // name saved, but |profile| lacks one, |profile| could still be a subset
      // of |this|.
      continue;
    } else if (AutofillType(*iter).group() == AutofillType::PHONE) {
      // Phone numbers should be canonicalized prior to being compared.
      if (*iter != PHONE_HOME_WHOLE_NUMBER) {
        continue;
      } else if (!autofill_i18n::PhoneNumbersMatch(GetRawInfo(*iter),
                                                   profile.GetRawInfo(*iter),
                                                   CountryCode())) {
        return false;
      }
    } else if (StringToLowerASCII(GetRawInfo(*iter)) !=
                   StringToLowerASCII(profile.GetRawInfo(*iter))) {
      return false;
    }
  }

  return true;
}

void AutofillProfile::OverwriteWithOrAddTo(const AutofillProfile& profile) {
  FieldTypeSet field_types;
  profile.GetNonEmptyTypes(AutofillCountry::ApplicationLocale(), &field_types);

  // Only transfer "full" types (e.g. full name) and not fragments (e.g.
  // first name, last name).
  CollapseCompoundFieldTypes(&field_types);

  for (FieldTypeSet::const_iterator iter = field_types.begin();
       iter != field_types.end(); ++iter) {
    if (AutofillProfile::SupportsMultiValue(*iter)) {
      std::vector<string16> new_values;
      profile.GetRawMultiInfo(*iter, &new_values);
      std::vector<string16> existing_values;
      GetRawMultiInfo(*iter, &existing_values);

      // GetMultiInfo always returns at least one element, even if the profile
      // has no data stored for this field type.
      if (existing_values.size() == 1 && existing_values.front().empty())
        existing_values.clear();

      FieldTypeGroup group = AutofillType(*iter).group();
      for (std::vector<string16>::iterator value_iter = new_values.begin();
           value_iter != new_values.end(); ++value_iter) {
        // Don't add duplicates.
        if (group == AutofillType::PHONE) {
          AddPhoneIfUnique(*value_iter, &existing_values);
        } else {
          std::vector<string16>::const_iterator existing_iter = std::find_if(
              existing_values.begin(), existing_values.end(),
              std::bind1st(CaseInsensitiveStringEquals(), *value_iter));
          if (existing_iter == existing_values.end())
            existing_values.insert(existing_values.end(), *value_iter);
        }
      }
      SetRawMultiInfo(*iter, existing_values);
    } else {
      string16 new_value = profile.GetRawInfo(*iter);
      if (StringToLowerASCII(GetRawInfo(*iter)) !=
              StringToLowerASCII(new_value)) {
        SetRawInfo(*iter, new_value);
      }
    }
  }
}

// static
bool AutofillProfile::SupportsMultiValue(AutofillFieldType type) {
  AutofillType::FieldTypeGroup group = AutofillType(type).group();
  return group == AutofillType::NAME ||
         group == AutofillType::EMAIL ||
         group == AutofillType::PHONE;
}

// static
bool AutofillProfile::AdjustInferredLabels(
    std::vector<AutofillProfile*>* profiles) {
  const size_t kMinimalFieldsShown = 2;

  std::vector<string16> created_labels;
  CreateInferredLabels(profiles, NULL, UNKNOWN_TYPE, kMinimalFieldsShown,
                       &created_labels);
  DCHECK_EQ(profiles->size(), created_labels.size());

  bool updated_labels = false;
  for (size_t i = 0; i < profiles->size(); ++i) {
    if ((*profiles)[i]->Label() != created_labels[i]) {
      updated_labels = true;
      (*profiles)[i]->label_ = created_labels[i];
    }
  }
  return updated_labels;
}

// static
void AutofillProfile::CreateInferredLabels(
    const std::vector<AutofillProfile*>* profiles,
    const std::vector<AutofillFieldType>* suggested_fields,
    AutofillFieldType excluded_field,
    size_t minimal_fields_shown,
    std::vector<string16>* created_labels) {
  DCHECK(profiles);
  DCHECK(created_labels);

  std::vector<AutofillFieldType> fields_to_use;
  GetFieldsForDistinguishingProfiles(suggested_fields, excluded_field,
                                     &fields_to_use);

  // Construct the default label for each profile. Also construct a map that
  // associates each label with the profiles that have this label. This map is
  // then used to detect which labels need further differentiating fields.
  std::map<string16, std::list<size_t> > labels;
  for (size_t i = 0; i < profiles->size(); ++i) {
    string16 label =
        (*profiles)[i]->ConstructInferredLabel(fields_to_use,
                                               minimal_fields_shown);
    labels[label].push_back(i);
  }

  created_labels->resize(profiles->size());
  for (std::map<string16, std::list<size_t> >::const_iterator it =
           labels.begin();
       it != labels.end(); ++it) {
    if (it->second.size() == 1) {
      // This label is unique, so use it without any further ado.
      string16 label = it->first;
      size_t profile_index = it->second.front();
      (*created_labels)[profile_index] = label;
    } else {
      // We have more than one profile with the same label, so add
      // differentiating fields.
      CreateDifferentiatingLabels(*profiles, it->second, fields_to_use,
                                  minimal_fields_shown, created_labels);
    }
  }
}

void AutofillProfile::GetSupportedTypes(FieldTypeSet* supported_types) const {
  FormGroupList info = FormGroups();
  for (FormGroupList::const_iterator it = info.begin(); it != info.end(); ++it)
    (*it)->GetSupportedTypes(supported_types);
}

bool AutofillProfile::FillCountrySelectControl(FormFieldData* field_data)
    const {
  std::string country_code = CountryCode();
  std::string app_locale = AutofillCountry::ApplicationLocale();

  DCHECK_EQ(field_data->option_values.size(),
            field_data->option_contents.size());
  for (size_t i = 0; i < field_data->option_values.size(); ++i) {
    // Canonicalize each <option> value to a country code, and compare to the
    // target country code.
    string16 value = field_data->option_values[i];
    string16 contents = field_data->option_contents[i];
    if (country_code == AutofillCountry::GetCountryCode(value, app_locale) ||
        country_code == AutofillCountry::GetCountryCode(contents, app_locale)) {
      field_data->value = value;
      return true;
    }
  }

  return false;
}

void AutofillProfile::GetMultiInfoImpl(AutofillFieldType type,
                                       const std::string& app_locale,
                                       std::vector<string16>* values) const {
  switch (AutofillType(type).group()) {
    case AutofillType::NAME:
      CopyItemsToValues(type, name_, app_locale, values);
      break;
    case AutofillType::EMAIL:
      CopyItemsToValues(type, email_, app_locale, values);
      break;
    case AutofillType::PHONE:
      CopyItemsToValues(type, home_number_, app_locale, values);
      break;
    default:
      values->resize(1);
      (*values)[0] = GetFormGroupInfo(*this, type, app_locale);
  }
}

void AutofillProfile::AddPhoneIfUnique(const string16& phone,
                                       std::vector<string16>* existing_phones) {
  DCHECK(existing_phones);
  // Phones allow "fuzzy" matching, so "1-800-FLOWERS", "18003569377",
  // "(800)356-9377" and "356-9377" are considered the same.
  if (std::find_if(existing_phones->begin(), existing_phones->end(),
                   FindByPhone(phone, CountryCode())) ==
      existing_phones->end()) {
    existing_phones->push_back(phone);
  }
}

string16 AutofillProfile::ConstructInferredLabel(
    const std::vector<AutofillFieldType>& included_fields,
    size_t num_fields_to_use) const {
  const string16 separator =
      l10n_util::GetStringUTF16(IDS_AUTOFILL_ADDRESS_SUMMARY_SEPARATOR);

  string16 label;
  size_t num_fields_used = 0;
  for (std::vector<AutofillFieldType>::const_iterator it =
           included_fields.begin();
       it != included_fields.end() && num_fields_used < num_fields_to_use;
       ++it) {
    string16 field = GetRawInfo(*it);
    if (field.empty())
      continue;

    if (!label.empty())
      label.append(separator);

    label.append(field);
    ++num_fields_used;
  }
  return label;
}

// static
void AutofillProfile::CreateDifferentiatingLabels(
    const std::vector<AutofillProfile*>& profiles,
    const std::list<size_t>& indices,
    const std::vector<AutofillFieldType>& fields,
    size_t num_fields_to_include,
    std::vector<string16>* created_labels) {
  // For efficiency, we first construct a map of fields to their text values and
  // each value's frequency.
  std::map<AutofillFieldType,
           std::map<string16, size_t> > field_text_frequencies_by_field;
  for (std::vector<AutofillFieldType>::const_iterator field = fields.begin();
       field != fields.end(); ++field) {
    std::map<string16, size_t>& field_text_frequencies =
        field_text_frequencies_by_field[*field];

    for (std::list<size_t>::const_iterator it = indices.begin();
         it != indices.end(); ++it) {
      const AutofillProfile* profile = profiles[*it];
      string16 field_text = profile->GetRawInfo(*field);

      // If this label is not already in the map, add it with frequency 0.
      if (!field_text_frequencies.count(field_text))
        field_text_frequencies[field_text] = 0;

      // Now, increment the frequency for this label.
      ++field_text_frequencies[field_text];
    }
  }

  // Now comes the meat of the algorithm. For each profile, we scan the list of
  // fields to use, looking for two things:
  //  1. A (non-empty) field that differentiates the profile from all others
  //  2. At least |num_fields_to_include| non-empty fields
  // Before we've satisfied condition (2), we include all fields, even ones that
  // are identical across all the profiles. Once we've satisfied condition (2),
  // we only include fields that that have at last two distinct values.
  for (std::list<size_t>::const_iterator it = indices.begin();
       it != indices.end(); ++it) {
    const AutofillProfile* profile = profiles[*it];

    std::vector<AutofillFieldType> label_fields;
    bool found_differentiating_field = false;
    for (std::vector<AutofillFieldType>::const_iterator field = fields.begin();
         field != fields.end(); ++field) {
      // Skip over empty fields.
      string16 field_text = profile->GetRawInfo(*field);
      if (field_text.empty())
        continue;

      std::map<string16, size_t>& field_text_frequencies =
          field_text_frequencies_by_field[*field];
      found_differentiating_field |=
          !field_text_frequencies.count(string16()) &&
          (field_text_frequencies[field_text] == 1);

      // Once we've found enough non-empty fields, skip over any remaining
      // fields that are identical across all the profiles.
      if (label_fields.size() >= num_fields_to_include &&
          (field_text_frequencies.size() == 1))
        continue;

      label_fields.push_back(*field);

      // If we've (1) found a differentiating field and (2) found at least
      // |num_fields_to_include| non-empty fields, we're done!
      if (found_differentiating_field &&
          label_fields.size() >= num_fields_to_include)
        break;
    }

    (*created_labels)[*it] =
        profile->ConstructInferredLabel(label_fields,
                                        label_fields.size());
  }
}

AutofillProfile::FormGroupList AutofillProfile::FormGroups() const {
  FormGroupList v(5);
  v[0] = &name_[0];
  v[1] = &email_[0];
  v[2] = &company_;
  v[3] = &home_number_[0];
  v[4] = &address_;
  return v;
}

const FormGroup* AutofillProfile::FormGroupForType(
    AutofillFieldType type) const {
  return const_cast<AutofillProfile*>(this)->MutableFormGroupForType(type);
}

FormGroup* AutofillProfile::MutableFormGroupForType(AutofillFieldType type) {
  FormGroup* form_group = NULL;
  switch (AutofillType(type).group()) {
    case AutofillType::NAME:
      form_group = &name_[0];
      break;
    case AutofillType::EMAIL:
      form_group = &email_[0];
      break;
    case AutofillType::COMPANY:
      form_group = &company_;
      break;
    case AutofillType::PHONE:
      form_group = &home_number_[0];
      break;
    case AutofillType::ADDRESS_HOME:
      form_group = &address_;
      break;
    default:
      break;
  }

  return form_group;
}

// So we can compare AutofillProfiles with EXPECT_EQ().
std::ostream& operator<<(std::ostream& os, const AutofillProfile& profile) {
  return os
      << UTF16ToUTF8(profile.Label())
      << " "
      << profile.guid()
      << " "
      << UTF16ToUTF8(MultiString(profile, NAME_FIRST))
      << " "
      << UTF16ToUTF8(MultiString(profile, NAME_MIDDLE))
      << " "
      << UTF16ToUTF8(MultiString(profile, NAME_LAST))
      << " "
      << UTF16ToUTF8(MultiString(profile, EMAIL_ADDRESS))
      << " "
      << UTF16ToUTF8(profile.GetRawInfo(COMPANY_NAME))
      << " "
      << UTF16ToUTF8(profile.GetRawInfo(ADDRESS_HOME_LINE1))
      << " "
      << UTF16ToUTF8(profile.GetRawInfo(ADDRESS_HOME_LINE2))
      << " "
      << UTF16ToUTF8(profile.GetRawInfo(ADDRESS_HOME_CITY))
      << " "
      << UTF16ToUTF8(profile.GetRawInfo(ADDRESS_HOME_STATE))
      << " "
      << UTF16ToUTF8(profile.GetRawInfo(ADDRESS_HOME_ZIP))
      << " "
      << UTF16ToUTF8(profile.GetRawInfo(ADDRESS_HOME_COUNTRY))
      << " "
      << UTF16ToUTF8(MultiString(profile, PHONE_HOME_WHOLE_NUMBER));
}
