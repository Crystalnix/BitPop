// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/basictypes.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/autofill/autofill_common_test.h"
#include "chrome/browser/autofill/autofill_profile.h"
#include "chrome/browser/autofill/form_structure.h"
#include "chrome/browser/autofill/personal_data_manager.h"
#include "chrome/browser/password_manager/encryptor.h"
#include "chrome/common/guid.h"
#include "chrome/test/testing_browser_process.h"
#include "chrome/test/testing_profile.h"
#include "content/browser/browser_thread.h"
#include "content/common/notification_details.h"
#include "content/common/notification_observer_mock.h"
#include "content/common/notification_registrar.h"
#include "content/common/notification_source.h"
#include "content/common/notification_type.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "webkit/glue/form_data.h"

using webkit_glue::FormData;

ACTION(QuitUIMessageLoop) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  MessageLoop::current()->Quit();
}

class PersonalDataLoadedObserverMock : public PersonalDataManager::Observer {
 public:
  PersonalDataLoadedObserverMock() {}
  virtual ~PersonalDataLoadedObserverMock() {}

  MOCK_METHOD0(OnPersonalDataChanged, void());
};

class PersonalDataManagerTest : public testing::Test {
 protected:
  PersonalDataManagerTest()
      : ui_thread_(BrowserThread::UI, &message_loop_),
        db_thread_(BrowserThread::DB) {
  }

  virtual void SetUp() {
    db_thread_.Start();

    profile_.reset(new TestingProfile);
    profile_->CreateWebDataService(false);

    autofill_test::DisableSystemServices(profile_.get());
    ResetPersonalDataManager();
  }

  virtual void TearDown() {
    personal_data_ = NULL;
    if (profile_.get())
      profile_.reset(NULL);

    db_thread_.Stop();
    MessageLoop::current()->PostTask(FROM_HERE, new MessageLoop::QuitTask);
    MessageLoop::current()->Run();
  }

  void ResetPersonalDataManager() {
    personal_data_ = new PersonalDataManager();
    personal_data_->Init(profile_.get());
    personal_data_->SetObserver(&personal_data_observer_);

    // Verify that the web database has been updated and the notification sent.
    EXPECT_CALL(personal_data_observer_,
                OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
    MessageLoop::current()->Run();
  }

  ScopedTestingBrowserProcess browser_process_;
  MessageLoopForUI message_loop_;
  BrowserThread ui_thread_;
  BrowserThread db_thread_;
  scoped_ptr<TestingProfile> profile_;
  scoped_refptr<PersonalDataManager> personal_data_;
  NotificationRegistrar registrar_;
  NotificationObserverMock observer_;
  PersonalDataLoadedObserverMock personal_data_observer_;
};

TEST_F(PersonalDataManagerTest, AddProfile) {
  AutofillProfile profile0;
  autofill_test::SetProfileInfo(&profile0,
      "John", "Mitchell", "Smith",
      "j@s.com", "Acme Inc.", "1 Main", "Apt A", "San Francisco", "CA",
      "94102", "USA", "4158889999", "4152223333");

  // Add profile0 to the database.
  personal_data_->AddProfile(profile0);

  // Reload the database.
  ResetPersonalDataManager();

  // Verify the addition.
  const std::vector<AutofillProfile*>& results1 = personal_data_->profiles();
  ASSERT_EQ(1U, results1.size());
  EXPECT_EQ(0, profile0.CompareMulti(*results1[0]));

  // Add profile with identical values.  Duplicates should not get saved.
  AutofillProfile profile0a = profile0;
  profile0a.set_guid(guid::GenerateGUID());
  personal_data_->AddProfile(profile0a);

  // Reload the database.
  ResetPersonalDataManager();

  // Verify the non-addition.
  const std::vector<AutofillProfile*>& results2 = personal_data_->profiles();
  ASSERT_EQ(1U, results2.size());
  EXPECT_EQ(0, profile0.CompareMulti(*results2[0]));

  // New profile with different email.
  AutofillProfile profile1 = profile0;
  profile1.set_guid(guid::GenerateGUID());
  profile1.SetInfo(EMAIL_ADDRESS, ASCIIToUTF16("john@smith.com"));

  // Add the different profile.  This should save as a separate profile.
  // Note that if this same profile was "merged" it would collapse to one
  // profile with a multi-valued entry for email.
  personal_data_->AddProfile(profile1);

  // Reload the database.
  ResetPersonalDataManager();

  // Verify the addition.
  const std::vector<AutofillProfile*>& results3 = personal_data_->profiles();
  ASSERT_EQ(2U, results3.size());
  EXPECT_EQ(0, profile0.CompareMulti(*results3[0]));
  EXPECT_EQ(0, profile1.CompareMulti(*results3[1]));
}

TEST_F(PersonalDataManagerTest, AddUpdateRemoveProfiles) {
  AutofillProfile profile0;
  autofill_test::SetProfileInfo(&profile0,
      "Marion", "Mitchell", "Morrison",
      "johnwayne@me.xyz", "Fox", "123 Zoo St.", "unit 5", "Hollywood", "CA",
      "91601", "US", "12345678910", "01987654321");

  AutofillProfile profile1;
  autofill_test::SetProfileInfo(&profile1,
      "Josephine", "Alicia", "Saenz",
      "joewayne@me.xyz", "Fox", "903 Apple Ct.", NULL, "Orlando", "FL", "32801",
      "US", "19482937549", "13502849239");

  AutofillProfile profile2;
  autofill_test::SetProfileInfo(&profile2,
      "Josephine", "Alicia", "Saenz",
      "joewayne@me.xyz", "Fox", "1212 Center.", "Bld. 5", "Orlando", "FL",
      "32801", "US", "19482937549", "13502849239");

  // Add two test profiles to the database.
  personal_data_->AddProfile(profile0);
  personal_data_->AddProfile(profile1);

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  const std::vector<AutofillProfile*>& results1 = personal_data_->profiles();
  ASSERT_EQ(2U, results1.size());
  EXPECT_EQ(0, profile0.Compare(*results1[0]));
  EXPECT_EQ(0, profile1.Compare(*results1[1]));

  // Update, remove, and add.
  profile0.SetInfo(NAME_FIRST, ASCIIToUTF16("John"));
  personal_data_->UpdateProfile(profile0);
  personal_data_->RemoveProfile(profile1.guid());
  personal_data_->AddProfile(profile2);

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  const std::vector<AutofillProfile*>& results2 = personal_data_->profiles();
  ASSERT_EQ(2U, results2.size());
  EXPECT_EQ(0, profile0.Compare(*results2[0]));
  EXPECT_EQ(0, profile2.Compare(*results2[1]));

  // Reset the PersonalDataManager.  This tests that the personal data was saved
  // to the web database, and that we can load the profiles from the web
  // database.
  ResetPersonalDataManager();

  // Verify that we've loaded the profiles from the web database.
  const std::vector<AutofillProfile*>& results3 = personal_data_->profiles();
  ASSERT_EQ(2U, results3.size());
  EXPECT_EQ(0, profile0.Compare(*results3[0]));
  EXPECT_EQ(0, profile2.Compare(*results3[1]));
}

TEST_F(PersonalDataManagerTest, AddUpdateRemoveCreditCards) {
  CreditCard credit_card0;
  autofill_test::SetCreditCardInfo(&credit_card0,
      "John Dillinger", "423456789012" /* Visa */, "01", "2010");

  CreditCard credit_card1;
  autofill_test::SetCreditCardInfo(&credit_card1,
      "Bonnie Parker", "518765432109" /* Mastercard */, "12", "2012");

  CreditCard credit_card2;
  autofill_test::SetCreditCardInfo(&credit_card2,
      "Clyde Barrow", "347666888555" /* American Express */, "04", "2015");

  // Add two test credit cards to the database.
  personal_data_->AddCreditCard(credit_card0);
  personal_data_->AddCreditCard(credit_card1);

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  const std::vector<CreditCard*>& results1 = personal_data_->credit_cards();
  ASSERT_EQ(2U, results1.size());
  EXPECT_EQ(0, credit_card0.Compare(*results1[0]));
  EXPECT_EQ(0, credit_card1.Compare(*results1[1]));

  // Update, remove, and add.
  credit_card0.SetInfo(CREDIT_CARD_NAME, ASCIIToUTF16("Joe"));
  personal_data_->UpdateCreditCard(credit_card0);
  personal_data_->RemoveCreditCard(credit_card1.guid());
  personal_data_->AddCreditCard(credit_card2);

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  const std::vector<CreditCard*>& results2 = personal_data_->credit_cards();
  ASSERT_EQ(2U, results2.size());
  EXPECT_EQ(credit_card0, *results2[0]);
  EXPECT_EQ(credit_card2, *results2[1]);

  // Reset the PersonalDataManager.  This tests that the personal data was saved
  // to the web database, and that we can load the credit cards from the web
  // database.
  ResetPersonalDataManager();

  // Verify that we've loaded the credit cards from the web database.
  const std::vector<CreditCard*>& results3 = personal_data_->credit_cards();
  ASSERT_EQ(2U, results3.size());
  EXPECT_EQ(credit_card0, *results3[0]);
  EXPECT_EQ(credit_card2, *results3[1]);
}

TEST_F(PersonalDataManagerTest, AddProfilesAndCreditCards) {
  AutofillProfile profile0;
  autofill_test::SetProfileInfo(&profile0,
      "Marion", "Mitchell", "Morrison",
      "johnwayne@me.xyz", "Fox", "123 Zoo St.", "unit 5", "Hollywood", "CA",
      "91601", "US", "12345678910", "01987654321");

  AutofillProfile profile1;
  autofill_test::SetProfileInfo(&profile1,
      "Josephine", "Alicia", "Saenz",
      "joewayne@me.xyz", "Fox", "903 Apple Ct.", NULL, "Orlando", "FL", "32801",
      "US", "19482937549", "13502849239");

  CreditCard credit_card0;
  autofill_test::SetCreditCardInfo(&credit_card0,
      "John Dillinger", "423456789012" /* Visa */, "01", "2010");

  CreditCard credit_card1;
  autofill_test::SetCreditCardInfo(&credit_card1,
      "Bonnie Parker", "518765432109" /* Mastercard */, "12", "2012");

  // Add two test profiles to the database.
  personal_data_->AddProfile(profile0);
  personal_data_->AddProfile(profile1);

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  const std::vector<AutofillProfile*>& results1 = personal_data_->profiles();
  ASSERT_EQ(2U, results1.size());
  EXPECT_EQ(0, profile0.Compare(*results1[0]));
  EXPECT_EQ(0, profile1.Compare(*results1[1]));

  // Add two test credit cards to the database.
  personal_data_->AddCreditCard(credit_card0);
  personal_data_->AddCreditCard(credit_card1);

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  const std::vector<CreditCard*>& results2 = personal_data_->credit_cards();
  ASSERT_EQ(2U, results2.size());
  EXPECT_EQ(credit_card0, *results2[0]);
  EXPECT_EQ(credit_card1, *results2[1]);

  // Determine uniqueness by inserting all of the GUIDs into a set and verifying
  // the size of the set matches the number of GUIDs.
  std::set<std::string> guids;
  guids.insert(profile0.guid());
  guids.insert(profile1.guid());
  guids.insert(credit_card0.guid());
  guids.insert(credit_card1.guid());
  EXPECT_EQ(4U, guids.size());
}

// Test for http://crbug.com/50047. Makes sure that guids are populated
// correctly on load.
TEST_F(PersonalDataManagerTest, PopulateUniqueIDsOnLoad) {
  AutofillProfile profile0;
  autofill_test::SetProfileInfo(&profile0,
      "y", "", "", "", "", "", "", "", "", "", "", "", "");

  // Add the profile0 to the db.
  personal_data_->AddProfile(profile0);

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  // Verify that we've loaded the profiles from the web database.
  const std::vector<AutofillProfile*>& results2 = personal_data_->profiles();
  ASSERT_EQ(1U, results2.size());
  EXPECT_EQ(0, profile0.Compare(*results2[0]));

  // Add a new profile.
  AutofillProfile profile1;
  autofill_test::SetProfileInfo(&profile1,
      "z", "", "", "", "", "", "", "", "", "", "", "", "");
  personal_data_->AddProfile(profile1);

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  // Make sure the two profiles have different GUIDs, both valid.
  const std::vector<AutofillProfile*>& results3 = personal_data_->profiles();
  ASSERT_EQ(2U, results3.size());
  EXPECT_NE(results3[0]->guid(), results3[1]->guid());
  EXPECT_TRUE(guid::IsValidGUID(results3[0]->guid()));
  EXPECT_TRUE(guid::IsValidGUID(results3[1]->guid()));
}

TEST_F(PersonalDataManagerTest, SetEmptyProfile) {
  AutofillProfile profile0;
  autofill_test::SetProfileInfo(&profile0,
      "", "", "", "", "", "", "", "", "", "", "", "", "");

  // Add the empty profile to the database.
  personal_data_->AddProfile(profile0);

  // Note: no refresh here.

  // Reset the PersonalDataManager.  This tests that the personal data was saved
  // to the web database, and that we can load the profiles from the web
  // database.
  ResetPersonalDataManager();

  // Verify that we've loaded the profiles from the web database.
  const std::vector<AutofillProfile*>& results2 = personal_data_->profiles();
  ASSERT_EQ(0U, results2.size());
}

TEST_F(PersonalDataManagerTest, SetEmptyCreditCard) {
  CreditCard credit_card0;
  autofill_test::SetCreditCardInfo(&credit_card0, "", "", "", "");

  // Add the empty credit card to the database.
  personal_data_->AddCreditCard(credit_card0);

  // Note: no refresh here.

  // Reset the PersonalDataManager.  This tests that the personal data was saved
  // to the web database, and that we can load the credit cards from the web
  // database.
  ResetPersonalDataManager();

  // Verify that we've loaded the credit cards from the web database.
  const std::vector<CreditCard*>& results2 = personal_data_->credit_cards();
  ASSERT_EQ(0U, results2.size());
}

TEST_F(PersonalDataManagerTest, Refresh) {
  AutofillProfile profile0;
  autofill_test::SetProfileInfo(&profile0,
      "Marion", "Mitchell", "Morrison",
      "johnwayne@me.xyz", "Fox", "123 Zoo St.", "unit 5", "Hollywood", "CA",
      "91601", "US", "12345678910", "01987654321");

  AutofillProfile profile1;
  autofill_test::SetProfileInfo(&profile1,
      "Josephine", "Alicia", "Saenz",
      "joewayne@me.xyz", "Fox", "903 Apple Ct.", NULL, "Orlando", "FL", "32801",
      "US", "19482937549", "13502849239");

  // Add the test profiles to the database.
  personal_data_->AddProfile(profile0);
  personal_data_->AddProfile(profile1);

  // Labels depend on other profiles in the list - update labels manually.
  std::vector<AutofillProfile *> profile_pointers;
  profile_pointers.push_back(&profile0);
  profile_pointers.push_back(&profile1);
  AutofillProfile::AdjustInferredLabels(&profile_pointers);

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  const std::vector<AutofillProfile*>& results1 = personal_data_->profiles();
  ASSERT_EQ(2U, results1.size());
  EXPECT_EQ(profile0, *results1[0]);
  EXPECT_EQ(profile1, *results1[1]);

  AutofillProfile profile2;
  autofill_test::SetProfileInfo(&profile2,
      "Josephine", "Alicia", "Saenz",
      "joewayne@me.xyz", "Fox", "1212 Center.", "Bld. 5", "Orlando", "FL",
      "32801", "US", "19482937549", "13502849239");

  // Adjust all labels.
  profile_pointers.push_back(&profile2);
  AutofillProfile::AdjustInferredLabels(&profile_pointers);

  WebDataService* wds = profile_->GetWebDataService(Profile::EXPLICIT_ACCESS);
  ASSERT_TRUE(wds);
  wds->AddAutofillProfile(profile2);

  personal_data_->Refresh();

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  const std::vector<AutofillProfile*>& results2 = personal_data_->profiles();
  ASSERT_EQ(3U, results2.size());
  EXPECT_EQ(profile0, *results2[0]);
  EXPECT_EQ(profile1, *results2[1]);
  EXPECT_EQ(profile2, *results2[2]);

  wds->RemoveAutofillProfile(profile1.guid());
  wds->RemoveAutofillProfile(profile2.guid());

  // Before telling the PDM to refresh, simulate an edit to one of the profiles
  // via a SetProfile update (this would happen if the Autofill window was
  // open with a previous snapshot of the profiles, and something [e.g. sync]
  // removed a profile from the browser.  In this edge case, we will end up
  // in a consistent state by dropping the write).
  profile2.SetInfo(NAME_FIRST, ASCIIToUTF16("Jo"));
  personal_data_->UpdateProfile(profile0);
  personal_data_->AddProfile(profile1);
  personal_data_->AddProfile(profile2);

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  const std::vector<AutofillProfile*>& results3 = personal_data_->profiles();
  ASSERT_EQ(1U, results3.size());
  EXPECT_EQ(profile0, *results2[0]);
}

TEST_F(PersonalDataManagerTest, ImportFormData) {
  FormData form;
  webkit_glue::FormField field;
  autofill_test::CreateTestFormField(
      "First name:", "first_name", "George", "text", &field);
  form.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Last name:", "last_name", "Washington", "text", &field);
  form.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Email:", "email", "theprez@gmail.com", "text", &field);
  form.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Address:", "address1", "21 Laussat St", "text", &field);
  form.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "City:", "city", "San Francisco", "text", &field);
  form.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "State:", "state", "California", "text", &field);
  form.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Zip:", "zip", "94102", "text", &field);
  form.fields.push_back(field);
  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes();
  const CreditCard* imported_credit_card;
  EXPECT_TRUE(personal_data_->ImportFormData(form_structure,
                                             &imported_credit_card));
  ASSERT_FALSE(imported_credit_card);

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  AutofillProfile expected;
  autofill_test::SetProfileInfo(&expected, "George", NULL,
      "Washington", "theprez@gmail.com", NULL, "21 Laussat St", NULL,
      "San Francisco", "California", "94102", NULL, NULL, NULL);
  const std::vector<AutofillProfile*>& results = personal_data_->profiles();
  ASSERT_EQ(1U, results.size());
  EXPECT_EQ(0, expected.Compare(*results[0]));
}

TEST_F(PersonalDataManagerTest, ImportFormDataBadEmail) {
  FormData form;
  webkit_glue::FormField field;
  autofill_test::CreateTestFormField(
      "First name:", "first_name", "George", "text", &field);
  form.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Last name:", "last_name", "Washington", "text", &field);
  form.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Email:", "email", "bogus", "text", &field);
  form.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Address:", "address1", "21 Laussat St", "text", &field);
  form.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "City:", "city", "San Francisco", "text", &field);
  form.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "State:", "state", "California", "text", &field);
  form.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Zip:", "zip", "94102", "text", &field);
  form.fields.push_back(field);
  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes();
  const CreditCard* imported_credit_card;
  EXPECT_FALSE(personal_data_->ImportFormData(form_structure,
                                              &imported_credit_card));
  ASSERT_EQ(static_cast<CreditCard*>(NULL), imported_credit_card);

  const std::vector<AutofillProfile*>& results = personal_data_->profiles();
  ASSERT_EQ(0U, results.size());
}

TEST_F(PersonalDataManagerTest, ImportFormDataNotEnoughFilledFields) {
  FormData form;
  webkit_glue::FormField field;
  autofill_test::CreateTestFormField(
      "First name:", "first_name", "George", "text", &field);
  form.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Last name:", "last_name", "Washington", "text", &field);
  form.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Card number:", "card_number", "4111 1111 1111 1111", "text", &field);
  form.fields.push_back(field);
  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes();
  const CreditCard* imported_credit_card;
  EXPECT_FALSE(personal_data_->ImportFormData(form_structure,
                                              &imported_credit_card));
  ASSERT_FALSE(imported_credit_card);

  const std::vector<AutofillProfile*>& profiles = personal_data_->profiles();
  ASSERT_EQ(0U, profiles.size());
  const std::vector<CreditCard*>& credit_cards = personal_data_->credit_cards();
  ASSERT_EQ(0U, credit_cards.size());
}

TEST_F(PersonalDataManagerTest, ImportPhoneNumberSplitAcrossMultipleFields) {
  FormData form;
  webkit_glue::FormField field;
  autofill_test::CreateTestFormField(
      "First name:", "first_name", "George", "text", &field);
  form.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Last name:", "last_name", "Washington", "text", &field);
  form.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Phone #:", "home_phone_area_code", "650", "text", &field);
  field.max_length = 3;
  form.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Phone #:", "home_phone_prefix", "555", "text", &field);
  field.max_length = 3;
  form.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Phone #:", "home_phone_suffix", "0000", "text", &field);
  field.max_length = 4;
  form.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Address:", "address1", "21 Laussat St", "text", &field);
  form.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "City:", "city", "San Francisco", "text", &field);
  form.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "State:", "state", "California", "text", &field);
  form.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Zip:", "zip", "94102", "text", &field);
  form.fields.push_back(field);
  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes();
  const CreditCard* imported_credit_card;
  EXPECT_TRUE(personal_data_->ImportFormData(form_structure,
                                             &imported_credit_card));
  ASSERT_FALSE(imported_credit_card);

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  AutofillProfile expected;
  autofill_test::SetProfileInfo(&expected, "George", NULL,
      "Washington", NULL, NULL, "21 Laussat St", NULL,
      "San Francisco", "California", "94102", NULL, "6505550000", NULL);
  const std::vector<AutofillProfile*>& results = personal_data_->profiles();
  ASSERT_EQ(1U, results.size());
  EXPECT_EQ(0, expected.Compare(*results[0]));
}

TEST_F(PersonalDataManagerTest, SetUniqueCreditCardLabels) {
  CreditCard credit_card0;
  credit_card0.SetInfo(CREDIT_CARD_NAME, ASCIIToUTF16("John"));
  CreditCard credit_card1;
  credit_card1.SetInfo(CREDIT_CARD_NAME, ASCIIToUTF16("Paul"));
  CreditCard credit_card2;
  credit_card2.SetInfo(CREDIT_CARD_NAME, ASCIIToUTF16("Ringo"));
  CreditCard credit_card3;
  credit_card3.SetInfo(CREDIT_CARD_NAME, ASCIIToUTF16("Other"));
  CreditCard credit_card4;
  credit_card4.SetInfo(CREDIT_CARD_NAME, ASCIIToUTF16("Ozzy"));
  CreditCard credit_card5;
  credit_card5.SetInfo(CREDIT_CARD_NAME, ASCIIToUTF16("Dio"));

  // Add the test credit cards to the database.
  personal_data_->AddCreditCard(credit_card0);
  personal_data_->AddCreditCard(credit_card1);
  personal_data_->AddCreditCard(credit_card2);
  personal_data_->AddCreditCard(credit_card3);
  personal_data_->AddCreditCard(credit_card4);
  personal_data_->AddCreditCard(credit_card5);

  // Reset the PersonalDataManager.  This tests that the personal data was saved
  // to the web database, and that we can load the credit cards from the web
  // database.
  ResetPersonalDataManager();

  const std::vector<CreditCard*>& results = personal_data_->credit_cards();
  ASSERT_EQ(6U, results.size());
  EXPECT_EQ(credit_card0.guid(), results[0]->guid());
  EXPECT_EQ(credit_card1.guid(), results[1]->guid());
  EXPECT_EQ(credit_card2.guid(), results[2]->guid());
  EXPECT_EQ(credit_card3.guid(), results[3]->guid());
  EXPECT_EQ(credit_card4.guid(), results[4]->guid());
  EXPECT_EQ(credit_card5.guid(), results[5]->guid());
}

TEST_F(PersonalDataManagerTest, AggregateTwoDifferentProfiles) {
  FormData form1;
  webkit_glue::FormField field;
  autofill_test::CreateTestFormField(
      "First name:", "first_name", "George", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Last name:", "last_name", "Washington", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Email:", "email", "theprez@gmail.com", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Address:", "address1", "21 Laussat St", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "City:", "city", "San Francisco", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "State:", "state", "California", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Zip:", "zip", "94102", "text", &field);
  form1.fields.push_back(field);

  FormStructure form_structure1(form1);
  form_structure1.DetermineHeuristicTypes();
  const CreditCard* imported_credit_card;
  EXPECT_TRUE(personal_data_->ImportFormData(form_structure1,
                                             &imported_credit_card));
  ASSERT_FALSE(imported_credit_card);

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  AutofillProfile expected;
  autofill_test::SetProfileInfo(&expected, "George", NULL,
      "Washington", "theprez@gmail.com", NULL, "21 Laussat St", NULL,
      "San Francisco", "California", "94102", NULL, NULL, NULL);
  const std::vector<AutofillProfile*>& results1 = personal_data_->profiles();
  ASSERT_EQ(1U, results1.size());
  EXPECT_EQ(0, expected.Compare(*results1[0]));

  // Now create a completely different profile.
  FormData form2;
  autofill_test::CreateTestFormField(
      "First name:", "first_name", "John", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Last name:", "last_name", "Adams", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Email:", "email", "second@gmail.com", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Address:", "address1", "22 Laussat St", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "City:", "city", "San Francisco", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "State:", "state", "California", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Zip:", "zip", "94102", "text", &field);
  form2.fields.push_back(field);

  FormStructure form_structure2(form2);
  form_structure2.DetermineHeuristicTypes();
  EXPECT_TRUE(personal_data_->ImportFormData(form_structure2,
                                             &imported_credit_card));
  ASSERT_FALSE(imported_credit_card);

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  const std::vector<AutofillProfile*>& results2 = personal_data_->profiles();

  AutofillProfile expected2;
  autofill_test::SetProfileInfo(&expected2, "John", NULL,
      "Adams", "second@gmail.com", NULL, "22 Laussat St", NULL,
      "San Francisco", "California", "94102", NULL, NULL, NULL);
  ASSERT_EQ(2U, results2.size());
  EXPECT_EQ(0, expected.Compare(*results2[0]));
  EXPECT_EQ(0, expected2.Compare(*results2[1]));
}

TEST_F(PersonalDataManagerTest, AggregateTwoProfilesWithMultiValue) {
  FormData form1;
  webkit_glue::FormField field;
  autofill_test::CreateTestFormField(
      "First name:", "first_name", "George", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Last name:", "last_name", "Washington", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Email:", "email", "theprez@gmail.com", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Address:", "address1", "21 Laussat St", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "City:", "city", "San Francisco", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "State:", "state", "California", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Zip:", "zip", "94102", "text", &field);
  form1.fields.push_back(field);

  FormStructure form_structure1(form1);
  form_structure1.DetermineHeuristicTypes();
  const CreditCard* imported_credit_card;
  EXPECT_TRUE(personal_data_->ImportFormData(form_structure1,
                                             &imported_credit_card));
  ASSERT_FALSE(imported_credit_card);

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  AutofillProfile expected;
  autofill_test::SetProfileInfo(&expected, "George", NULL,
      "Washington", "theprez@gmail.com", NULL, "21 Laussat St", NULL,
      "San Francisco", "California", "94102", NULL, NULL, NULL);
  const std::vector<AutofillProfile*>& results1 = personal_data_->profiles();
  ASSERT_EQ(1U, results1.size());
  EXPECT_EQ(0, expected.Compare(*results1[0]));

  // Now create a completely different profile.
  FormData form2;
  autofill_test::CreateTestFormField(
      "First name:", "first_name", "John", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Last name:", "last_name", "Adams", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Email:", "email", "second@gmail.com", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Address:", "address1", "21 Laussat St", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "City:", "city", "San Francisco", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "State:", "state", "California", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Zip:", "zip", "94102", "text", &field);
  form2.fields.push_back(field);

  FormStructure form_structure2(form2);
  form_structure2.DetermineHeuristicTypes();
  EXPECT_TRUE(personal_data_->ImportFormData(form_structure2,
                                             &imported_credit_card));
  ASSERT_FALSE(imported_credit_card);

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  const std::vector<AutofillProfile*>& results2 = personal_data_->profiles();

  // Modify expected to include multi-valued fields.
  std::vector<string16> values;
  expected.GetMultiInfo(NAME_FULL, &values);
  values.push_back(ASCIIToUTF16("John Adams"));
  expected.SetMultiInfo(NAME_FULL, values);
  expected.GetMultiInfo(EMAIL_ADDRESS, &values);
  values.push_back(ASCIIToUTF16("second@gmail.com"));
  expected.SetMultiInfo(EMAIL_ADDRESS, values);

  ASSERT_EQ(1U, results2.size());
  EXPECT_EQ(0, expected.CompareMulti(*results2[0]));
}

TEST_F(PersonalDataManagerTest, AggregateSameProfileWithConflict) {
  FormData form1;
  webkit_glue::FormField field;
  autofill_test::CreateTestFormField(
      "First name:", "first_name", "George", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Last name:", "last_name", "Washington", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Address:", "address", "1600 Pennsylvania Avenue", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Address Line 2:", "address2", "Suite A", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "City:", "city", "San Francisco", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "State:", "state", "California", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Zip:", "zip", "94102", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Email:", "email", "theprez@gmail.com", "text", &field);
  form1.fields.push_back(field);
  // Phone gets updated.
  autofill_test::CreateTestFormField(
      "Phone:", "phone", "6505556666", "text", &field);
  form1.fields.push_back(field);

  FormStructure form_structure1(form1);
  form_structure1.DetermineHeuristicTypes();
  const CreditCard* imported_credit_card;
  EXPECT_TRUE(personal_data_->ImportFormData(form_structure1,
                                             &imported_credit_card));
  ASSERT_FALSE(imported_credit_card);

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  AutofillProfile expected;
  autofill_test::SetProfileInfo(&expected, "George", NULL,
      "Washington", "theprez@gmail.com", NULL, "1600 Pennsylvania Avenue",
      "Suite A", "San Francisco", "California", "94102", NULL, "4085556666",
      NULL);
  const std::vector<AutofillProfile*>& results1 = personal_data_->profiles();
  ASSERT_EQ(1U, results1.size());
  EXPECT_EQ(0, expected.Compare(*results1[0]));

  // Now create an updated profile.
  FormData form2;
  autofill_test::CreateTestFormField(
      "First name:", "first_name", "George", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Last name:", "last_name", "Washington", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Address:", "address", "1600 Pennsylvania Avenue", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Address Line 2:", "address2", "Suite A", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "City:", "city", "San Francisco", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "State:", "state", "California", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Zip:", "zip", "94102", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Email:", "email", "theprez@gmail.com", "text", &field);
  form2.fields.push_back(field);
  // Country gets added.
  autofill_test::CreateTestFormField(
      "Country:", "country", "USA", "text", &field);
  form2.fields.push_back(field);
  // Phone gets updated.
  autofill_test::CreateTestFormField(
      "Phone:", "phone", "6502231234", "text", &field);
  form2.fields.push_back(field);

  FormStructure form_structure2(form2);
  form_structure2.DetermineHeuristicTypes();
  EXPECT_TRUE(personal_data_->ImportFormData(form_structure2,
                                             &imported_credit_card));
  ASSERT_FALSE(imported_credit_card);

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  const std::vector<AutofillProfile*>& results2 = personal_data_->profiles();

  // Add multi-valued phone number to expectation.  Also, country gets added.
  std::vector<string16> values;
  expected.GetMultiInfo(PHONE_HOME_WHOLE_NUMBER, &values);
  values.push_back(ASCIIToUTF16("6502231234"));
  expected.SetMultiInfo(PHONE_HOME_WHOLE_NUMBER, values);
  expected.SetInfo(ADDRESS_HOME_COUNTRY, ASCIIToUTF16("United States"));
  ASSERT_EQ(1U, results2.size());
  EXPECT_EQ(0, expected.CompareMulti(*results2[0]));
}

TEST_F(PersonalDataManagerTest, AggregateProfileWithMissingInfoInOld) {
  FormData form1;
  webkit_glue::FormField field;
  autofill_test::CreateTestFormField(
      "First name:", "first_name", "George", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Last name:", "last_name", "Washington", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Address Line 1:", "address", "190 High Street", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "City:", "city", "Philadelphia", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "State:", "state", "Pennsylvania", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Zip:", "zipcode", "19106", "text", &field);
  form1.fields.push_back(field);

  FormStructure form_structure1(form1);
  form_structure1.DetermineHeuristicTypes();
  const CreditCard* imported_credit_card;
  EXPECT_TRUE(personal_data_->ImportFormData(form_structure1,
                                             &imported_credit_card));
  ASSERT_FALSE(imported_credit_card);

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  AutofillProfile expected;
  autofill_test::SetProfileInfo(&expected, "George", NULL,
      "Washington", NULL, NULL, "190 High Street", NULL,
      "Philadelphia", "Pennsylvania", "19106", NULL, NULL, NULL);
  const std::vector<AutofillProfile*>& results1 = personal_data_->profiles();
  ASSERT_EQ(1U, results1.size());
  EXPECT_EQ(0, expected.Compare(*results1[0]));

  // Submit a form with new data for the first profile.
  FormData form2;
  autofill_test::CreateTestFormField(
      "First name:", "first_name", "George", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Last name:", "last_name", "Washington", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Email:", "email", "theprez@gmail.com", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Address Line 1:", "address", "190 High Street", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "City:", "city", "Philadelphia", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "State:", "state", "Pennsylvania", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Zip:", "zipcode", "19106", "text", &field);
  form2.fields.push_back(field);

  FormStructure form_structure2(form2);
  form_structure2.DetermineHeuristicTypes();
  EXPECT_TRUE(personal_data_->ImportFormData(form_structure2,
                                             &imported_credit_card));
  ASSERT_FALSE(imported_credit_card);

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  const std::vector<AutofillProfile*>& results2 = personal_data_->profiles();

  AutofillProfile expected2;
  autofill_test::SetProfileInfo(&expected2, "George", NULL,
      "Washington", "theprez@gmail.com", NULL, "190 High Street", NULL,
      "Philadelphia", "Pennsylvania", "19106", NULL, NULL, NULL);
  ASSERT_EQ(1U, results2.size());
  EXPECT_EQ(0, expected2.Compare(*results2[0]));
}

TEST_F(PersonalDataManagerTest, AggregateProfileWithMissingInfoInNew) {
  FormData form1;
  webkit_glue::FormField field;
  autofill_test::CreateTestFormField(
      "First name:", "first_name", "George", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Last name:", "last_name", "Washington", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Company:", "company", "Government", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Email:", "email", "theprez@gmail.com", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Address Line 1:", "address", "190 High Street", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "City:", "city", "Philadelphia", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "State:", "state", "Pennsylvania", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Zip:", "zipcode", "19106", "text", &field);
  form1.fields.push_back(field);

  FormStructure form_structure1(form1);
  form_structure1.DetermineHeuristicTypes();
  const CreditCard* imported_credit_card;
  EXPECT_TRUE(personal_data_->ImportFormData(form_structure1,
                                             &imported_credit_card));
  ASSERT_FALSE(imported_credit_card);

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  AutofillProfile expected;
  autofill_test::SetProfileInfo(&expected, "George", NULL,
      "Washington", "theprez@gmail.com", "Government", "190 High Street", NULL,
      "Philadelphia", "Pennsylvania", "19106", NULL, NULL, NULL);
  const std::vector<AutofillProfile*>& results1 = personal_data_->profiles();
  ASSERT_EQ(1U, results1.size());
  EXPECT_EQ(0, expected.Compare(*results1[0]));

  // Submit a form with new data for the first profile.
  FormData form2;
  autofill_test::CreateTestFormField(
      "First name:", "first_name", "George", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Last name:", "last_name", "Washington", "text", &field);
  form2.fields.push_back(field);
  // Note missing Company field.
  autofill_test::CreateTestFormField(
      "Email:", "email", "theprez@gmail.com", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Address Line 1:", "address", "190 High Street", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "City:", "city", "Philadelphia", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "State:", "state", "Pennsylvania", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Zip:", "zipcode", "19106", "text", &field);
  form2.fields.push_back(field);

  FormStructure form_structure2(form2);
  form_structure2.DetermineHeuristicTypes();
  EXPECT_TRUE(personal_data_->ImportFormData(form_structure2,
                                             &imported_credit_card));
  ASSERT_FALSE(imported_credit_card);

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  const std::vector<AutofillProfile*>& results2 = personal_data_->profiles();

  // Expect no change.
  ASSERT_EQ(1U, results2.size());
  EXPECT_EQ(0, expected.Compare(*results2[0]));
}

TEST_F(PersonalDataManagerTest, AggregateProfileWithInsufficientAddress) {
  FormData form1;
  webkit_glue::FormField field;
  autofill_test::CreateTestFormField(
      "First name:", "first_name", "George", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Last name:", "last_name", "Washington", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Company:", "company", "Government", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Email:", "email", "theprez@gmail.com", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Address Line 1:", "address", "190 High Street", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "City:", "city", "Philadelphia", "text", &field);
  form1.fields.push_back(field);

  FormStructure form_structure1(form1);
  form_structure1.DetermineHeuristicTypes();
  const CreditCard* imported_credit_card;
  EXPECT_FALSE(personal_data_->ImportFormData(form_structure1,
                                              &imported_credit_card));
  ASSERT_FALSE(imported_credit_card);

  // Note: no refresh here.

  const std::vector<AutofillProfile*>& profiles = personal_data_->profiles();
  ASSERT_EQ(0U, profiles.size());
  const std::vector<CreditCard*>& credit_cards = personal_data_->credit_cards();
  ASSERT_EQ(0U, credit_cards.size());
}

TEST_F(PersonalDataManagerTest, AggregateTwoDifferentCreditCards) {
  FormData form1;

  // Start with a single valid credit card form.
  webkit_glue::FormField field;
  autofill_test::CreateTestFormField(
      "Name on card:", "name_on_card", "Biggie Smalls", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Card Number:", "card_number", "4111-1111-1111-1111", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Exp Month:", "exp_month", "01", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Exp Year:", "exp_year", "2011", "text", &field);
  form1.fields.push_back(field);

  FormStructure form_structure1(form1);
  form_structure1.DetermineHeuristicTypes();
  const CreditCard* imported_credit_card;
  EXPECT_TRUE(personal_data_->ImportFormData(form_structure1,
                                             &imported_credit_card));
  ASSERT_TRUE(imported_credit_card);
  personal_data_->SaveImportedCreditCard(*imported_credit_card);
  delete imported_credit_card;

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  CreditCard expected;
  autofill_test::SetCreditCardInfo(&expected,
      "Biggie Smalls", "4111111111111111", "01", "2011");
  const std::vector<CreditCard*>& results = personal_data_->credit_cards();
  ASSERT_EQ(1U, results.size());
  EXPECT_EQ(0, expected.Compare(*results[0]));

  // Add a second different valid credit card.
  FormData form2;
  autofill_test::CreateTestFormField(
      "Name on card:", "name_on_card", "Jim Johansen", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Card Number:", "card_number", "5500 0000 0000 0004", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Exp Month:", "exp_month", "02", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Exp Year:", "exp_year", "2012", "text", &field);
  form2.fields.push_back(field);

  FormStructure form_structure2(form2);
  form_structure2.DetermineHeuristicTypes();
  EXPECT_TRUE(personal_data_->ImportFormData(form_structure2,
                                             &imported_credit_card));
  ASSERT_TRUE(imported_credit_card);
  personal_data_->SaveImportedCreditCard(*imported_credit_card);
  delete imported_credit_card;

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  CreditCard expected2;
  autofill_test::SetCreditCardInfo(&expected2,
      "Jim Johansen", "5500000000000004", "02", "2012");
  const std::vector<CreditCard*>& results2 = personal_data_->credit_cards();
  ASSERT_EQ(2U, results2.size());
  EXPECT_EQ(0, expected.Compare(*results2[0]));
  EXPECT_EQ(0, expected2.Compare(*results2[1]));
}

TEST_F(PersonalDataManagerTest, AggregateInvalidCreditCard) {
  FormData form1;

  // Start with a single valid credit card form.
  webkit_glue::FormField field;
  autofill_test::CreateTestFormField(
      "Name on card:", "name_on_card", "Biggie Smalls", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Card Number:", "card_number", "4111-1111-1111-1111", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Exp Month:", "exp_month", "01", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Exp Year:", "exp_year", "2011", "text", &field);
  form1.fields.push_back(field);

  FormStructure form_structure1(form1);
  form_structure1.DetermineHeuristicTypes();
  const CreditCard* imported_credit_card;
  EXPECT_TRUE(personal_data_->ImportFormData(form_structure1,
                                             &imported_credit_card));
  ASSERT_TRUE(imported_credit_card);
  personal_data_->SaveImportedCreditCard(*imported_credit_card);
  delete imported_credit_card;

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  CreditCard expected;
  autofill_test::SetCreditCardInfo(&expected,
      "Biggie Smalls", "4111111111111111", "01", "2011");
  const std::vector<CreditCard*>& results = personal_data_->credit_cards();
  ASSERT_EQ(1U, results.size());
  EXPECT_EQ(0, expected.Compare(*results[0]));

  // Add a second different invalid credit card.
  FormData form2;
  autofill_test::CreateTestFormField(
      "Name on card:", "name_on_card", "Jim Johansen", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Card Number:", "card_number", "1000000000000000", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Exp Month:", "exp_month", "02", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Exp Year:", "exp_year", "2012", "text", &field);
  form2.fields.push_back(field);

  FormStructure form_structure2(form2);
  form_structure2.DetermineHeuristicTypes();
  EXPECT_FALSE(personal_data_->ImportFormData(form_structure2,
                                              &imported_credit_card));
  ASSERT_FALSE(imported_credit_card);

  // Note: no refresh here.

  const std::vector<CreditCard*>& results2 = personal_data_->credit_cards();
  ASSERT_EQ(1U, results2.size());
  EXPECT_EQ(0, expected.Compare(*results2[0]));
}

TEST_F(PersonalDataManagerTest, AggregateSameCreditCardWithConflict) {
  FormData form1;

  // Start with a single valid credit card form.
  webkit_glue::FormField field;
  autofill_test::CreateTestFormField(
      "Name on card:", "name_on_card", "Biggie Smalls", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Card Number:", "card_number", "4111-1111-1111-1111", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Exp Month:", "exp_month", "01", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Exp Year:", "exp_year", "2011", "text", &field);
  form1.fields.push_back(field);

  FormStructure form_structure1(form1);
  form_structure1.DetermineHeuristicTypes();
  const CreditCard* imported_credit_card;
  EXPECT_TRUE(personal_data_->ImportFormData(form_structure1,
                                             &imported_credit_card));
  ASSERT_TRUE(imported_credit_card);
  personal_data_->SaveImportedCreditCard(*imported_credit_card);
  delete imported_credit_card;

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  CreditCard expected;
  autofill_test::SetCreditCardInfo(&expected,
      "Biggie Smalls", "4111111111111111", "01", "2011");
  const std::vector<CreditCard*>& results = personal_data_->credit_cards();
  ASSERT_EQ(1U, results.size());
  EXPECT_EQ(0, expected.Compare(*results[0]));

  // Add a second different valid credit card where the year is different but
  // the credit card number matches.
  FormData form2;
  autofill_test::CreateTestFormField(
      "Name on card:", "name_on_card", "Biggie Smalls", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Card Number:", "card_number", "4111 1111 1111 1111", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Exp Month:", "exp_month", "01", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Exp Year:", "exp_year", "2012", "text", &field);
  form2.fields.push_back(field);

  FormStructure form_structure2(form2);
  form_structure2.DetermineHeuristicTypes();
  EXPECT_TRUE(personal_data_->ImportFormData(form_structure2,
                                             &imported_credit_card));
  ASSERT_TRUE(imported_credit_card);
  personal_data_->SaveImportedCreditCard(*imported_credit_card);
  delete imported_credit_card;

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  // Expect that the newer information is saved.  In this case the year is
  // updated to "2012".
  CreditCard expected2;
  autofill_test::SetCreditCardInfo(&expected2,
      "Biggie Smalls", "4111111111111111", "01", "2012");
  const std::vector<CreditCard*>& results2 = personal_data_->credit_cards();
  ASSERT_EQ(1U, results2.size());
  EXPECT_EQ(0, expected2.Compare(*results2[0]));
}

TEST_F(PersonalDataManagerTest, AggregateEmptyCreditCardWithConflict) {
  FormData form1;

  // Start with a single valid credit card form.
  webkit_glue::FormField field;
  autofill_test::CreateTestFormField(
      "Name on card:", "name_on_card", "Biggie Smalls", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Card Number:", "card_number", "4111-1111-1111-1111", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Exp Month:", "exp_month", "01", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Exp Year:", "exp_year", "2011", "text", &field);
  form1.fields.push_back(field);

  FormStructure form_structure1(form1);
  form_structure1.DetermineHeuristicTypes();
  const CreditCard* imported_credit_card;
  EXPECT_TRUE(personal_data_->ImportFormData(form_structure1,
                                             &imported_credit_card));
  ASSERT_TRUE(imported_credit_card);
  personal_data_->SaveImportedCreditCard(*imported_credit_card);
  delete imported_credit_card;

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  CreditCard expected;
  autofill_test::SetCreditCardInfo(&expected,
      "Biggie Smalls", "4111111111111111", "01", "2011");
  const std::vector<CreditCard*>& results = personal_data_->credit_cards();
  ASSERT_EQ(1U, results.size());
  EXPECT_EQ(0, expected.Compare(*results[0]));

  // Add a second credit card with no number.
  FormData form2;
  autofill_test::CreateTestFormField(
      "Name on card:", "name_on_card", "Biggie Smalls", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Exp Month:", "exp_month", "01", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Exp Year:", "exp_year", "2012", "text", &field);
  form2.fields.push_back(field);

  FormStructure form_structure2(form2);
  form_structure2.DetermineHeuristicTypes();
  EXPECT_FALSE(personal_data_->ImportFormData(form_structure2,
                                              &imported_credit_card));
  EXPECT_FALSE(imported_credit_card);

  // Note: no refresh here.

  // No change is expected.
  CreditCard expected2;
  autofill_test::SetCreditCardInfo(&expected2,
      "Biggie Smalls", "4111111111111111", "01", "2011");
  const std::vector<CreditCard*>& results2 = personal_data_->credit_cards();
  ASSERT_EQ(1U, results2.size());
  EXPECT_EQ(0, expected2.Compare(*results2[0]));
}

TEST_F(PersonalDataManagerTest, AggregateCreditCardWithMissingInfoInNew) {
  FormData form1;

  // Start with a single valid credit card form.
  webkit_glue::FormField field;
  autofill_test::CreateTestFormField(
      "Name on card:", "name_on_card", "Biggie Smalls", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Card Number:", "card_number", "4111-1111-1111-1111", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Exp Month:", "exp_month", "01", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Exp Year:", "exp_year", "2011", "text", &field);
  form1.fields.push_back(field);

  FormStructure form_structure1(form1);
  form_structure1.DetermineHeuristicTypes();
  const CreditCard* imported_credit_card;
  EXPECT_TRUE(personal_data_->ImportFormData(form_structure1,
                                             &imported_credit_card));
  ASSERT_TRUE(imported_credit_card);
  personal_data_->SaveImportedCreditCard(*imported_credit_card);
  delete imported_credit_card;

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  CreditCard expected;
  autofill_test::SetCreditCardInfo(&expected,
      "Biggie Smalls", "4111111111111111", "01", "2011");
  const std::vector<CreditCard*>& results = personal_data_->credit_cards();
  ASSERT_EQ(1U, results.size());
  EXPECT_EQ(0, expected.Compare(*results[0]));

  // Add a second different valid credit card where the name is missing but
  // the credit card number matches.
  FormData form2;
  // Note missing name.
  autofill_test::CreateTestFormField(
      "Card Number:", "card_number", "4111111111111111", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Exp Month:", "exp_month", "01", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Exp Year:", "exp_year", "2011", "text", &field);
  form2.fields.push_back(field);

  FormStructure form_structure2(form2);
  form_structure2.DetermineHeuristicTypes();
  EXPECT_FALSE(personal_data_->ImportFormData(form_structure2,
                                              &imported_credit_card));
  ASSERT_FALSE(imported_credit_card);

  // Note: no refresh here.

  // No change is expected.
  CreditCard expected2;
  autofill_test::SetCreditCardInfo(&expected2,
      "Biggie Smalls", "4111111111111111", "01", "2011");
  const std::vector<CreditCard*>& results2 = personal_data_->credit_cards();
  ASSERT_EQ(1U, results2.size());
  EXPECT_EQ(0, expected2.Compare(*results2[0]));
}

TEST_F(PersonalDataManagerTest, AggregateCreditCardWithMissingInfoInOld) {
  FormData form1;

  // Start with a single valid credit card form.
  webkit_glue::FormField field;
  // Note missing name.
  autofill_test::CreateTestFormField(
      "Card Number:", "card_number", "4111-1111-1111-1111", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Exp Month:", "exp_month", "01", "text", &field);
  form1.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Exp Year:", "exp_year", "2011", "text", &field);
  form1.fields.push_back(field);

  FormStructure form_structure1(form1);
  form_structure1.DetermineHeuristicTypes();
  const CreditCard* imported_credit_card;
  EXPECT_TRUE(personal_data_->ImportFormData(form_structure1,
                                             &imported_credit_card));
  ASSERT_TRUE(imported_credit_card);
  personal_data_->SaveImportedCreditCard(*imported_credit_card);
  delete imported_credit_card;

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  CreditCard expected;
  autofill_test::SetCreditCardInfo(&expected,
      NULL, "4111111111111111", "01", "2011");
  const std::vector<CreditCard*>& results = personal_data_->credit_cards();
  ASSERT_EQ(1U, results.size());
  EXPECT_EQ(0, expected.Compare(*results[0]));

  // Add a second different valid credit card where the year is different but
  // the credit card number matches.
  FormData form2;
  autofill_test::CreateTestFormField(
      "Name on card:", "name_on_card", "Biggie Smalls", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Card Number:", "card_number", "4111-1111-1111-1111", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Exp Month:", "exp_month", "01", "text", &field);
  form2.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Exp Year:", "exp_year", "2011", "text", &field);
  form2.fields.push_back(field);

  FormStructure form_structure2(form2);
  form_structure2.DetermineHeuristicTypes();
  EXPECT_TRUE(personal_data_->ImportFormData(form_structure2,
                                             &imported_credit_card));
  ASSERT_TRUE(imported_credit_card);
  personal_data_->SaveImportedCreditCard(*imported_credit_card);
  delete imported_credit_card;

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  // Expect that the newer information is saved.  In this case the year is
  // added to the existing credit card.
  CreditCard expected2;
  autofill_test::SetCreditCardInfo(&expected2,
      "Biggie Smalls", "4111111111111111", "01", "2011");
  const std::vector<CreditCard*>& results2 = personal_data_->credit_cards();
  ASSERT_EQ(1U, results2.size());
  EXPECT_EQ(0, expected2.Compare(*results2[0]));
}

TEST_F(PersonalDataManagerTest, GetNonEmptyTypes) {
  // Check that there are no available types with no profiles stored.
  FieldTypeSet non_empty_types;
  personal_data_->GetNonEmptyTypes(&non_empty_types);
  EXPECT_EQ(0U, non_empty_types.size());

  // Test with one profile stored.
  AutofillProfile profile0;
  autofill_test::SetProfileInfo(&profile0,
      "Marion", NULL, "Morrison",
      "johnwayne@me.xyz", NULL, "123 Zoo St.", NULL, "Hollywood", "CA",
      "91601", "US", "14155678910", NULL);

  personal_data_->AddProfile(profile0);

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  personal_data_->GetNonEmptyTypes(&non_empty_types);
  EXPECT_EQ(14U, non_empty_types.size());
  EXPECT_TRUE(non_empty_types.count(NAME_FIRST));
  EXPECT_TRUE(non_empty_types.count(NAME_LAST));
  EXPECT_TRUE(non_empty_types.count(NAME_FULL));
  EXPECT_TRUE(non_empty_types.count(EMAIL_ADDRESS));
  EXPECT_TRUE(non_empty_types.count(ADDRESS_HOME_LINE1));
  EXPECT_TRUE(non_empty_types.count(ADDRESS_HOME_CITY));
  EXPECT_TRUE(non_empty_types.count(ADDRESS_HOME_STATE));
  EXPECT_TRUE(non_empty_types.count(ADDRESS_HOME_ZIP));
  EXPECT_TRUE(non_empty_types.count(ADDRESS_HOME_COUNTRY));
  EXPECT_TRUE(non_empty_types.count(PHONE_HOME_NUMBER));
  EXPECT_TRUE(non_empty_types.count(PHONE_HOME_COUNTRY_CODE));
  EXPECT_TRUE(non_empty_types.count(PHONE_HOME_CITY_CODE));
  EXPECT_TRUE(non_empty_types.count(PHONE_HOME_CITY_AND_NUMBER));
  EXPECT_TRUE(non_empty_types.count(PHONE_HOME_WHOLE_NUMBER));

  // Test with multiple profiles stored.
  AutofillProfile profile1;
  autofill_test::SetProfileInfo(&profile1,
      "Josephine", "Alicia", "Saenz",
      "joewayne@me.xyz", "Fox", "903 Apple Ct.", NULL, "Orlando", "FL", "32801",
      "US", "16502937549", "14082849239");

  AutofillProfile profile2;
  autofill_test::SetProfileInfo(&profile2,
      "Josephine", "Alicia", "Saenz",
      "joewayne@me.xyz", "Fox", "1212 Center.", "Bld. 5", "Orlando", "FL",
      "32801", "US", "16502937549", "14152849239");

  personal_data_->AddProfile(profile1);
  personal_data_->AddProfile(profile2);

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  personal_data_->GetNonEmptyTypes(&non_empty_types);
  EXPECT_EQ(23U, non_empty_types.size());
  EXPECT_TRUE(non_empty_types.count(NAME_FIRST));
  EXPECT_TRUE(non_empty_types.count(NAME_MIDDLE));
  EXPECT_TRUE(non_empty_types.count(NAME_MIDDLE_INITIAL));
  EXPECT_TRUE(non_empty_types.count(NAME_LAST));
  EXPECT_TRUE(non_empty_types.count(NAME_FULL));
  EXPECT_TRUE(non_empty_types.count(EMAIL_ADDRESS));
  EXPECT_TRUE(non_empty_types.count(COMPANY_NAME));
  EXPECT_TRUE(non_empty_types.count(ADDRESS_HOME_LINE1));
  EXPECT_TRUE(non_empty_types.count(ADDRESS_HOME_LINE2));
  EXPECT_TRUE(non_empty_types.count(ADDRESS_HOME_CITY));
  EXPECT_TRUE(non_empty_types.count(ADDRESS_HOME_STATE));
  EXPECT_TRUE(non_empty_types.count(ADDRESS_HOME_ZIP));
  EXPECT_TRUE(non_empty_types.count(ADDRESS_HOME_COUNTRY));
  EXPECT_TRUE(non_empty_types.count(PHONE_HOME_NUMBER));
  EXPECT_TRUE(non_empty_types.count(PHONE_HOME_CITY_CODE));
  EXPECT_TRUE(non_empty_types.count(PHONE_HOME_COUNTRY_CODE));
  EXPECT_TRUE(non_empty_types.count(PHONE_HOME_CITY_AND_NUMBER));
  EXPECT_TRUE(non_empty_types.count(PHONE_HOME_WHOLE_NUMBER));
  EXPECT_TRUE(non_empty_types.count(PHONE_FAX_NUMBER));
  EXPECT_TRUE(non_empty_types.count(PHONE_FAX_CITY_CODE));
  EXPECT_TRUE(non_empty_types.count(PHONE_FAX_COUNTRY_CODE));
  EXPECT_TRUE(non_empty_types.count(PHONE_FAX_CITY_AND_NUMBER));
  EXPECT_TRUE(non_empty_types.count(PHONE_FAX_WHOLE_NUMBER));

  // Test with credit card information also stored.
  CreditCard credit_card;
  autofill_test::SetCreditCardInfo(&credit_card,
                                   "John Dillinger", "423456789012" /* Visa */,
                                   "01", "2010");
  personal_data_->AddCreditCard(credit_card);

  // Verify that the web database has been updated and the notification sent.
  EXPECT_CALL(personal_data_observer_,
              OnPersonalDataChanged()).WillOnce(QuitUIMessageLoop());
  MessageLoop::current()->Run();

  personal_data_->GetNonEmptyTypes(&non_empty_types);
  EXPECT_EQ(30U, non_empty_types.size());
  EXPECT_TRUE(non_empty_types.count(NAME_FIRST));
  EXPECT_TRUE(non_empty_types.count(NAME_MIDDLE));
  EXPECT_TRUE(non_empty_types.count(NAME_MIDDLE_INITIAL));
  EXPECT_TRUE(non_empty_types.count(NAME_LAST));
  EXPECT_TRUE(non_empty_types.count(NAME_FULL));
  EXPECT_TRUE(non_empty_types.count(EMAIL_ADDRESS));
  EXPECT_TRUE(non_empty_types.count(COMPANY_NAME));
  EXPECT_TRUE(non_empty_types.count(ADDRESS_HOME_LINE1));
  EXPECT_TRUE(non_empty_types.count(ADDRESS_HOME_LINE2));
  EXPECT_TRUE(non_empty_types.count(ADDRESS_HOME_CITY));
  EXPECT_TRUE(non_empty_types.count(ADDRESS_HOME_STATE));
  EXPECT_TRUE(non_empty_types.count(ADDRESS_HOME_ZIP));
  EXPECT_TRUE(non_empty_types.count(ADDRESS_HOME_COUNTRY));
  EXPECT_TRUE(non_empty_types.count(PHONE_HOME_NUMBER));
  EXPECT_TRUE(non_empty_types.count(PHONE_HOME_CITY_CODE));
  EXPECT_TRUE(non_empty_types.count(PHONE_HOME_COUNTRY_CODE));
  EXPECT_TRUE(non_empty_types.count(PHONE_HOME_CITY_AND_NUMBER));
  EXPECT_TRUE(non_empty_types.count(PHONE_HOME_WHOLE_NUMBER));
  EXPECT_TRUE(non_empty_types.count(PHONE_FAX_NUMBER));
  EXPECT_TRUE(non_empty_types.count(PHONE_FAX_CITY_CODE));
  EXPECT_TRUE(non_empty_types.count(PHONE_FAX_COUNTRY_CODE));
  EXPECT_TRUE(non_empty_types.count(PHONE_FAX_CITY_AND_NUMBER));
  EXPECT_TRUE(non_empty_types.count(PHONE_FAX_WHOLE_NUMBER));
  EXPECT_TRUE(non_empty_types.count(CREDIT_CARD_NAME));
  EXPECT_TRUE(non_empty_types.count(CREDIT_CARD_NUMBER));
  EXPECT_TRUE(non_empty_types.count(CREDIT_CARD_EXP_MONTH));
  EXPECT_TRUE(non_empty_types.count(CREDIT_CARD_EXP_2_DIGIT_YEAR));
  EXPECT_TRUE(non_empty_types.count(CREDIT_CARD_EXP_4_DIGIT_YEAR));
  EXPECT_TRUE(non_empty_types.count(CREDIT_CARD_EXP_DATE_2_DIGIT_YEAR));
  EXPECT_TRUE(non_empty_types.count(CREDIT_CARD_EXP_DATE_4_DIGIT_YEAR));
}
