// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>

#include "base/file_path.h"
#include "base/memory/scoped_callback_factory.h"
#include "base/message_loop.h"
#include "base/message_loop_proxy.h"
#include "base/utf_string_conversions.h"
#include "net/base/net_errors.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "webkit/database/database_quota_client.h"
#include "webkit/database/database_tracker.h"
#include "webkit/database/database_util.h"

namespace webkit_database {

// Declared to shorten the line lengths.
static const quota::StorageType kTemp = quota::kStorageTypeTemporary;
static const quota::StorageType kPerm = quota::kStorageTypePersistent;

// Mock tracker class the mocks up those methods of the tracker
// that are used by the QuotaClient.
class MockDatabaseTracker : public DatabaseTracker {
 public:
  MockDatabaseTracker()
      : DatabaseTracker(FilePath(), false, NULL, NULL, NULL),
        delete_called_count_(0),
        async_delete_(false) {}

  virtual ~MockDatabaseTracker() {}

  virtual bool GetOriginInfo(
      const string16& origin_identifier,
      OriginInfo* info) OVERRIDE {
    std::map<GURL, MockOriginInfo>::const_iterator found =
        mock_origin_infos_.find(
            DatabaseUtil::GetOriginFromIdentifier(origin_identifier));
    if (found == mock_origin_infos_.end())
      return false;
    *info = OriginInfo(found->second);
    return true;
  }

  virtual bool GetAllOriginIdentifiers(
      std::vector<string16>* origins_identifiers) OVERRIDE {
    std::map<GURL, MockOriginInfo>::const_iterator iter;
    for (iter = mock_origin_infos_.begin();
         iter != mock_origin_infos_.end();
         ++iter) {
      origins_identifiers->push_back(iter->second.GetOrigin());
    }
    return true;
  }

  virtual bool GetAllOriginsInfo(
      std::vector<OriginInfo>* origins_info) OVERRIDE {
    std::map<GURL, MockOriginInfo>::const_iterator iter;
    for (iter = mock_origin_infos_.begin();
         iter != mock_origin_infos_.end();
         ++iter) {
      origins_info->push_back(OriginInfo(iter->second));
    }
    return true;
  }

  virtual int DeleteDataForOrigin(
      const string16& origin_id,
      net::CompletionCallback* callback) {
    ++delete_called_count_;
    if (async_delete()) {
      base::MessageLoopProxy::CreateForCurrentThread()->PostTask(FROM_HERE,
          NewRunnableMethod(this,
              &MockDatabaseTracker::AsyncDeleteDataForOrigin, callback));
      return net::ERR_IO_PENDING;
    }
    return net::OK;
  }

  void AsyncDeleteDataForOrigin(net::CompletionCallback* callback) {
    callback->Run(net::OK);
  }

  void AddMockDatabase(const GURL& origin,  const char* name, int size) {
    MockOriginInfo& info = mock_origin_infos_[origin];
    info.set_origin(DatabaseUtil::GetOriginIdentifier(origin));
    info.AddMockDatabase(ASCIIToUTF16(name), size);
  }

  int delete_called_count() { return delete_called_count_; }
  bool async_delete() { return async_delete_; }
  void set_async_delete(bool async) { async_delete_ = async; }

 private:
  class MockOriginInfo : public OriginInfo {
   public:
    void set_origin(const string16& origin_id) {
      origin_ = origin_id;
    }

    void AddMockDatabase(const string16& name, int size) {
      EXPECT_TRUE(database_info_.find(name) == database_info_.end());
      database_info_[name].first = size;
      total_size_ += size;
    }
  };

  int delete_called_count_;
  bool async_delete_;
  std::map<GURL, MockOriginInfo> mock_origin_infos_;
};


// Base class for our test fixtures.
class DatabaseQuotaClientTest : public testing::Test {
 public:
  const GURL kOriginA;
  const GURL kOriginB;
  const GURL kOriginOther;

  DatabaseQuotaClientTest()
      : kOriginA("http://host"),
        kOriginB("http://host:8000"),
        kOriginOther("http://other"),
        usage_(0),
        mock_tracker_(new MockDatabaseTracker),
        callback_factory_(ALLOW_THIS_IN_INITIALIZER_LIST(this)) {
  }

  int64 GetOriginUsage(
      quota::QuotaClient* client,
      const GURL& origin,
      quota::StorageType type) {
    usage_ = 0;
    client->GetOriginUsage(origin, type,
        callback_factory_.NewCallback(
            &DatabaseQuotaClientTest::OnGetOriginUsageComplete));
    MessageLoop::current()->RunAllPending();
    return usage_;
  }

  const std::set<GURL>& GetOriginsForType(
      quota::QuotaClient* client,
      quota::StorageType type) {
    origins_.clear();
    client->GetOriginsForType(type,
        callback_factory_.NewCallback(
            &DatabaseQuotaClientTest::OnGetOriginsComplete));
    MessageLoop::current()->RunAllPending();
    return origins_;
  }

  const std::set<GURL>& GetOriginsForHost(
      quota::QuotaClient* client,
      quota::StorageType type,
      const std::string& host) {
    origins_.clear();
    client->GetOriginsForHost(type, host,
        callback_factory_.NewCallback(
            &DatabaseQuotaClientTest::OnGetOriginsComplete));
    MessageLoop::current()->RunAllPending();
    return origins_;
  }

  bool DeleteOriginData(
      quota::QuotaClient* client,
      quota::StorageType type,
      const GURL& origin) {
    delete_status_ = quota::kQuotaStatusUnknown;
    client->DeleteOriginData(origin, type,
        callback_factory_.NewCallback(
            &DatabaseQuotaClientTest::OnDeleteOriginDataComplete));
    MessageLoop::current()->RunAllPending();
    return delete_status_ == quota::kQuotaStatusOk;
  }

  MockDatabaseTracker* mock_tracker() { return mock_tracker_.get(); }


 private:
  void OnGetOriginUsageComplete(int64 usage) {
    usage_ = usage;
  }

  void OnGetOriginsComplete(const std::set<GURL>& origins) {
    origins_ = origins;
  }

  void OnDeleteOriginDataComplete(quota::QuotaStatusCode status) {
    delete_status_ = status;
  }

  int64 usage_;
  std::set<GURL> origins_;
  quota::QuotaStatusCode delete_status_;
  scoped_refptr<MockDatabaseTracker> mock_tracker_;
  base::ScopedCallbackFactory<DatabaseQuotaClientTest> callback_factory_;
};


TEST_F(DatabaseQuotaClientTest, GetOriginUsage) {
  DatabaseQuotaClient client(
      base::MessageLoopProxy::CreateForCurrentThread(),
      mock_tracker());

  EXPECT_EQ(0, GetOriginUsage(&client, kOriginA, kTemp));
  EXPECT_EQ(0, GetOriginUsage(&client, kOriginA, kPerm));

  mock_tracker()->AddMockDatabase(kOriginA, "fooDB", 1000);
  EXPECT_EQ(1000, GetOriginUsage(&client, kOriginA, kTemp));
  EXPECT_EQ(0, GetOriginUsage(&client, kOriginA, kPerm));

  EXPECT_EQ(0, GetOriginUsage(&client, kOriginB, kPerm));
  EXPECT_EQ(0, GetOriginUsage(&client, kOriginB, kTemp));
}

TEST_F(DatabaseQuotaClientTest, GetOriginsForHost) {
  DatabaseQuotaClient client(
      base::MessageLoopProxy::CreateForCurrentThread(),
      mock_tracker());

  EXPECT_EQ(kOriginA.host(), kOriginB.host());
  EXPECT_NE(kOriginA.host(), kOriginOther.host());

  std::set<GURL> origins = GetOriginsForHost(&client, kTemp, kOriginA.host());
  EXPECT_TRUE(origins.empty());

  mock_tracker()->AddMockDatabase(kOriginA, "fooDB", 1000);
  origins = GetOriginsForHost(&client, kTemp, kOriginA.host());
  EXPECT_EQ(origins.size(), 1ul);
  EXPECT_TRUE(origins.find(kOriginA) != origins.end());

  mock_tracker()->AddMockDatabase(kOriginB, "barDB", 1000);
  origins = GetOriginsForHost(&client, kTemp, kOriginA.host());
  EXPECT_EQ(origins.size(), 2ul);
  EXPECT_TRUE(origins.find(kOriginA) != origins.end());
  EXPECT_TRUE(origins.find(kOriginB) != origins.end());

  EXPECT_TRUE(GetOriginsForHost(&client, kPerm, kOriginA.host()).empty());
  EXPECT_TRUE(GetOriginsForHost(&client, kTemp, kOriginOther.host()).empty());
}

TEST_F(DatabaseQuotaClientTest, GetOriginsForType) {
  DatabaseQuotaClient client(
      base::MessageLoopProxy::CreateForCurrentThread(),
      mock_tracker());

  EXPECT_TRUE(GetOriginsForType(&client, kTemp).empty());
  EXPECT_TRUE(GetOriginsForType(&client, kPerm).empty());

  mock_tracker()->AddMockDatabase(kOriginA, "fooDB", 1000);
  std::set<GURL> origins = GetOriginsForType(&client, kTemp);
  EXPECT_EQ(origins.size(), 1ul);
  EXPECT_TRUE(origins.find(kOriginA) != origins.end());

  EXPECT_TRUE(GetOriginsForType(&client, kPerm).empty());
}

TEST_F(DatabaseQuotaClientTest, DeleteOriginData) {
  DatabaseQuotaClient client(
      base::MessageLoopProxy::CreateForCurrentThread(),
      mock_tracker());

  // Perm deletions are short circuited in the Client and
  // should not reach the DatabaseTracker.
  EXPECT_TRUE(DeleteOriginData(&client, kPerm, kOriginA));
  EXPECT_EQ(0, mock_tracker()->delete_called_count());

  mock_tracker()->set_async_delete(false);
  EXPECT_TRUE(DeleteOriginData(&client, kTemp, kOriginA));
  EXPECT_EQ(1, mock_tracker()->delete_called_count());

  mock_tracker()->set_async_delete(true);
  EXPECT_TRUE(DeleteOriginData(&client, kTemp, kOriginA));
  EXPECT_EQ(2, mock_tracker()->delete_called_count());
}

}  // namespace webkit_database
