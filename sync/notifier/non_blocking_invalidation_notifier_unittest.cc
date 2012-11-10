// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sync/notifier/non_blocking_invalidation_notifier.h"

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "base/threading/thread.h"
#include "google/cacheinvalidation/types.pb.h"
#include "jingle/notifier/base/fake_base_task.h"
#include "net/url_request/url_request_test_util.h"
#include "sync/internal_api/public/base/model_type.h"
#include "sync/internal_api/public/base/model_type_payload_map.h"
#include "sync/internal_api/public/util/weak_handle.h"
#include "sync/notifier/invalidation_state_tracker.h"
#include "sync/notifier/mock_sync_notifier_observer.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace syncer {

namespace {

using ::testing::InSequence;
using ::testing::StrictMock;

class NonBlockingInvalidationNotifierTest : public testing::Test {
 public:
  NonBlockingInvalidationNotifierTest() : io_thread_("Test IO thread") {}

 protected:
  virtual void SetUp() {
    base::Thread::Options options;
    options.message_loop_type = MessageLoop::TYPE_IO;
    io_thread_.StartWithOptions(options);
    request_context_getter_ =
        new TestURLRequestContextGetter(io_thread_.message_loop_proxy());
    notifier::NotifierOptions notifier_options;
    notifier_options.request_context_getter = request_context_getter_;
    invalidation_notifier_.reset(
        new NonBlockingInvalidationNotifier(
            notifier_options,
            InvalidationVersionMap(),
            std::string(),  // initial_invalidation_state
            MakeWeakHandle(base::WeakPtr<InvalidationStateTracker>()),
            "fake_client_info"));
    invalidation_notifier_->RegisterHandler(&mock_observer_);
  }

  virtual void TearDown() {
    invalidation_notifier_->UnregisterHandler(&mock_observer_);
    invalidation_notifier_.reset();
    request_context_getter_ = NULL;
    io_thread_.Stop();
    ui_loop_.RunAllPending();
  }

  MessageLoop ui_loop_;
  base::Thread io_thread_;
  scoped_refptr<net::URLRequestContextGetter> request_context_getter_;
  scoped_ptr<NonBlockingInvalidationNotifier> invalidation_notifier_;
  StrictMock<MockSyncNotifierObserver> mock_observer_;
  notifier::FakeBaseTask fake_base_task_;
};

// TODO(akalin): Add real unit tests (http://crbug.com/140410).

TEST_F(NonBlockingInvalidationNotifierTest, Basic) {
  InSequence dummy;

  const ModelTypeSet models(PREFERENCES, BOOKMARKS, AUTOFILL);
  const ModelTypePayloadMap& type_payloads =
      ModelTypePayloadMapFromEnumSet(models, "payload");
  EXPECT_CALL(mock_observer_, OnNotificationsEnabled());
  EXPECT_CALL(mock_observer_, OnIncomingNotification(
      ModelTypePayloadMapToObjectIdPayloadMap(type_payloads),
      REMOTE_NOTIFICATION));
  EXPECT_CALL(mock_observer_,
              OnNotificationsDisabled(TRANSIENT_NOTIFICATION_ERROR));
  EXPECT_CALL(mock_observer_,
              OnNotificationsDisabled(NOTIFICATION_CREDENTIALS_REJECTED));

  invalidation_notifier_->UpdateRegisteredIds(
      &mock_observer_, ModelTypeSetToObjectIdSet(models));

  invalidation_notifier_->SetStateDeprecated("fake_state");
  invalidation_notifier_->SetUniqueId("fake_id");
  invalidation_notifier_->UpdateCredentials("foo@bar.com", "fake_token");

  invalidation_notifier_->OnNotificationsEnabled();
  invalidation_notifier_->OnIncomingNotification(
      ModelTypePayloadMapToObjectIdPayloadMap(type_payloads),
      REMOTE_NOTIFICATION);
  invalidation_notifier_->OnNotificationsDisabled(
      TRANSIENT_NOTIFICATION_ERROR);
  invalidation_notifier_->OnNotificationsDisabled(
      NOTIFICATION_CREDENTIALS_REJECTED);

  ui_loop_.RunAllPending();
}

}  // namespace

}  // namespace syncer
