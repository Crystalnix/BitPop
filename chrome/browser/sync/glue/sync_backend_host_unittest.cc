// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/glue/sync_backend_host.h"

#include <cstddef>

#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "chrome/browser/sync/engine/model_safe_worker.h"
#include "chrome/browser/sync/internal_api/includes/unrecoverable_error_handler_mock.h"
#include "chrome/browser/sync/protocol/encryption.pb.h"
#include "chrome/browser/sync/protocol/sync_protocol_error.h"
#include "chrome/browser/sync/sync_prefs.h"
#include "chrome/browser/sync/syncable/model_type.h"
#include "chrome/test/base/test_url_request_context_getter.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/base/test_url_request_context_getter.h"
#include "content/test/test_browser_thread.h"
#include "content/test/test_url_fetcher_factory.h"
#include "googleurl/src/gurl.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using content::BrowserThread;

namespace browser_sync {

namespace {

class MockSyncFrontend : public SyncFrontend {
 public:
  virtual ~MockSyncFrontend() {}

  MOCK_METHOD2(OnBackendInitialized, void(const WeakHandle<JsBackend>&, bool));
  MOCK_METHOD0(OnSyncCycleCompleted, void());
  MOCK_METHOD0(OnAuthError, void());
  MOCK_METHOD0(OnStopSyncingPermanently, void());
  MOCK_METHOD0(OnClearServerDataSucceeded, void());
  MOCK_METHOD0(OnClearServerDataFailed, void());
  MOCK_METHOD2(OnPassphraseRequired,
               void(sync_api::PassphraseRequiredReason,
                    const sync_pb::EncryptedData&));
  MOCK_METHOD0(OnPassphraseAccepted, void());
  MOCK_METHOD2(OnEncryptedTypesChanged,
               void(syncable::ModelTypeSet, bool));
  MOCK_METHOD0(OnEncryptionComplete, void());
  MOCK_METHOD1(OnMigrationNeededForTypes, void(syncable::ModelTypeSet));
  MOCK_METHOD1(OnDataTypesChanged, void(syncable::ModelTypeSet));
  MOCK_METHOD1(OnActionableError,
      void(const browser_sync::SyncProtocolError& sync_error));
  MOCK_METHOD0(OnSyncConfigureRetry, void());
};

}  // namespace

class SyncBackendHostTest : public testing::Test {
 protected:
  SyncBackendHostTest()
      : ui_thread_(BrowserThread::UI, &ui_loop_),
        io_thread_(BrowserThread::IO) {}

  virtual ~SyncBackendHostTest() {}

  virtual void SetUp() {
    io_thread_.StartIOThread();
  }

  virtual void TearDown() {
    // Pump messages posted by the sync core thread (which may end up
    // posting on the IO thread).
    ui_loop_.RunAllPending();
    io_thread_.Stop();
    // Pump any messages posted by the IO thread.
    ui_loop_.RunAllPending();
  }

 private:
  MessageLoop ui_loop_;
  content::TestBrowserThread ui_thread_;
  content::TestBrowserThread io_thread_;
};

TEST_F(SyncBackendHostTest, InitShutdown) {
  std::string k_mock_url = "http://www.example.com";
  FakeURLFetcherFactory test_factory_;
  test_factory_.SetFakeResponse(k_mock_url + "/time?command=get_time", "",
      false);

  TestingProfile profile;
  profile.CreateRequestContext();

  SyncPrefs sync_prefs(profile.GetPrefs());
  SyncBackendHost backend(profile.GetDebugName(),
                          &profile, sync_prefs.AsWeakPtr());

  MockSyncFrontend mock_frontend;
  sync_api::SyncCredentials credentials;
  credentials.email = "user@example.com";
  credentials.sync_token = "sync_token";
  browser_sync::MockUnrecoverableErrorHandler handler_mock;
  backend.Initialize(&mock_frontend,
                     WeakHandle<JsEventHandler>(),
                     GURL(k_mock_url),
                     syncable::ModelTypeSet(),
                     credentials,
                     true,
                     &handler_mock);
  backend.StopSyncingForShutdown();
  backend.Shutdown(false);
}

// TODO(akalin): Write more SyncBackendHost unit tests.

}  // namespace browser_sync
