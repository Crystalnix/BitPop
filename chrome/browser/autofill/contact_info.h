// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_AUTOFILL_CONTACT_INFO_H_
#define CHROME_BROWSER_AUTOFILL_CONTACT_INFO_H_
#pragma once

#include <vector>

#include "base/gtest_prod_util.h"
#include "base/string16.h"
#include "chrome/browser/autofill/field_types.h"
#include "chrome/browser/autofill/form_group.h"

// A form group that stores name information.
class NameInfo : public FormGroup {
 public:
  NameInfo();
  NameInfo(const NameInfo& info);
  virtual ~NameInfo();

  NameInfo& operator=(const NameInfo& info);

  // FormGroup:
  virtual string16 GetInfo(AutofillFieldType type) const OVERRIDE;
  virtual void SetInfo(AutofillFieldType type, const string16& value) OVERRIDE;

 private:
  // FormGroup:
  virtual void GetSupportedTypes(FieldTypeSet* supported_types) const OVERRIDE;

  // Returns the full name, which can include up to the first, middle, and last
  // name.
  string16 FullName() const;

  // Returns the middle initial if |middle_| is non-empty.  Returns an empty
  // string otherwise.
  string16 MiddleInitial() const;

  const string16& first() const { return first_; }
  const string16& middle() const { return middle_; }
  const string16& last() const { return last_; }

  // Sets |first_|, |middle_|, and |last_| to the tokenized |full|.
  // It is tokenized on a space only.
  void SetFullName(const string16& full);

  string16 first_;
  string16 middle_;
  string16 last_;
};

class EmailInfo : public FormGroup {
 public:
  EmailInfo();
  EmailInfo(const EmailInfo& info);
  virtual ~EmailInfo();

  EmailInfo& operator=(const EmailInfo& info);

  // FormGroup:
  virtual string16 GetInfo(AutofillFieldType type) const OVERRIDE;
  virtual void SetInfo(AutofillFieldType type, const string16& value) OVERRIDE;

 private:
  // FormGroup:
  virtual void GetSupportedTypes(FieldTypeSet* supported_types) const OVERRIDE;

  string16 email_;
};

class CompanyInfo : public FormGroup {
 public:
  CompanyInfo();
  CompanyInfo(const CompanyInfo& info);
  virtual ~CompanyInfo();

  CompanyInfo& operator=(const CompanyInfo& info);

  // FormGroup:
  virtual string16 GetInfo(AutofillFieldType type) const OVERRIDE;
  virtual void SetInfo(AutofillFieldType type, const string16& value) OVERRIDE;

 private:
  // FormGroup:
  virtual void GetSupportedTypes(FieldTypeSet* supported_types) const OVERRIDE;

  string16 company_name_;
};

#endif  // CHROME_BROWSER_AUTOFILL_CONTACT_INFO_H_
