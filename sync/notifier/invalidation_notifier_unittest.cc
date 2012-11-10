// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sync/notifier/invalidation_notifier.h"

#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "jingle/notifier/base/fake_base_task.h"
#include "jingle/notifier/base/notifier_options.h"
#include "jingle/notifier/listener/fake_push_client.h"
#include "net/url_request/url_request_test_util.h"
#include "sync/internal_api/public/base/model_type.h"
#include "sync/internal_api/public/base/model_type_payload_map.h"
#include "sync/internal_api/public/util/weak_handle.h"
#include "sync/notifier/invalidation_state_tracker.h"
#include "sync/notifier/mock_invalidation_state_tracker.h"
#include "sync/notifier/mock_sync_notifier_observer.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace syncer {

namespace {

using ::testing::InSequence;
using ::testing::StrictMock;

class InvalidationNotifierTest : public testing::Test {
 protected:
  virtual void TearDown() {
    if (invalidation_notifier_.get())
      ResetNotifier();
  }

  // Constructs an InvalidationNotifier, places it in |invalidation_notifier_|,
  // and registers |mock_observer_| as a handler. This remains in place until
  // either TearDown (automatic) or ResetNotifier (manual) is called.
  void CreateNotifier(
      const std::string& initial_invalidation_state) {
    notifier::NotifierOptions notifier_options;
    // Note: URLRequestContextGetters are ref-counted.
    notifier_options.request_context_getter =
        new TestURLRequestContextGetter(message_loop_.message_loop_proxy());
    invalidation_notifier_.reset(
        new InvalidationNotifier(
            scoped_ptr<notifier::PushClient>(new notifier::FakePushClient()),
            InvalidationVersionMap(),
            initial_invalidation_state,
            MakeWeakHandle(mock_tracker_.AsWeakPtr()),
            "fake_client_info"));
    invalidation_notifier_->RegisterHandler(&mock_observer_);
  }

  void ResetNotifier() {
    invalidation_notifier_->UnregisterHandler(&mock_observer_);
    // Stopping the invalidation notifier stops its scheduler, which deletes any
    // pending tasks without running them.  Some tasks "run and delete" another
    // task, so they must be run in order to avoid leaking the inner task.
    // Stopping does not schedule any tasks, so it's both necessary and
    // sufficient to drain the task queue before stopping the notifier.
    message_loop_.RunAllPending();
    invalidation_notifier_.reset();
  }

  MessageLoopForIO message_loop_;
  scoped_ptr<InvalidationNotifier> invalidation_notifier_;
  StrictMock<MockInvalidationStateTracker> mock_tracker_;
  StrictMock<MockSyncNotifierObserver> mock_observer_;
  notifier::FakeBaseTask fake_base_task_;
};

TEST_F(InvalidationNotifierTest, Basic) {
  InSequence dummy;

  CreateNotifier("fake_state");

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
  // Note no expectation on mock_tracker_, as we initialized with
  // non-empty initial_invalidation_state above.

  invalidation_notifier_->UpdateRegisteredIds(
      &mock_observer_, ModelTypeSetToObjectIdSet(models));

  // TODO(tim): This call should be a no-op, Remove once bug 124140 and
  // associated issues are fixed.
  invalidation_notifier_->SetStateDeprecated("fake_state");
  invalidation_notifier_->SetUniqueId("fake_id");
  invalidation_notifier_->UpdateCredentials("foo@bar.com", "fake_token");

  invalidation_notifier_->OnNotificationsEnabled();

  invalidation_notifier_->OnInvalidate(
      ModelTypePayloadMapToObjectIdPayloadMap(type_payloads));

  invalidation_notifier_->OnNotificationsDisabled(
      TRANSIENT_NOTIFICATION_ERROR);
  invalidation_notifier_->OnNotificationsDisabled(
      NOTIFICATION_CREDENTIALS_REJECTED);
}

TEST_F(InvalidationNotifierTest, MigrateState) {
  CreateNotifier(std::string());

  EXPECT_CALL(mock_tracker_, SetInvalidationState("fake_state"));
  invalidation_notifier_->SetStateDeprecated("fake_state");

  // Should do nothing.
  invalidation_notifier_->SetStateDeprecated("spurious_fake_state");

  // Pretend Chrome shut down.
  ResetNotifier();

  CreateNotifier("fake_state");
  // Should do nothing.
  invalidation_notifier_->SetStateDeprecated("more_spurious_fake_state");
}

}  // namespace

}  // namespace syncer
