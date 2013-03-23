// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/cloud_policy_controller.h"

#include "base/files/scoped_temp_dir.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "chrome/browser/policy/cloud_policy_data_store.h"
#include "chrome/browser/policy/device_token_fetcher.h"
#include "chrome/browser/policy/logging_work_scheduler.h"
#include "chrome/browser/policy/mock_device_management_service.h"
#include "chrome/browser/policy/policy_notifier.h"
#include "chrome/browser/policy/proto/cloud_policy.pb.h"
#include "chrome/browser/policy/proto/device_management_backend.pb.h"
#include "chrome/browser/policy/user_policy_cache.h"
#include "content/public/test/test_browser_thread.h"
#include "policy/policy_constants.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace em = enterprise_management;

namespace policy {

using ::testing::AnyNumber;
using ::testing::DoAll;
using ::testing::InSequence;
using ::testing::InvokeWithoutArgs;
using ::testing::_;
using content::BrowserThread;

class MockDeviceTokenFetcher : public DeviceTokenFetcher {
 public:
  explicit MockDeviceTokenFetcher(CloudPolicyCacheBase* cache)
      : DeviceTokenFetcher(NULL, cache, NULL, NULL) {}
  virtual ~MockDeviceTokenFetcher() {}

  MOCK_METHOD0(FetchToken, void());
  MOCK_METHOD0(SetUnmanagedState, void());
  MOCK_METHOD0(SetSerialNumberInvalidState, void());
  MOCK_METHOD0(SetMissingLicensesState, void());

 private:
  DISALLOW_COPY_AND_ASSIGN(MockDeviceTokenFetcher);
};

class CloudPolicyControllerTest : public testing::Test {
 public:
  CloudPolicyControllerTest()
      : ui_thread_(BrowserThread::UI, &loop_),
        file_thread_(BrowserThread::FILE, &loop_) {
    em::PolicyData signed_response;
    em::CloudPolicySettings settings;
    em::BooleanPolicyProto* spdy_proto = settings.mutable_disablespdy();
    spdy_proto->set_value(true);
    spdy_proto->mutable_policy_options()->set_mode(
        em::PolicyOptions::MANDATORY);
    EXPECT_TRUE(
        settings.SerializeToString(signed_response.mutable_policy_value()));
    base::TimeDelta timestamp =
        base::Time::NowFromSystemTime() - base::Time::UnixEpoch();
    signed_response.set_timestamp(timestamp.InMilliseconds());
    std::string serialized_signed_response;
    EXPECT_TRUE(signed_response.SerializeToString(&serialized_signed_response));
    em::PolicyFetchResponse* fetch_response =
        spdy_policy_response_.mutable_policy_response()->add_response();
    fetch_response->set_policy_data(serialized_signed_response);
  }

  virtual ~CloudPolicyControllerTest() {}

  virtual void SetUp() {
    ASSERT_TRUE(temp_user_data_dir_.CreateUniqueTempDir());
    cache_.reset(new UserPolicyCache(
        temp_user_data_dir_.path().AppendASCII("CloudPolicyControllerTest"),
        false  /* wait_for_policy_fetch */));
    token_fetcher_.reset(new MockDeviceTokenFetcher(cache_.get()));
    EXPECT_CALL(service_, StartJob(_, _, _, _, _, _, _)).Times(AnyNumber());
    data_store_.reset(CloudPolicyDataStore::CreateForUserPolicies());
  }

  virtual void TearDown() {
    controller_.reset();  // Unregisters observers.
    data_store_.reset();
  }

  void CreateNewController() {
    controller_.reset(new CloudPolicyController(
        &service_, cache_.get(), token_fetcher_.get(), data_store_.get(),
        &notifier_, new DummyWorkScheduler));
  }

  void CreateNewWaitingCache() {
    cache_.reset(new UserPolicyCache(
        temp_user_data_dir_.path().AppendASCII("CloudPolicyControllerTest"),
        true  /* wait_for_policy_fetch */));
    // Make this cache's disk cache ready, but have it still waiting for a
    // policy fetch.
    cache_->Load();
    loop_.RunUntilIdle();
    ASSERT_TRUE(cache_->last_policy_refresh_time().is_null());
    ASSERT_FALSE(cache_->IsReady());
  }

  void ExpectHasSpdyPolicy() {
    base::FundamentalValue expected(true);
    ASSERT_TRUE(Value::Equals(&expected,
                              cache_->policy()->GetValue(key::kDisableSpdy)));
  }

 protected:
  scoped_ptr<CloudPolicyCacheBase> cache_;
  scoped_ptr<CloudPolicyController> controller_;
  scoped_ptr<MockDeviceTokenFetcher> token_fetcher_;
  scoped_ptr<CloudPolicyDataStore> data_store_;
  MockDeviceManagementService service_;
  PolicyNotifier notifier_;
  base::ScopedTempDir temp_user_data_dir_;
  MessageLoop loop_;
  em::DeviceManagementResponse spdy_policy_response_;

 private:
  content::TestBrowserThread ui_thread_;
  content::TestBrowserThread file_thread_;

  DISALLOW_COPY_AND_ASSIGN(CloudPolicyControllerTest);
};

// If a device token is present when the controller starts up, it should
// fetch and apply policy.
TEST_F(CloudPolicyControllerTest, StartupWithDeviceToken) {
  data_store_->SetupForTesting("fake_device_token", "device_id", "", "",
                               true);
  EXPECT_CALL(service_,
              CreateJob(DeviceManagementRequestJob::TYPE_POLICY_FETCH))
      .WillOnce(DoAll(InvokeWithoutArgs(&loop_, &MessageLoop::QuitNow),
                      service_.SucceedJob(spdy_policy_response_)));
  CreateNewController();
  loop_.RunUntilIdle();
  ExpectHasSpdyPolicy();
}

// If no device token is present when the controller starts up, it should
// instruct the token_fetcher_ to fetch one.
TEST_F(CloudPolicyControllerTest, StartupWithoutDeviceToken) {
  data_store_->SetupForTesting("", "device_id", "a@b.com", "auth_token",
                               true);
  EXPECT_CALL(*token_fetcher_.get(), FetchToken()).Times(1);
  CreateNewController();
  loop_.RunUntilIdle();
}

// If the current user belongs to a known non-managed domain, no token fetch
// should be initiated.
TEST_F(CloudPolicyControllerTest, StartupUnmanagedUser) {
  data_store_->SetupForTesting("", "device_id", "DannoHelper@gmail.com",
                               "auth_token", true);
  EXPECT_CALL(*token_fetcher_.get(), FetchToken()).Times(0);
  CreateNewController();
  loop_.RunUntilIdle();
}

// After policy has been fetched successfully, a new fetch should be triggered
// after the refresh interval has timed out.
TEST_F(CloudPolicyControllerTest, RefreshAfterSuccessfulPolicy) {
  data_store_->SetupForTesting("device_token", "device_id",
                               "DannoHelperDelegate@b.com",
                               "auth_token", true);
  {
    InSequence s;
    EXPECT_CALL(service_,
                CreateJob(DeviceManagementRequestJob::TYPE_POLICY_FETCH))
        .WillOnce(service_.SucceedJob(spdy_policy_response_));
    EXPECT_CALL(service_,
                CreateJob(DeviceManagementRequestJob::TYPE_POLICY_FETCH))
        .WillOnce(DoAll(InvokeWithoutArgs(&loop_, &MessageLoop::QuitNow),
                        service_.FailJob(DM_STATUS_REQUEST_FAILED)));
  }
  CreateNewController();
  loop_.RunUntilIdle();
  ExpectHasSpdyPolicy();
}

// If policy fetching failed, it should be retried.
TEST_F(CloudPolicyControllerTest, RefreshAfterError) {
  data_store_->SetupForTesting("device_token", "device_id",
                               "DannoHelperDelegateImpl@b.com",
                               "auth_token", true);
  {
    InSequence s;
    EXPECT_CALL(service_,
                CreateJob(DeviceManagementRequestJob::TYPE_POLICY_FETCH))
        .WillOnce(service_.FailJob(DM_STATUS_REQUEST_FAILED));
    EXPECT_CALL(service_,
                CreateJob(DeviceManagementRequestJob::TYPE_POLICY_FETCH))
        .WillOnce(DoAll(InvokeWithoutArgs(&loop_, &MessageLoop::QuitNow),
                        service_.SucceedJob(spdy_policy_response_)));
  }
  CreateNewController();
  loop_.RunUntilIdle();
  ExpectHasSpdyPolicy();
}

// If the backend reports that the device token was invalid, the controller
// should instruct the token fetcher to fetch a new token.
TEST_F(CloudPolicyControllerTest, InvalidToken) {
  data_store_->SetupForTesting("device_token", "device_id",
                               "standup@ten.am", "auth", true);
  EXPECT_CALL(service_,
              CreateJob(DeviceManagementRequestJob::TYPE_POLICY_FETCH))
      .WillOnce(service_.FailJob(DM_STATUS_SERVICE_MANAGEMENT_TOKEN_INVALID));
  EXPECT_CALL(*token_fetcher_.get(), FetchToken()).Times(1);
  CreateNewController();
  loop_.RunUntilIdle();
}

// If the backend reports that the device is unknown to the server, the
// controller should instruct the token fetcher to fetch a new token.
TEST_F(CloudPolicyControllerTest, DeviceNotFound) {
  data_store_->SetupForTesting("device_token", "device_id",
                               "me@you.com", "auth", true);
  EXPECT_CALL(service_,
              CreateJob(DeviceManagementRequestJob::TYPE_POLICY_FETCH))
      .WillOnce(service_.FailJob(DM_STATUS_SERVICE_DEVICE_NOT_FOUND));
  EXPECT_CALL(*token_fetcher_.get(), FetchToken()).Times(1);
  CreateNewController();
  loop_.RunUntilIdle();
}

// If the backend reports that the device-id is already existing, the
// controller should instruct the token fetcher to fetch a new token.
TEST_F(CloudPolicyControllerTest, DeviceIdConflict) {
  data_store_->SetupForTesting("device_token", "device_id",
                               "me@you.com", "auth", true);
  EXPECT_CALL(service_,
              CreateJob(DeviceManagementRequestJob::TYPE_POLICY_FETCH))
      .WillOnce(service_.FailJob(DM_STATUS_SERVICE_DEVICE_ID_CONFLICT));
  EXPECT_CALL(*token_fetcher_.get(), FetchToken()).Times(1);
  CreateNewController();
  loop_.RunUntilIdle();
}

// If the backend reports that the device is no longer managed, the controller
// should instruct the token fetcher to fetch a new token (which will in turn
// set and persist the correct 'unmanaged' state).
TEST_F(CloudPolicyControllerTest, NoLongerManaged) {
  data_store_->SetupForTesting("device_token", "device_id",
                               "who@what.com", "auth", true);
  EXPECT_CALL(service_,
              CreateJob(DeviceManagementRequestJob::TYPE_POLICY_FETCH))
      .WillOnce(service_.FailJob(DM_STATUS_SERVICE_MANAGEMENT_NOT_SUPPORTED));
  EXPECT_CALL(*token_fetcher_.get(), SetUnmanagedState()).Times(1);
  CreateNewController();
  loop_.RunUntilIdle();
}

// If the backend reports that the device has invalid serial number, the
// controller should instruct the token fetcher not to fetch a new token
// (which will in turn set and persist the correct 'sn invalid' state).
TEST_F(CloudPolicyControllerTest, InvalidSerialNumber) {
  data_store_->SetupForTesting("device_token", "device_id",
                               "who@what.com", "auth", true);
  EXPECT_CALL(service_,
              CreateJob(DeviceManagementRequestJob::TYPE_POLICY_FETCH))
      .WillOnce(service_.FailJob(DM_STATUS_SERVICE_INVALID_SERIAL_NUMBER));
  EXPECT_CALL(*token_fetcher_.get(), SetSerialNumberInvalidState()).Times(1);
  CreateNewController();
  loop_.RunUntilIdle();
}

// If the backend reports that the domain has run out of licenses, the
// controller should instruct the token fetcher not to fetch a new token
// (which will in turn set and persist the correct 'missing licenses' state).
TEST_F(CloudPolicyControllerTest, MissingLicenses) {
  data_store_->SetupForTesting("device_token", "device_id",
                               "who@what.com", "auth", true);
  EXPECT_CALL(service_,
              CreateJob(DeviceManagementRequestJob::TYPE_POLICY_FETCH))
      .WillOnce(service_.FailJob(DM_STATUS_SERVICE_MISSING_LICENSES));
  EXPECT_CALL(*token_fetcher_.get(), SetMissingLicensesState()).Times(1);
  CreateNewController();
  loop_.RunUntilIdle();
}

TEST_F(CloudPolicyControllerTest, DontSetFetchingDoneWithoutTokens) {
  CreateNewWaitingCache();
  CreateNewController();
  // Initialized without an oauth token, goes into TOKEN_UNAVAILABLE state.
  // This means the controller is still waiting for an oauth token fetch.
  loop_.RunUntilIdle();
  EXPECT_FALSE(cache_->IsReady());

  controller_->OnDeviceTokenChanged();
  loop_.RunUntilIdle();
  EXPECT_FALSE(cache_->IsReady());
}

TEST_F(CloudPolicyControllerTest, RefreshPoliciesWithoutMaterial) {
  CreateNewWaitingCache();
  CreateNewController();
  loop_.RunUntilIdle();
  EXPECT_FALSE(cache_->IsReady());

  // Same scenario as the last test, but the RefreshPolicies call must always
  // notify the cache.
  controller_->RefreshPolicies(false);
  loop_.RunUntilIdle();
  EXPECT_TRUE(cache_->IsReady());
}

TEST_F(CloudPolicyControllerTest, DontSetFetchingDoneWithoutFetching) {
  CreateNewWaitingCache();
  data_store_->SetupForTesting("device_token", "device_id",
                               "who@what.com", "auth", true);
  CreateNewController();
  // Initialized with an oauth token, goes into TOKEN_VALID state.
  // This means the controller has an oauth token and should fetch the next
  // token, which is the dm server register token.
  EXPECT_FALSE(cache_->IsReady());
}

TEST_F(CloudPolicyControllerTest, SetFetchingDoneForUnmanagedUsers) {
  CreateNewWaitingCache();
  data_store_->SetupForTesting("", "device_id",
                               "user@gmail.com", "auth", true);
  CreateNewController();
  loop_.RunUntilIdle();
  // User is in an unmanaged domain.
  EXPECT_TRUE(cache_->IsReady());
  EXPECT_TRUE(cache_->last_policy_refresh_time().is_null());
}

TEST_F(CloudPolicyControllerTest, SetFetchingDoneAfterPolicyFetch) {
  CreateNewWaitingCache();
  data_store_->SetupForTesting("device_token", "device_id",
                               "user@enterprise.com", "auth", true);
  EXPECT_CALL(service_,
              CreateJob(DeviceManagementRequestJob::TYPE_POLICY_FETCH))
      .WillOnce(DoAll(InvokeWithoutArgs(&loop_, &MessageLoop::QuitNow),
                      service_.SucceedJob(spdy_policy_response_)));
  CreateNewController();
  loop_.RunUntilIdle();
  EXPECT_TRUE(cache_->IsReady());
  EXPECT_FALSE(cache_->last_policy_refresh_time().is_null());
}

TEST_F(CloudPolicyControllerTest, SetFetchingDoneAfterPolicyFetchFails) {
  CreateNewWaitingCache();
  data_store_->SetupForTesting("device_token", "device_id",
                               "user@enterprise.com", "auth", true);
  EXPECT_CALL(service_,
              CreateJob(DeviceManagementRequestJob::TYPE_POLICY_FETCH))
      .WillOnce(DoAll(InvokeWithoutArgs(&loop_, &MessageLoop::QuitNow),
                      service_.FailJob(DM_STATUS_REQUEST_FAILED)));
  CreateNewController();
  loop_.RunUntilIdle();
  EXPECT_TRUE(cache_->IsReady());
  EXPECT_TRUE(cache_->last_policy_refresh_time().is_null());
}

TEST_F(CloudPolicyControllerTest, DelayRefreshesIfPolicyIsInvalid) {
  // Reply with a protobuf whose timestamp is too far in the future. The policy
  // cache will reject it, and the controller should detect that and go into
  // STATE_POLICY_ERROR instead of STATE_POLICY_VALID.

  // Build the |response|.
  em::DeviceManagementResponse response;
  em::PolicyData data;
  em::CloudPolicySettings settings;
  EXPECT_TRUE(settings.SerializeToString(data.mutable_policy_value()));
  base::Time far_in_the_future =
      base::Time::NowFromSystemTime() + base::TimeDelta::FromDays(42);
  base::TimeDelta timestamp = far_in_the_future - base::Time::UnixEpoch();
  data.set_timestamp(timestamp.InMilliseconds());
  std::string serialized_data;
  EXPECT_TRUE(data.SerializeToString(&serialized_data));
  em::PolicyFetchResponse* fetch_response =
      response.mutable_policy_response()->add_response();
  fetch_response->set_policy_data(serialized_data);

  data_store_->SetupForTesting("device_token", "device_id",
                               "madmax@managedchrome.com",
                               "auth_token", true);

  EXPECT_CALL(service_,
              CreateJob(DeviceManagementRequestJob::TYPE_POLICY_FETCH))
      .WillOnce(DoAll(InvokeWithoutArgs(&loop_, &MessageLoop::QuitNow),
                      service_.SucceedJob(response)));
  CreateNewController();
  loop_.RunUntilIdle();
  EXPECT_EQ(CloudPolicySubsystem::NETWORK_ERROR, notifier_.state());
  EXPECT_EQ(CloudPolicySubsystem::POLICY_NETWORK_ERROR,
            notifier_.error_details());
}

}  // namespace policy
