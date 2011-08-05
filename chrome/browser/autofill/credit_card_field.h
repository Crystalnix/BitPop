// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_AUTOFILL_CREDIT_CARD_FIELD_H_
#define CHROME_BROWSER_AUTOFILL_CREDIT_CARD_FIELD_H_
#pragma once

#include <vector>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/gtest_prod_util.h"
#include "chrome/browser/autofill/autofill_type.h"
#include "chrome/browser/autofill/form_field.h"

class AutofillField;
class AutofillScanner;

class CreditCardField : public FormField {
 public:
  static FormField* Parse(AutofillScanner* scanner, bool is_ecml);

 protected:
  // FormField:
  virtual bool ClassifyField(FieldTypeMap* map) const OVERRIDE;

 private:
  FRIEND_TEST_ALL_PREFIXES(CreditCardFieldTest, ParseMiniumCreditCard);
  FRIEND_TEST_ALL_PREFIXES(CreditCardFieldTest, ParseMiniumCreditCardEcml);
  FRIEND_TEST_ALL_PREFIXES(CreditCardFieldTest, ParseFullCreditCard);
  FRIEND_TEST_ALL_PREFIXES(CreditCardFieldTest, ParseFullCreditCardEcml);
  FRIEND_TEST_ALL_PREFIXES(CreditCardFieldTest, ParseExpMonthYear);
  FRIEND_TEST_ALL_PREFIXES(CreditCardFieldTest, ParseExpMonthYear2);

  CreditCardField();

  const AutofillField* cardholder_;  // Optional.

  // Occasionally pages have separate fields for the cardholder's first and
  // last names; for such pages cardholder_ holds the first name field and
  // we store the last name field here.
  // (We could store an embedded NameField object here, but we don't do so
  // because the text patterns for matching a cardholder name are different
  // than for ordinary names, and because cardholder names never have titles,
  // middle names or suffixes.)
  const AutofillField* cardholder_last_;

  // TODO(jhawkins): Parse the select control.
  const AutofillField* type_;  // Optional.
  const AutofillField* number_;  // Required.

  // The 3-digit card verification number; we don't currently fill this.
  const AutofillField* verification_;

  // Both required.  TODO(jhawkins): Parse the select control.
  const AutofillField* expiration_month_;
  const AutofillField* expiration_year_;

  DISALLOW_COPY_AND_ASSIGN(CreditCardField);
};

#endif  // CHROME_BROWSER_AUTOFILL_CREDIT_CARD_FIELD_H_
