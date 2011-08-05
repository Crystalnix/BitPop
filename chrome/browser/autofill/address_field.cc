// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/autofill/address_field.h"

#include <stddef.h>

#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/string16.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/autofill/autofill_ecml.h"
#include "chrome/browser/autofill/autofill_field.h"
#include "chrome/browser/autofill/autofill_scanner.h"
#include "grit/autofill_resources.h"
#include "ui/base/l10n/l10n_util.h"

using autofill::GetEcmlPattern;

FormField* AddressField::Parse(AutofillScanner* scanner, bool is_ecml) {
  if (scanner->IsEnd())
    return NULL;

  scoped_ptr<AddressField> address_field(new AddressField);
  const AutofillField* initial_field = scanner->Cursor();
  scanner->SaveCursor();

  string16 attention_ignored =
      l10n_util::GetStringUTF16(IDS_AUTOFILL_ATTENTION_IGNORED_RE);
  string16 region_ignored =
      l10n_util::GetStringUTF16(IDS_AUTOFILL_REGION_IGNORED_RE);

  // Allow address fields to appear in any order.
  while (!scanner->IsEnd()) {
    if (ParseCompany(scanner, is_ecml, address_field.get()) ||
        ParseAddressLines(scanner, is_ecml, address_field.get()) ||
        ParseCity(scanner, is_ecml, address_field.get()) ||
        ParseState(scanner, is_ecml, address_field.get()) ||
        ParseZipCode(scanner, is_ecml, address_field.get()) ||
        ParseCountry(scanner, is_ecml, address_field.get())) {
      continue;
    } else if (ParseField(scanner, attention_ignored, NULL) ||
               ParseField(scanner, region_ignored, NULL)) {
      // We ignore the following:
      // * Attention.
      // * Province/Region/Other.
      continue;
    } else if (scanner->Cursor() != initial_field &&
               ParseEmptyLabel(scanner, NULL)) {
      // Ignore non-labeled fields within an address; the page
      // MapQuest Driving Directions North America.html contains such a field.
      // We only ignore such fields after we've parsed at least one other field;
      // otherwise we'd effectively parse address fields before other field
      // types after any non-labeled fields, and we want email address fields to
      // have precedence since some pages contain fields labeled
      // "Email address".
      continue;
    } else {
      // No field found.
      break;
    }
  }

  // If we have identified any address fields in this field then it should be
  // added to the list of fields.
  if (address_field->company_ != NULL ||
      address_field->address1_ != NULL || address_field->address2_ != NULL ||
      address_field->city_ != NULL || address_field->state_ != NULL ||
      address_field->zip_ != NULL || address_field->zip4_ ||
      address_field->country_ != NULL) {
    address_field->type_ = address_field->FindType();
    return address_field.release();
  }

  scanner->Rewind();
  return NULL;
}

AddressType AddressField::FindType() const {
  // This is not a full address, so don't even bother trying to figure
  // out its type.
  if (address1_ == NULL)
    return kGenericAddress;

  // First look at the field name, which itself will sometimes contain
  // "bill" or "ship".  We could check for the ECML type prefixes
  // here, but there's no need to since ECML's prefixes Ecom_BillTo
  // and Ecom_ShipTo contain "bill" and "ship" anyway.
  string16 name = StringToLowerASCII(address1_->name);
  return AddressTypeFromText(name);
}

AddressField::AddressField()
    : company_(NULL),
      address1_(NULL),
      address2_(NULL),
      city_(NULL),
      state_(NULL),
      zip_(NULL),
      zip4_(NULL),
      country_(NULL),
      type_(kGenericAddress) {
}

bool AddressField::ClassifyField(FieldTypeMap* map) const {
  AutofillFieldType address_company;
  AutofillFieldType address_line1;
  AutofillFieldType address_line2;
  AutofillFieldType address_city;
  AutofillFieldType address_state;
  AutofillFieldType address_zip;
  AutofillFieldType address_country;

  switch (type_) {
    case kShippingAddress:
     // Fall through. Autofill does not support shipping addresses.
    case kGenericAddress:
      address_company = COMPANY_NAME;
      address_line1 = ADDRESS_HOME_LINE1;
      address_line2 = ADDRESS_HOME_LINE2;
      address_city = ADDRESS_HOME_CITY;
      address_state = ADDRESS_HOME_STATE;
      address_zip = ADDRESS_HOME_ZIP;
      address_country = ADDRESS_HOME_COUNTRY;
      break;

    case kBillingAddress:
      address_company = COMPANY_NAME;
      address_line1 = ADDRESS_BILLING_LINE1;
      address_line2 = ADDRESS_BILLING_LINE2;
      address_city = ADDRESS_BILLING_CITY;
      address_state = ADDRESS_BILLING_STATE;
      address_zip = ADDRESS_BILLING_ZIP;
      address_country = ADDRESS_BILLING_COUNTRY;
      break;

    default:
      NOTREACHED();
      return false;
  }

  bool ok = AddClassification(company_, address_company, map);
  ok = ok && AddClassification(address1_, address_line1, map);
  ok = ok && AddClassification(address2_, address_line2, map);
  ok = ok && AddClassification(city_, address_city, map);
  ok = ok && AddClassification(state_, address_state, map);
  ok = ok && AddClassification(zip_, address_zip, map);
  ok = ok && AddClassification(country_, address_country, map);
  return ok;
}

// static
bool AddressField::ParseCompany(AutofillScanner* scanner,
                                bool is_ecml,
                                AddressField* address_field) {
  if (address_field->company_ && !address_field->company_->IsEmpty())
    return false;

  string16 pattern;
  if (is_ecml) {
    pattern = GetEcmlPattern(kEcmlShipToCompanyName,
                             kEcmlBillToCompanyName, '|');
  } else {
    pattern = l10n_util::GetStringUTF16(IDS_AUTOFILL_COMPANY_RE);
  }

  return ParseField(scanner, pattern, &address_field->company_);
}

// static
bool AddressField::ParseAddressLines(AutofillScanner* scanner,
                                     bool is_ecml,
                                     AddressField* address_field) {
  // We only match the string "address" in page text, not in element names,
  // because sometimes every element in a group of address fields will have
  // a name containing the string "address"; for example, on the page
  // Kohl's - Register Billing Address.html the text element labeled "city"
  // has the name "BILL_TO_ADDRESS<>city".  We do match address labels
  // such as "address1", which appear as element names on various pages (eg
  // AmericanGirl-Registration.html, BloomingdalesBilling.html,
  // EBay Registration Enter Information.html).
  if (address_field->address1_)
    return false;

  string16 pattern;
  if (is_ecml) {
    pattern = GetEcmlPattern(kEcmlShipToAddress1, kEcmlBillToAddress1, '|');
    if (!ParseField(scanner, pattern, &address_field->address1_))
      return false;
  } else {
    pattern = l10n_util::GetStringUTF16(IDS_AUTOFILL_ADDRESS_LINE_1_RE);
    string16 label_pattern =
        l10n_util::GetStringUTF16(IDS_AUTOFILL_ADDRESS_LINE_1_LABEL_RE);

    if (!ParseField(scanner, pattern, &address_field->address1_) &&
        !ParseFieldSpecifics(scanner, label_pattern, MATCH_LABEL | MATCH_TEXT,
                             &address_field->address1_)) {
      return false;
    }
  }

  // Optionally parse more address lines, which may have empty labels.
  // Some pages have 3 address lines (eg SharperImageModifyAccount.html)
  // Some pages even have 4 address lines (e.g. uk/ShoesDirect2.html)!
  if (is_ecml) {
    pattern = GetEcmlPattern(kEcmlShipToAddress2, kEcmlBillToAddress2, '|');
    if (!ParseEmptyLabel(scanner, &address_field->address2_))
      ParseField(scanner, pattern, &address_field->address2_);
  } else {
    pattern = l10n_util::GetStringUTF16(IDS_AUTOFILL_ADDRESS_LINE_2_RE);
    string16 label_pattern =
        l10n_util::GetStringUTF16(IDS_AUTOFILL_ADDRESS_LINE_1_LABEL_RE);
    if (!ParseEmptyLabel(scanner, &address_field->address2_) &&
        !ParseField(scanner, pattern, &address_field->address2_)) {
      ParseFieldSpecifics(scanner, label_pattern, MATCH_LABEL | MATCH_TEXT,
                          &address_field->address2_);
    }
  }

  // Try for a third line, which we will promptly discard.
  if (address_field->address2_ != NULL) {
    if (is_ecml) {
      pattern = GetEcmlPattern(kEcmlShipToAddress3, kEcmlBillToAddress3, '|');
      ParseField(scanner, pattern, NULL);
    } else {
      pattern = l10n_util::GetStringUTF16(IDS_AUTOFILL_ADDRESS_LINE_3_RE);
      if (!ParseEmptyLabel(scanner, NULL))
        ParseField(scanner, pattern, NULL);
    }
  }

  return true;
}

// static
bool AddressField::ParseCountry(AutofillScanner* scanner,
                                bool is_ecml,
                                AddressField* address_field) {
  // Parse a country.  The occasional page (e.g.
  // Travelocity_New Member Information1.html) calls this a "location".
  // Note: ECML standard uses 2 letter country code (ISO 3166)
  if (address_field->country_ && !address_field->country_->IsEmpty())
    return false;

  string16 pattern;
  if (is_ecml)
    pattern = GetEcmlPattern(kEcmlShipToCountry, kEcmlBillToCountry, '|');
  else
    pattern = l10n_util::GetStringUTF16(IDS_AUTOFILL_COUNTRY_RE);

  return ParseFieldSpecifics(scanner, pattern, MATCH_DEFAULT | MATCH_SELECT,
                             &address_field->country_);
}

// static
bool AddressField::ParseZipCode(AutofillScanner* scanner,
                                bool is_ecml,
                                AddressField* address_field) {
  // Parse a zip code.  On some UK pages (e.g. The China Shop2.html) this
  // is called a "post code".
  //
  // HACK: Just for the MapQuest driving directions page we match the
  // exact name "1z", which MapQuest uses to label its zip code field.
  // Hopefully before long we'll be smart enough to find the zip code
  // on that page automatically.
  if (address_field->zip_)
    return false;

  string16 pattern;
  if (is_ecml) {
    pattern = GetEcmlPattern(kEcmlShipToPostalCode, kEcmlBillToPostalCode, '|');
  } else {
    pattern = l10n_util::GetStringUTF16(IDS_AUTOFILL_ZIP_CODE_RE);
  }

  AddressType tempType;
  string16 name = scanner->Cursor()->name;

  // Note: comparisons using the ECML compliant name as a prefix must be used in
  // order to accommodate Google Checkout. See |GetEcmlPattern| for more detail.
  string16 bill_to_postal_code_field(ASCIIToUTF16(kEcmlBillToPostalCode));
  if (StartsWith(name, bill_to_postal_code_field, false)) {
    tempType = kBillingAddress;
  } else if (StartsWith(name, bill_to_postal_code_field, false)) {
    tempType = kShippingAddress;
  } else {
    tempType = kGenericAddress;
  }

  if (!ParseField(scanner, pattern, &address_field->zip_))
    return false;

  address_field->type_ = tempType;
  if (!is_ecml) {
    // Look for a zip+4, whose field name will also often contain
    // the substring "zip".
    ParseField(scanner,
               l10n_util::GetStringUTF16(IDS_AUTOFILL_ZIP_4_RE),
               &address_field->zip4_);
  }

  return true;
}

// static
bool AddressField::ParseCity(AutofillScanner* scanner,
                             bool is_ecml,
                             AddressField* address_field) {
  // Parse a city name.  Some UK pages (e.g. The China Shop2.html) use
  // the term "town".
  if (address_field->city_)
    return false;

  string16 pattern;
  if (is_ecml)
    pattern = GetEcmlPattern(kEcmlShipToCity, kEcmlBillToCity, '|');
  else
    pattern = l10n_util::GetStringUTF16(IDS_AUTOFILL_CITY_RE);

  // Select fields are allowed here.  This occurs on top-100 site rediff.com.
  return ParseFieldSpecifics(scanner, pattern, MATCH_DEFAULT | MATCH_SELECT,
                             &address_field->city_);
}

// static
bool AddressField::ParseState(AutofillScanner* scanner,
                              bool is_ecml,
                              AddressField* address_field) {
  if (address_field->state_)
    return false;

  string16 pattern;
  if (is_ecml)
    pattern = GetEcmlPattern(kEcmlShipToStateProv, kEcmlBillToStateProv, '|');
  else
    pattern = l10n_util::GetStringUTF16(IDS_AUTOFILL_STATE_RE);

  return ParseFieldSpecifics(scanner, pattern, MATCH_DEFAULT | MATCH_SELECT,
                             &address_field->state_);
}

AddressType AddressField::AddressTypeFromText(const string16 &text) {
  if (text.find(l10n_util::GetStringUTF16(IDS_AUTOFILL_ADDRESS_TYPE_SAME_AS_RE))
          != string16::npos ||
      text.find(l10n_util::GetStringUTF16(IDS_AUTOFILL_ADDRESS_TYPE_USE_MY_RE))
          != string16::npos)
    // This text could be a checkbox label such as "same as my billing
    // address" or "use my shipping address".
    // ++ It would help if we generally skipped all text that appears
    // after a check box.
    return kGenericAddress;

  // Not all pages say "billing address" and "shipping address" explicitly;
  // for example, Craft Catalog1.html has "Bill-to Address" and
  // "Ship-to Address".
  size_t bill = text.rfind(
      l10n_util::GetStringUTF16(IDS_AUTOFILL_BILLING_DESIGNATOR_RE));
  size_t ship = text.rfind(
      l10n_util::GetStringUTF16(IDS_AUTOFILL_SHIPPING_DESIGNATOR_RE));

  if (bill == string16::npos && ship == string16::npos)
    return kGenericAddress;

  if (bill != string16::npos && ship == string16::npos)
    return kBillingAddress;

  if (bill == string16::npos && ship != string16::npos)
    return kShippingAddress;

  if (bill > ship)
    return kBillingAddress;

  return kShippingAddress;
}
