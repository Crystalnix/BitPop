// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/scoped_ptr.h"
#include "base/memory/scoped_vector.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/autofill/autofill_ecml.h"
#include "chrome/browser/autofill/address_field.h"
#include "chrome/browser/autofill/autofill_field.h"
#include "chrome/browser/autofill/autofill_scanner.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "webkit/glue/form_field.h"

class AddressFieldTest : public testing::Test {
 public:
  AddressFieldTest() {}

 protected:
  ScopedVector<const AutofillField> list_;
  scoped_ptr<AddressField> field_;
  FieldTypeMap field_type_map_;

  // Downcast for tests.
  static AddressField* Parse(AutofillScanner* scanner, bool is_ecml) {
    return static_cast<AddressField*>(AddressField::Parse(scanner, is_ecml));
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(AddressFieldTest);
};

TEST_F(AddressFieldTest, Empty) {
  AutofillScanner scanner(list_.get());
  field_.reset(Parse(&scanner, false));
  ASSERT_EQ(static_cast<AddressField*>(NULL), field_.get());
}

TEST_F(AddressFieldTest, NonParse) {
  list_.push_back(new AutofillField);
  AutofillScanner scanner(list_.get());
  field_.reset(Parse(&scanner, false));
  ASSERT_EQ(static_cast<AddressField*>(NULL), field_.get());
}

TEST_F(AddressFieldTest, ParseOneLineAddress) {
  list_.push_back(
      new AutofillField(webkit_glue::FormField(ASCIIToUTF16("Address"),
                                               ASCIIToUTF16("address"),
                                               string16(),
                                               ASCIIToUTF16("text"),
                                               0,
                                               false),
                        ASCIIToUTF16("addr1")));
  AutofillScanner scanner(list_.get());
  field_.reset(Parse(&scanner, false));
  ASSERT_NE(static_cast<AddressField*>(NULL), field_.get());
  EXPECT_EQ(kGenericAddress, field_->FindType());
  ASSERT_TRUE(field_->ClassifyField(&field_type_map_));
  ASSERT_TRUE(
      field_type_map_.find(ASCIIToUTF16("addr1")) != field_type_map_.end());
  EXPECT_EQ(ADDRESS_HOME_LINE1, field_type_map_[ASCIIToUTF16("addr1")]);
}

TEST_F(AddressFieldTest, ParseOneLineAddressBilling) {
  list_.push_back(
      new AutofillField(webkit_glue::FormField(ASCIIToUTF16("Address"),
                                               ASCIIToUTF16("billingAddress"),
                                               string16(),
                                               ASCIIToUTF16("text"),
                                               0,
                                               false),
                        ASCIIToUTF16("addr1")));
  AutofillScanner scanner(list_.get());
  field_.reset(Parse(&scanner, false));
  ASSERT_NE(static_cast<AddressField*>(NULL), field_.get());
  EXPECT_EQ(kBillingAddress, field_->FindType());
  ASSERT_TRUE(field_->ClassifyField(&field_type_map_));
  ASSERT_TRUE(
      field_type_map_.find(ASCIIToUTF16("addr1")) != field_type_map_.end());
  EXPECT_EQ(ADDRESS_BILLING_LINE1, field_type_map_[ASCIIToUTF16("addr1")]);
}

TEST_F(AddressFieldTest, ParseOneLineAddressShipping) {
  list_.push_back(
      new AutofillField(webkit_glue::FormField(ASCIIToUTF16("Address"),
                                               ASCIIToUTF16("shippingAddress"),
                                               string16(),
                                               ASCIIToUTF16("text"),
                                               0,
                                               false),
                        ASCIIToUTF16("addr1")));
  AutofillScanner scanner(list_.get());
  field_.reset(Parse(&scanner, false));
  ASSERT_NE(static_cast<AddressField*>(NULL), field_.get());
  EXPECT_EQ(kShippingAddress, field_->FindType());
  ASSERT_TRUE(field_->ClassifyField(&field_type_map_));
  ASSERT_TRUE(
      field_type_map_.find(ASCIIToUTF16("addr1")) != field_type_map_.end());
  EXPECT_EQ(ADDRESS_HOME_LINE1, field_type_map_[ASCIIToUTF16("addr1")]);
}

TEST_F(AddressFieldTest, ParseOneLineAddressEcml) {
  list_.push_back(
      new AutofillField(
          webkit_glue::FormField(ASCIIToUTF16("Address"),
                                 ASCIIToUTF16(kEcmlShipToAddress1),
                                 string16(),
                                 ASCIIToUTF16("text"),
                                 0,
                                 false),
          ASCIIToUTF16("addr1")));
  AutofillScanner scanner(list_.get());
  field_.reset(Parse(&scanner, true));
  ASSERT_NE(static_cast<AddressField*>(NULL), field_.get());
  EXPECT_EQ(kShippingAddress, field_->FindType());
  ASSERT_TRUE(field_->ClassifyField(&field_type_map_));
  ASSERT_TRUE(
      field_type_map_.find(ASCIIToUTF16("addr1")) != field_type_map_.end());
  EXPECT_EQ(ADDRESS_HOME_LINE1, field_type_map_[ASCIIToUTF16("addr1")]);
}

TEST_F(AddressFieldTest, ParseTwoLineAddress) {
  list_.push_back(
      new AutofillField(webkit_glue::FormField(ASCIIToUTF16("Address"),
                                               ASCIIToUTF16("address"),
                                               string16(),
                                               ASCIIToUTF16("text"),
                                               0,
                                               false),
                        ASCIIToUTF16("addr1")));
  list_.push_back(
      new AutofillField(webkit_glue::FormField(string16(),
                                               string16(),
                                               string16(),
                                               ASCIIToUTF16("text"),
                                               0,
                                               false),
                        ASCIIToUTF16("addr2")));
  AutofillScanner scanner(list_.get());
  field_.reset(Parse(&scanner, false));
  ASSERT_NE(static_cast<AddressField*>(NULL), field_.get());
  EXPECT_EQ(kGenericAddress, field_->FindType());
  ASSERT_TRUE(field_->ClassifyField(&field_type_map_));
  ASSERT_TRUE(
      field_type_map_.find(ASCIIToUTF16("addr1")) != field_type_map_.end());
  EXPECT_EQ(ADDRESS_HOME_LINE1, field_type_map_[ASCIIToUTF16("addr1")]);
  ASSERT_TRUE(
      field_type_map_.find(ASCIIToUTF16("addr2")) != field_type_map_.end());
  EXPECT_EQ(ADDRESS_HOME_LINE2, field_type_map_[ASCIIToUTF16("addr2")]);
}

TEST_F(AddressFieldTest, ParseThreeLineAddress) {
  list_.push_back(
      new AutofillField(webkit_glue::FormField(ASCIIToUTF16("Address Line1"),
                                               ASCIIToUTF16("Address"),
                                               string16(),
                                               ASCIIToUTF16("text"),
                                               0,
                                               false),
                        ASCIIToUTF16("addr1")));
  list_.push_back(
      new AutofillField(webkit_glue::FormField(ASCIIToUTF16("Address Line2"),
                                               ASCIIToUTF16("Address"),
                                               string16(),
                                               ASCIIToUTF16("text"),
                                               0,
                                               false),
                        ASCIIToUTF16("addr2")));
  list_.push_back(
      new AutofillField(webkit_glue::FormField(ASCIIToUTF16("Address Line3"),
                                               ASCIIToUTF16("Address"),
                                               string16(),
                                               ASCIIToUTF16("text"),
                                               0,
                                               false),
                        ASCIIToUTF16("addr3")));
  AutofillScanner scanner(list_.get());
  field_.reset(Parse(&scanner, false));
  ASSERT_NE(static_cast<AddressField*>(NULL), field_.get());
  EXPECT_EQ(kGenericAddress, field_->FindType());
  ASSERT_TRUE(field_->ClassifyField(&field_type_map_));
  ASSERT_TRUE(
      field_type_map_.find(ASCIIToUTF16("addr1")) != field_type_map_.end());
  EXPECT_EQ(ADDRESS_HOME_LINE1, field_type_map_[ASCIIToUTF16("addr1")]);
  ASSERT_TRUE(
      field_type_map_.find(ASCIIToUTF16("addr2")) != field_type_map_.end());
  EXPECT_EQ(ADDRESS_HOME_LINE2, field_type_map_[ASCIIToUTF16("addr2")]);
  ASSERT_TRUE(
      field_type_map_.find(ASCIIToUTF16("addr3")) == field_type_map_.end());
}

TEST_F(AddressFieldTest, ParseTwoLineAddressEcml) {
  list_.push_back(
      new AutofillField(
          webkit_glue::FormField(ASCIIToUTF16("Address"),
                                 ASCIIToUTF16(kEcmlShipToAddress1),
                                 string16(),
                                 ASCIIToUTF16("text"),
                                 0,
                                 false),
          ASCIIToUTF16("addr1")));
  list_.push_back(
      new AutofillField(
          webkit_glue::FormField(string16(),
                                 ASCIIToUTF16(kEcmlShipToAddress2),
                                 string16(),
                                 ASCIIToUTF16("text"),
                                 0,
                                 false),
          ASCIIToUTF16("addr2")));
  AutofillScanner scanner(list_.get());
  field_.reset(Parse(&scanner, true));
  ASSERT_NE(static_cast<AddressField*>(NULL), field_.get());
  EXPECT_EQ(kShippingAddress, field_->FindType());
  ASSERT_TRUE(field_->ClassifyField(&field_type_map_));
  ASSERT_TRUE(
      field_type_map_.find(ASCIIToUTF16("addr1")) != field_type_map_.end());
  EXPECT_EQ(ADDRESS_HOME_LINE1, field_type_map_[ASCIIToUTF16("addr1")]);
  ASSERT_TRUE(
      field_type_map_.find(ASCIIToUTF16("addr2")) != field_type_map_.end());
  EXPECT_EQ(ADDRESS_HOME_LINE2, field_type_map_[ASCIIToUTF16("addr2")]);
}

TEST_F(AddressFieldTest, ParseCity) {
  list_.push_back(
      new AutofillField(webkit_glue::FormField(ASCIIToUTF16("City"),
                                               ASCIIToUTF16("city"),
                                               string16(),
                                               ASCIIToUTF16("text"),
                                               0,
                                               false),
                        ASCIIToUTF16("city1")));
  AutofillScanner scanner(list_.get());
  field_.reset(Parse(&scanner, false));
  ASSERT_NE(static_cast<AddressField*>(NULL), field_.get());
  EXPECT_EQ(kGenericAddress, field_->FindType());
  ASSERT_TRUE(field_->ClassifyField(&field_type_map_));
  ASSERT_TRUE(
      field_type_map_.find(ASCIIToUTF16("city1")) != field_type_map_.end());
  EXPECT_EQ(ADDRESS_HOME_CITY, field_type_map_[ASCIIToUTF16("city1")]);
}

TEST_F(AddressFieldTest, ParseCityEcml) {
  list_.push_back(
      new AutofillField(webkit_glue::FormField(ASCIIToUTF16("City"),
                                               ASCIIToUTF16(kEcmlShipToCity),
                                               string16(),
                                               ASCIIToUTF16("text"),
                                               0,
                                               false),
                        ASCIIToUTF16("city1")));
  AutofillScanner scanner(list_.get());
  field_.reset(Parse(&scanner, true));
  ASSERT_NE(static_cast<AddressField*>(NULL), field_.get());
  EXPECT_EQ(kGenericAddress, field_->FindType());
  ASSERT_TRUE(field_->ClassifyField(&field_type_map_));
  ASSERT_TRUE(
      field_type_map_.find(ASCIIToUTF16("city1")) != field_type_map_.end());
  EXPECT_EQ(ADDRESS_HOME_CITY, field_type_map_[ASCIIToUTF16("city1")]);
}

TEST_F(AddressFieldTest, ParseState) {
  list_.push_back(
      new AutofillField(webkit_glue::FormField(ASCIIToUTF16("State"),
                                               ASCIIToUTF16("state"),
                                               string16(),
                                               ASCIIToUTF16("text"),
                                               0,
                                               false),
                        ASCIIToUTF16("state1")));
  AutofillScanner scanner(list_.get());
  field_.reset(Parse(&scanner, false));
  ASSERT_NE(static_cast<AddressField*>(NULL), field_.get());
  EXPECT_EQ(kGenericAddress, field_->FindType());
  ASSERT_TRUE(field_->ClassifyField(&field_type_map_));
  ASSERT_TRUE(
      field_type_map_.find(ASCIIToUTF16("state1")) != field_type_map_.end());
  EXPECT_EQ(ADDRESS_HOME_STATE, field_type_map_[ASCIIToUTF16("state1")]);
}

TEST_F(AddressFieldTest, ParseStateEcml) {
  list_.push_back(
      new AutofillField(
          webkit_glue::FormField(ASCIIToUTF16("State"),
                                 ASCIIToUTF16(kEcmlShipToStateProv),
                                 string16(),
                                 ASCIIToUTF16("text"),
                                 0,
                                 false),
          ASCIIToUTF16("state1")));
  AutofillScanner scanner(list_.get());
  field_.reset(Parse(&scanner, true));
  ASSERT_NE(static_cast<AddressField*>(NULL), field_.get());
  EXPECT_EQ(kGenericAddress, field_->FindType());
  ASSERT_TRUE(field_->ClassifyField(&field_type_map_));
  ASSERT_TRUE(
      field_type_map_.find(ASCIIToUTF16("state1")) != field_type_map_.end());
  EXPECT_EQ(ADDRESS_HOME_STATE, field_type_map_[ASCIIToUTF16("state1")]);
}

TEST_F(AddressFieldTest, ParseZip) {
  list_.push_back(
      new AutofillField(webkit_glue::FormField(ASCIIToUTF16("Zip"),
                                               ASCIIToUTF16("zip"),
                                               string16(),
                                               ASCIIToUTF16("text"),
                                               0,
                                               false),
                        ASCIIToUTF16("zip1")));
  AutofillScanner scanner(list_.get());
  field_.reset(Parse(&scanner, false));
  ASSERT_NE(static_cast<AddressField*>(NULL), field_.get());
  EXPECT_EQ(kGenericAddress, field_->FindType());
  ASSERT_TRUE(field_->ClassifyField(&field_type_map_));
  ASSERT_TRUE(
      field_type_map_.find(ASCIIToUTF16("zip1")) != field_type_map_.end());
  EXPECT_EQ(ADDRESS_HOME_ZIP, field_type_map_[ASCIIToUTF16("zip1")]);
}

TEST_F(AddressFieldTest, ParseZipEcml) {
  list_.push_back(
      new AutofillField(
          webkit_glue::FormField(ASCIIToUTF16("Zip"),
                                 ASCIIToUTF16(kEcmlShipToPostalCode),
                                 string16(),
                                 ASCIIToUTF16("text"),
                                 0,
                                 false),
                        ASCIIToUTF16("zip1")));
  AutofillScanner scanner(list_.get());
  field_.reset(Parse(&scanner, true));
  ASSERT_NE(static_cast<AddressField*>(NULL), field_.get());
  EXPECT_EQ(kGenericAddress, field_->FindType());
  ASSERT_TRUE(field_->ClassifyField(&field_type_map_));
  ASSERT_TRUE(
      field_type_map_.find(ASCIIToUTF16("zip1")) != field_type_map_.end());
  EXPECT_EQ(ADDRESS_HOME_ZIP, field_type_map_[ASCIIToUTF16("zip1")]);
}

TEST_F(AddressFieldTest, ParseStateAndZipOneLabel) {
  list_.push_back(
      new AutofillField(
          webkit_glue::FormField(
              ASCIIToUTF16("State/Province, Zip/Postal Code"),
              ASCIIToUTF16("state"),
              string16(),
              ASCIIToUTF16("text"),
              0,
              false),
          ASCIIToUTF16("state")));
  list_.push_back(
      new AutofillField(
          webkit_glue::FormField(
              ASCIIToUTF16("State/Province, Zip/Postal Code"),
              ASCIIToUTF16("zip"),
              string16(),
              ASCIIToUTF16("text"),
              0,
              false),
          ASCIIToUTF16("zip")));
  AutofillScanner scanner(list_.get());
  field_.reset(Parse(&scanner, false));
  ASSERT_NE(static_cast<AddressField*>(NULL), field_.get());
  EXPECT_EQ(kGenericAddress, field_->FindType());
  ASSERT_TRUE(field_->ClassifyField(&field_type_map_));
  ASSERT_TRUE(
      field_type_map_.find(ASCIIToUTF16("state")) != field_type_map_.end());
  EXPECT_EQ(ADDRESS_HOME_STATE, field_type_map_[ASCIIToUTF16("state")]);
  ASSERT_TRUE(
      field_type_map_.find(ASCIIToUTF16("zip")) != field_type_map_.end());
  EXPECT_EQ(ADDRESS_HOME_ZIP, field_type_map_[ASCIIToUTF16("zip")]);
}

TEST_F(AddressFieldTest, ParseCountry) {
  list_.push_back(
      new AutofillField(webkit_glue::FormField(ASCIIToUTF16("Country"),
                                               ASCIIToUTF16("country"),
                                               string16(),
                                               ASCIIToUTF16("text"),
                                               0,
                                               false),
                        ASCIIToUTF16("country1")));
  AutofillScanner scanner(list_.get());
  field_.reset(Parse(&scanner, false));
  ASSERT_NE(static_cast<AddressField*>(NULL), field_.get());
  EXPECT_EQ(kGenericAddress, field_->FindType());
  ASSERT_TRUE(field_->ClassifyField(&field_type_map_));
  ASSERT_TRUE(
      field_type_map_.find(ASCIIToUTF16("country1")) != field_type_map_.end());
  EXPECT_EQ(ADDRESS_HOME_COUNTRY, field_type_map_[ASCIIToUTF16("country1")]);
}

TEST_F(AddressFieldTest, ParseCountryEcml) {
  list_.push_back(
      new AutofillField(webkit_glue::FormField(ASCIIToUTF16("Country"),
                                               ASCIIToUTF16(kEcmlShipToCountry),
                                               string16(),
                                               ASCIIToUTF16("text"),
                                               0,
                                               false),
                        ASCIIToUTF16("country1")));
  AutofillScanner scanner(list_.get());
  field_.reset(Parse(&scanner, true));
  ASSERT_NE(static_cast<AddressField*>(NULL), field_.get());
  EXPECT_EQ(kGenericAddress, field_->FindType());
  ASSERT_TRUE(field_->ClassifyField(&field_type_map_));
  ASSERT_TRUE(
      field_type_map_.find(ASCIIToUTF16("country1")) != field_type_map_.end());
  EXPECT_EQ(ADDRESS_HOME_COUNTRY, field_type_map_[ASCIIToUTF16("country1")]);
}

TEST_F(AddressFieldTest, ParseTwoLineAddressMissingLabel) {
  list_.push_back(
      new AutofillField(webkit_glue::FormField(ASCIIToUTF16("Address"),
                                               ASCIIToUTF16("address"),
                                               string16(),
                                               ASCIIToUTF16("text"),
                                               0,
                                               false),
                        ASCIIToUTF16("addr1")));
  list_.push_back(
      new AutofillField(webkit_glue::FormField(string16(),
                                               ASCIIToUTF16("bogus"),
                                               string16(),
                                               ASCIIToUTF16("text"),
                                               0,
                                               false),
                        ASCIIToUTF16("addr2")));
  AutofillScanner scanner(list_.get());
  field_.reset(Parse(&scanner, false));
  ASSERT_NE(static_cast<AddressField*>(NULL), field_.get());
  EXPECT_EQ(kGenericAddress, field_->FindType());
  ASSERT_TRUE(field_->ClassifyField(&field_type_map_));
  ASSERT_TRUE(
      field_type_map_.find(ASCIIToUTF16("addr1")) != field_type_map_.end());
  EXPECT_EQ(ADDRESS_HOME_LINE1, field_type_map_[ASCIIToUTF16("addr1")]);
  ASSERT_TRUE(
      field_type_map_.find(ASCIIToUTF16("addr2")) != field_type_map_.end());
  EXPECT_EQ(ADDRESS_HOME_LINE2, field_type_map_[ASCIIToUTF16("addr2")]);
}

TEST_F(AddressFieldTest, ParseCompany) {
  list_.push_back(
      new AutofillField(webkit_glue::FormField(ASCIIToUTF16("Company"),
                                               ASCIIToUTF16("company"),
                                               string16(),
                                               ASCIIToUTF16("text"),
                                               0,
                                               false),
                        ASCIIToUTF16("company1")));
  AutofillScanner scanner(list_.get());
  field_.reset(Parse(&scanner, false));
  ASSERT_NE(static_cast<AddressField*>(NULL), field_.get());
  EXPECT_EQ(kGenericAddress, field_->FindType());
  ASSERT_TRUE(field_->ClassifyField(&field_type_map_));
  ASSERT_TRUE(
      field_type_map_.find(ASCIIToUTF16("company1")) != field_type_map_.end());
  EXPECT_EQ(COMPANY_NAME, field_type_map_[ASCIIToUTF16("company1")]);
}

TEST_F(AddressFieldTest, ParseCompanyEcml) {
  list_.push_back(
      new AutofillField(
          webkit_glue::FormField(ASCIIToUTF16("Company"),
                                 ASCIIToUTF16(kEcmlShipToCompanyName),
                                 string16(),
                                 ASCIIToUTF16("text"),
                                 0,
                                 false),
          ASCIIToUTF16("company1")));
  AutofillScanner scanner(list_.get());
  field_.reset(Parse(&scanner, true));
  ASSERT_NE(static_cast<AddressField*>(NULL), field_.get());
  EXPECT_EQ(kGenericAddress, field_->FindType());
  ASSERT_TRUE(field_->ClassifyField(&field_type_map_));
  ASSERT_TRUE(
      field_type_map_.find(ASCIIToUTF16("company1")) != field_type_map_.end());
  EXPECT_EQ(COMPANY_NAME, field_type_map_[ASCIIToUTF16("company1")]);
}
