// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/prefs/pref_change_registrar.h"
#include "chrome/test/base/testing_pref_service.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/notification_types.h"
#include "content/test/notification_observer_mock.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::Mock;
using testing::Eq;

namespace {

// A mock provider that allows us to capture pref observer changes.
class MockPrefService : public TestingPrefService {
 public:
  MockPrefService() {}
  virtual ~MockPrefService() {}

  MOCK_METHOD2(AddPrefObserver,
               void(const char*, content::NotificationObserver*));
  MOCK_METHOD2(RemovePrefObserver,
               void(const char*, content::NotificationObserver*));
};

}  // namespace

class PrefChangeRegistrarTest : public testing::Test {
 public:
  PrefChangeRegistrarTest() {}
  virtual ~PrefChangeRegistrarTest() {}

 protected:
  virtual void SetUp();

  content::NotificationObserver* observer() const { return observer_.get(); }
  MockPrefService* service() const { return service_.get(); }

 private:
  scoped_ptr<MockPrefService> service_;
  scoped_ptr<content::NotificationObserverMock> observer_;
};

void PrefChangeRegistrarTest::SetUp() {
  service_.reset(new MockPrefService());
  observer_.reset(new content::NotificationObserverMock());
}

TEST_F(PrefChangeRegistrarTest, AddAndRemove) {
  PrefChangeRegistrar registrar;
  registrar.Init(service());

  // Test adding.
  EXPECT_CALL(*service(),
              AddPrefObserver(Eq(std::string("test.pref.1")), observer()));
  EXPECT_CALL(*service(),
              AddPrefObserver(Eq(std::string("test.pref.2")), observer()));
  registrar.Add("test.pref.1", observer());
  registrar.Add("test.pref.2", observer());
  EXPECT_FALSE(registrar.IsEmpty());

  // Test removing.
  Mock::VerifyAndClearExpectations(service());
  EXPECT_CALL(*service(),
              RemovePrefObserver(Eq(std::string("test.pref.1")), observer()));
  EXPECT_CALL(*service(),
              RemovePrefObserver(Eq(std::string("test.pref.2")), observer()));
  registrar.Remove("test.pref.1", observer());
  registrar.Remove("test.pref.2", observer());
  EXPECT_TRUE(registrar.IsEmpty());

  // Explicitly check the expectations now to make sure that the Removes
  // worked (rather than the registrar destructor doing the work).
  Mock::VerifyAndClearExpectations(service());
}

TEST_F(PrefChangeRegistrarTest, AutoRemove) {
  PrefChangeRegistrar registrar;
  registrar.Init(service());

  // Setup of auto-remove.
  EXPECT_CALL(*service(),
              AddPrefObserver(Eq(std::string("test.pref.1")), observer()));
  registrar.Add("test.pref.1", observer());
  Mock::VerifyAndClearExpectations(service());
  EXPECT_FALSE(registrar.IsEmpty());

  // Test auto-removing.
  EXPECT_CALL(*service(),
              RemovePrefObserver(Eq(std::string("test.pref.1")), observer()));
}

TEST_F(PrefChangeRegistrarTest, RemoveAll) {
  PrefChangeRegistrar registrar;
  registrar.Init(service());

  EXPECT_CALL(*service(),
              AddPrefObserver(Eq(std::string("test.pref.1")), observer()));
  EXPECT_CALL(*service(),
              AddPrefObserver(Eq(std::string("test.pref.2")), observer()));
  registrar.Add("test.pref.1", observer());
  registrar.Add("test.pref.2", observer());
  Mock::VerifyAndClearExpectations(service());

  EXPECT_CALL(*service(),
              RemovePrefObserver(Eq(std::string("test.pref.1")), observer()));
  EXPECT_CALL(*service(),
              RemovePrefObserver(Eq(std::string("test.pref.2")), observer()));
  registrar.RemoveAll();
  EXPECT_TRUE(registrar.IsEmpty());

  // Explicitly check the expectations now to make sure that the RemoveAll
  // worked (rather than the registrar destructor doing the work).
  Mock::VerifyAndClearExpectations(service());
}
