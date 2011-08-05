// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/autofill/contact_info.h"

#include <stddef.h>
#include <ostream>
#include <string>

#include "base/basictypes.h"
#include "base/logging.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/autofill/autofill_type.h"
#include "chrome/browser/autofill/field_types.h"

static const string16 kNameSplitChars = ASCIIToUTF16("-'. ");

static const AutofillFieldType kAutofillNameInfoTypes[] = {
  NAME_FIRST,
  NAME_MIDDLE,
  NAME_LAST
};

static const size_t kAutofillNameInfoLength =
    arraysize(kAutofillNameInfoTypes);

NameInfo::NameInfo() {}

NameInfo::NameInfo(const NameInfo& info) : FormGroup() {
  *this = info;
}

NameInfo::~NameInfo() {}

NameInfo& NameInfo::operator=(const NameInfo& info) {
  if (this == &info)
    return *this;

  first_tokens_ = info.first_tokens_;
  middle_tokens_ = info.middle_tokens_;
  last_tokens_ = info.last_tokens_;
  first_ = info.first_;
  middle_ = info.middle_;
  last_ = info.last_;
  return *this;
}

void NameInfo::GetMatchingTypes(const string16& text,
                                FieldTypeSet* matching_types) const {
  DCHECK(matching_types);

  if (IsFirstName(text))
    matching_types->insert(NAME_FIRST);

  if (IsMiddleName(text))
    matching_types->insert(NAME_MIDDLE);

  if (IsLastName(text))
    matching_types->insert(NAME_LAST);

  if (IsMiddleInitial(text))
    matching_types->insert(NAME_MIDDLE_INITIAL);

  if (IsFullName(text))
    matching_types->insert(NAME_FULL);
}

void NameInfo::GetNonEmptyTypes(FieldTypeSet* non_empty_types) const {
  DCHECK(non_empty_types);

  if (!first().empty())
    non_empty_types->insert(NAME_FIRST);

  if (!middle().empty())
    non_empty_types->insert(NAME_MIDDLE);

  if (!last().empty())
    non_empty_types->insert(NAME_LAST);

  if (!MiddleInitial().empty())
    non_empty_types->insert(NAME_MIDDLE_INITIAL);

  if (!FullName().empty())
    non_empty_types->insert(NAME_FULL);
}

string16 NameInfo::GetInfo(AutofillFieldType type) const {
  if (type == NAME_FIRST)
    return first();

  if (type == NAME_MIDDLE)
    return middle();

  if (type == NAME_LAST)
    return last();

  if (type == NAME_MIDDLE_INITIAL)
    return MiddleInitial();

  if (type == NAME_FULL)
    return FullName();

  return string16();
}

void NameInfo::SetInfo(AutofillFieldType type, const string16& value) {
  DCHECK_EQ(AutofillType::NAME, AutofillType(type).group());
  if (type == NAME_FIRST)
    SetFirst(value);
  else if (type == NAME_MIDDLE || type == NAME_MIDDLE_INITIAL)
    SetMiddle(value);
  else if (type == NAME_LAST)
    SetLast(value);
  else if (type == NAME_FULL)
    SetFullName(value);
  else
    NOTREACHED();
}

string16 NameInfo::FullName() const {
  if (first_.empty())
    return string16();

  std::vector<string16> full_name;
  full_name.push_back(first_);

  if (!middle_.empty())
    full_name.push_back(middle_);

  if (!last_.empty())
    full_name.push_back(last_);

  return JoinString(full_name, ' ');
}

string16 NameInfo::MiddleInitial() const {
  if (middle_.empty())
    return string16();

  string16 middle_name(middle());
  string16 initial;
  initial.push_back(middle_name[0]);
  return initial;
}

// If each of the 'words' contained in the text are also present in the first
// name then we will consider the text to be of type kFirstName. This means
// that people with multiple first names will be able to enter any one of
// their first names and have it correctly recognized.
bool NameInfo::IsFirstName(const string16& text) const {
  return IsNameMatch(text, first_tokens_);
}

// If each of the 'words' contained in the text are also present in the middle
// name then we will consider the text to be of type kMiddleName.
bool NameInfo::IsMiddleName(const string16& text) const {
  return IsNameMatch(text, middle_tokens_);
}

// If each of the 'words' contained in the text are also present in the last
// name then we will consider the text to be of type kLastName.
bool NameInfo::IsLastName(const string16& text) const {
  return IsNameMatch(text, last_tokens_);
}

bool NameInfo::IsMiddleInitial(const string16& text) const {
  if (text.length() != 1)
     return false;

  string16 lower_case = StringToLowerASCII(text);
  // If the text entered was a single character and it matches the first letter
  // of any of the given middle names then we consider it to be a middle
  // initial field.
  size_t middle_tokens_size = middle_tokens_.size();
  for (size_t i = 0; i < middle_tokens_size; ++i) {
    if (middle_tokens_[i][0] == lower_case[0])
      return true;
  }

  return false;
}

// A field will be considered to be of type NAME_FULL if:
//    1) it contains at least one word from the first name.
//    2) it contains at least one word from the last name.
//    3) all of the words in the field match a word in either the first,
//       middle, or last name.
bool NameInfo::IsFullName(const string16& text) const {
  size_t first_tokens_size = first_tokens_.size();
  if (first_tokens_size == 0)
    return false;

  size_t middle_tokens_size = middle_tokens_.size();

  size_t last_tokens_size = last_tokens_.size();
  if (last_tokens_size == 0)
    return false;

  std::vector<string16> text_tokens;
  Tokenize(text, kNameSplitChars, &text_tokens);
  size_t text_tokens_size = text_tokens.size();
  if (text_tokens_size == 0 || text_tokens_size < 2)
    return false;

  size_t name_tokens_size =
      first_tokens_size + middle_tokens_size + last_tokens_size;
  if (text_tokens_size > name_tokens_size)
    return false;

  bool first_name_match = false;
  bool last_name_match = false;
  for (std::vector<string16>::iterator iter = text_tokens.begin();
       iter != text_tokens.end(); ++iter) {
    bool match = false;
    if (IsWordInName(*iter, first_tokens_)) {
      match = true;
      first_name_match = true;
    }

    if (IsWordInName(*iter, last_tokens_)) {
      match = true;
      last_name_match = true;
    }

    if (IsWordInName(*iter, middle_tokens_))
      match = true;

    if (!match)
      return false;
  }

  return (first_name_match && last_name_match);
}

bool NameInfo::IsNameMatch(const string16& text,
                           const std::vector<string16>& name_tokens) const {
  size_t name_tokens_size = name_tokens.size();
  if (name_tokens_size == 0)
    return false;

  std::vector<string16> text_tokens;
  Tokenize(text, kNameSplitChars, &text_tokens);
  size_t text_tokens_size = text_tokens.size();
  if (text_tokens_size == 0)
    return false;

  if (text_tokens_size > name_tokens_size)
    return false;

  // If each of the 'words' contained in the text are also present in the name,
  // then we will consider the text to match the name.
  for (std::vector<string16>::iterator iter = text_tokens.begin();
       iter != text_tokens.end(); ++iter) {
    if (!IsWordInName(*iter, name_tokens))
      return false;
  }

  return true;
}

bool NameInfo::IsWordInName(const string16& word,
                            const std::vector<string16>& name_tokens) const {
  for (std::vector<string16>::const_iterator iter = name_tokens.begin();
       iter != name_tokens.end(); ++iter) {
    // |*iter| is already lower-cased.
    if (StringToLowerASCII(word) == *iter)
      return true;
  }

  return false;
}

void NameInfo::SetFirst(const string16& first) {
  first_ = first;
  first_tokens_.clear();
  Tokenize(first, kNameSplitChars, &first_tokens_);
  for (std::vector<string16>::iterator iter = first_tokens_.begin();
       iter != first_tokens_.end(); ++iter) {
    *iter = StringToLowerASCII(*iter);
  }
}

void NameInfo::SetMiddle(const string16& middle) {
  middle_ = middle;
  middle_tokens_.clear();
  Tokenize(middle, kNameSplitChars, &middle_tokens_);
  for (std::vector<string16>::iterator iter = middle_tokens_.begin();
       iter != middle_tokens_.end(); ++iter) {
    *iter = StringToLowerASCII(*iter);
  }
}

void NameInfo::SetLast(const string16& last) {
  last_ = last;
  last_tokens_.clear();
  Tokenize(last, kNameSplitChars, &last_tokens_);
  for (std::vector<string16>::iterator iter = last_tokens_.begin();
       iter != last_tokens_.end(); ++iter) {
    *iter = StringToLowerASCII(*iter);
  }
}

void NameInfo::SetFullName(const string16& full) {
  std::vector<string16> full_name_tokens;
  Tokenize(full, ASCIIToUTF16(" "), &full_name_tokens);
  // Clear the names.
  SetFirst(string16());
  SetMiddle(string16());
  SetLast(string16());

  // There are four possibilities: empty; first name; first and last names;
  // first, middle (possibly multiple strings) and then the last name.
  if (full_name_tokens.size() > 0) {
    SetFirst(full_name_tokens[0]);
    if (full_name_tokens.size() > 1) {
      SetLast(full_name_tokens.back());
      if (full_name_tokens.size() > 2) {
        full_name_tokens.erase(full_name_tokens.begin());
        full_name_tokens.pop_back();
        SetMiddle(JoinString(full_name_tokens, ' '));
      }
    }
  }
}

EmailInfo::EmailInfo() {}

EmailInfo::EmailInfo(const EmailInfo& info) : FormGroup() {
  *this = info;
}

EmailInfo::~EmailInfo() {}

EmailInfo& EmailInfo::operator=(const EmailInfo& info) {
  if (this == &info)
    return *this;

  email_ = info.email_;
  return *this;
}

void EmailInfo::GetMatchingTypes(const string16& text,
                                 FieldTypeSet* matching_types) const {
  DCHECK(matching_types);
  // TODO(isherman): Investigate case-insensitive comparison.
  if (email_ == text)
    matching_types->insert(EMAIL_ADDRESS);
}

void EmailInfo::GetNonEmptyTypes(FieldTypeSet* non_empty_types) const {
  DCHECK(non_empty_types);
  if (!email_.empty())
    non_empty_types->insert(EMAIL_ADDRESS);
}

string16 EmailInfo::GetInfo(AutofillFieldType type) const {
  if (type == EMAIL_ADDRESS)
    return email_;

  return string16();
}

void EmailInfo::SetInfo(AutofillFieldType type, const string16& value) {
  DCHECK_EQ(EMAIL_ADDRESS, type);
  email_ = value;
}

CompanyInfo::CompanyInfo() {}

CompanyInfo::CompanyInfo(const CompanyInfo& info) : FormGroup() {
  *this = info;
}

CompanyInfo::~CompanyInfo() {}

CompanyInfo& CompanyInfo::operator=(const CompanyInfo& info) {
  if (this == &info)
    return *this;

  company_name_ = info.company_name_;
  return *this;
}

void CompanyInfo::GetMatchingTypes(const string16& text,
                                   FieldTypeSet* matching_types) const {
  DCHECK(matching_types);

  if (company_name_ == text)
    matching_types->insert(COMPANY_NAME);
}

void CompanyInfo::GetNonEmptyTypes(FieldTypeSet* non_empty_types) const {
  DCHECK(non_empty_types);

  if (!company_name_.empty())
    non_empty_types->insert(COMPANY_NAME);
}

string16 CompanyInfo::GetInfo(AutofillFieldType type) const {
  if (type == COMPANY_NAME)
    return company_name_;

  return string16();
}

void CompanyInfo::SetInfo(AutofillFieldType type, const string16& value) {
  DCHECK_EQ(COMPANY_NAME, type);
  company_name_ = value;
}
