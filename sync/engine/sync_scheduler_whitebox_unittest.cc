// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/message_loop.h"
#include "base/time.h"
#include "sync/engine/sync_scheduler_impl.h"
#include "sync/engine/throttled_data_type_tracker.h"
#include "sync/sessions/sync_session_context.h"
#include "sync/sessions/test_util.h"
#include "sync/test/engine/fake_model_worker.h"
#include "sync/test/engine/mock_connection_manager.h"
#include "sync/test/engine/test_directory_setter_upper.h"
#include "sync/test/fake_extensions_activity_monitor.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using base::TimeDelta;
using base::TimeTicks;

namespace syncer {
using sessions::SyncSession;
using sessions::SyncSessionContext;
using sessions::SyncSourceInfo;
using sync_pb::GetUpdatesCallerInfo;

class SyncSchedulerWhiteboxTest : public testing::Test {
 public:
  virtual void SetUp() {
    dir_maker_.SetUp();
    Syncer* syncer = new Syncer();

    ModelSafeRoutingInfo routes;
    routes[BOOKMARKS] = GROUP_UI;
    routes[NIGORI] = GROUP_PASSIVE;

    workers_.push_back(make_scoped_refptr(new FakeModelWorker(GROUP_UI)));
    workers_.push_back(make_scoped_refptr(new FakeModelWorker(GROUP_PASSIVE)));

    std::vector<ModelSafeWorker*> workers;
    for (std::vector<scoped_refptr<FakeModelWorker> >::iterator it =
         workers_.begin(); it != workers_.end(); ++it) {
      workers.push_back(it->get());
    }

    connection_.reset(new MockConnectionManager(NULL));
    throttled_data_type_tracker_.reset(new ThrottledDataTypeTracker(NULL));
    context_.reset(
        new SyncSessionContext(
            connection_.get(), dir_maker_.directory(),
            workers, &extensions_activity_monitor_,
            throttled_data_type_tracker_.get(),
            std::vector<SyncEngineEventListener*>(), NULL, NULL,
            true  /* enable keystore encryption */));
    context_->set_notifications_enabled(true);
    context_->set_account_name("Test");
    scheduler_.reset(
        new SyncSchedulerImpl("TestSyncSchedulerWhitebox", context(), syncer));
  }

  virtual void TearDown() {
    scheduler_.reset();
  }

  void SetMode(SyncScheduler::Mode mode) {
    scheduler_->mode_ = mode;
  }

  void SetLastSyncedTime(base::TimeTicks ticks) {
    scheduler_->last_sync_session_end_time_ = ticks;
  }

  void ResetWaitInterval() {
    scheduler_->wait_interval_.reset();
  }

  void SetWaitIntervalToThrottled() {
    scheduler_->wait_interval_.reset(new SyncSchedulerImpl::WaitInterval(
        SyncSchedulerImpl::WaitInterval::THROTTLED, TimeDelta::FromSeconds(1)));
  }

  void SetWaitIntervalToExponentialBackoff() {
    scheduler_->wait_interval_.reset(
       new SyncSchedulerImpl::WaitInterval(
       SyncSchedulerImpl::WaitInterval::EXPONENTIAL_BACKOFF,
       TimeDelta::FromSeconds(1)));
  }

  void SetWaitIntervalHadNudge(bool had_nudge) {
    scheduler_->wait_interval_->had_nudge = had_nudge;
  }

  SyncSchedulerImpl::JobProcessDecision DecideOnJob(
      const SyncSchedulerImpl::SyncSessionJob& job) {
    return scheduler_->DecideOnJob(job);
  }

  void InitializeSyncerOnNormalMode() {
    SetMode(SyncScheduler::NORMAL_MODE);
    ResetWaitInterval();
    SetLastSyncedTime(base::TimeTicks::Now());
  }

  SyncSchedulerImpl::JobProcessDecision CreateAndDecideJob(
      SyncSchedulerImpl::SyncSessionJob::SyncSessionJobPurpose purpose) {
    SyncSession* s = scheduler_->CreateSyncSession(SyncSourceInfo());
    SyncSchedulerImpl::SyncSessionJob job(purpose, TimeTicks::Now(),
         make_linked_ptr(s),
         false,
         ConfigurationParams(),
         FROM_HERE);
    return DecideOnJob(job);
  }

  SyncSessionContext* context() { return context_.get(); }

 private:
  MessageLoop message_loop_;
  scoped_ptr<MockConnectionManager> connection_;
  scoped_ptr<SyncSessionContext> context_;
  std::vector<scoped_refptr<FakeModelWorker> > workers_;
  FakeExtensionsActivityMonitor extensions_activity_monitor_;
  scoped_ptr<ThrottledDataTypeTracker> throttled_data_type_tracker_;
  TestDirectorySetterUpper dir_maker_;

 protected:
  // Declared here to ensure it is destructed before the objects it references.
  scoped_ptr<SyncSchedulerImpl> scheduler_;
};

TEST_F(SyncSchedulerWhiteboxTest, SaveNudge) {
  InitializeSyncerOnNormalMode();

  // Now set the mode to configure.
  SetMode(SyncScheduler::CONFIGURATION_MODE);

  SyncSchedulerImpl::JobProcessDecision decision =
      CreateAndDecideJob(SyncSchedulerImpl::SyncSessionJob::NUDGE);

  EXPECT_EQ(decision, SyncSchedulerImpl::SAVE);
}

TEST_F(SyncSchedulerWhiteboxTest, SaveNudgeWhileTypeThrottled) {
  InitializeSyncerOnNormalMode();

  ModelTypeSet types;
  types.Put(BOOKMARKS);

  // Mark bookmarks as throttled.
  context()->throttled_data_type_tracker()->SetUnthrottleTime(
      types, base::TimeTicks::Now() + base::TimeDelta::FromHours(2));

  ModelTypePayloadMap types_with_payload;
  types_with_payload[BOOKMARKS] = "";

  SyncSourceInfo info(GetUpdatesCallerInfo::LOCAL, types_with_payload);
  SyncSession* s = scheduler_->CreateSyncSession(info);

  // Now schedule a nudge with just bookmarks and the change is local.
  SyncSchedulerImpl::SyncSessionJob job(
        SyncSchedulerImpl::SyncSessionJob::NUDGE,
        TimeTicks::Now(),
        make_linked_ptr(s),
        false,
        ConfigurationParams(),
        FROM_HERE);

  SyncSchedulerImpl::JobProcessDecision decision = DecideOnJob(job);
  EXPECT_EQ(decision, SyncSchedulerImpl::SAVE);
}

TEST_F(SyncSchedulerWhiteboxTest, ContinueNudge) {
  InitializeSyncerOnNormalMode();

  SyncSchedulerImpl::JobProcessDecision decision = CreateAndDecideJob(
      SyncSchedulerImpl::SyncSessionJob::NUDGE);

  EXPECT_EQ(decision, SyncSchedulerImpl::CONTINUE);
}

TEST_F(SyncSchedulerWhiteboxTest, DropPoll) {
  InitializeSyncerOnNormalMode();
  SetMode(SyncScheduler::CONFIGURATION_MODE);

  SyncSchedulerImpl::JobProcessDecision decision = CreateAndDecideJob(
      SyncSchedulerImpl::SyncSessionJob::POLL);

  EXPECT_EQ(decision, SyncSchedulerImpl::DROP);
}

TEST_F(SyncSchedulerWhiteboxTest, ContinuePoll) {
  InitializeSyncerOnNormalMode();

  SyncSchedulerImpl::JobProcessDecision decision = CreateAndDecideJob(
      SyncSchedulerImpl::SyncSessionJob::POLL);

  EXPECT_EQ(decision, SyncSchedulerImpl::CONTINUE);
}

TEST_F(SyncSchedulerWhiteboxTest, ContinueConfiguration) {
  InitializeSyncerOnNormalMode();
  SetMode(SyncScheduler::CONFIGURATION_MODE);

  SyncSchedulerImpl::JobProcessDecision decision = CreateAndDecideJob(
      SyncSchedulerImpl::SyncSessionJob::CONFIGURATION);

  EXPECT_EQ(decision, SyncSchedulerImpl::CONTINUE);
}

TEST_F(SyncSchedulerWhiteboxTest, SaveConfigurationWhileThrottled) {
  InitializeSyncerOnNormalMode();
  SetMode(SyncScheduler::CONFIGURATION_MODE);

  SetWaitIntervalToThrottled();

  SyncSchedulerImpl::JobProcessDecision decision = CreateAndDecideJob(
      SyncSchedulerImpl::SyncSessionJob::CONFIGURATION);

  EXPECT_EQ(decision, SyncSchedulerImpl::SAVE);
}

TEST_F(SyncSchedulerWhiteboxTest, SaveNudgeWhileThrottled) {
  InitializeSyncerOnNormalMode();
  SetMode(SyncScheduler::CONFIGURATION_MODE);

  SetWaitIntervalToThrottled();

  SyncSchedulerImpl::JobProcessDecision decision = CreateAndDecideJob(
      SyncSchedulerImpl::SyncSessionJob::NUDGE);

  EXPECT_EQ(decision, SyncSchedulerImpl::SAVE);
}

TEST_F(SyncSchedulerWhiteboxTest, ContinueNudgeWhileExponentialBackOff) {
  InitializeSyncerOnNormalMode();
  SetMode(SyncScheduler::NORMAL_MODE);
  SetWaitIntervalToExponentialBackoff();

  SyncSchedulerImpl::JobProcessDecision decision = CreateAndDecideJob(
      SyncSchedulerImpl::SyncSessionJob::NUDGE);

  EXPECT_EQ(decision, SyncSchedulerImpl::CONTINUE);
}

TEST_F(SyncSchedulerWhiteboxTest, DropNudgeWhileExponentialBackOff) {
  InitializeSyncerOnNormalMode();
  SetMode(SyncScheduler::NORMAL_MODE);
  SetWaitIntervalToExponentialBackoff();
  SetWaitIntervalHadNudge(true);

  SyncSchedulerImpl::JobProcessDecision decision = CreateAndDecideJob(
      SyncSchedulerImpl::SyncSessionJob::NUDGE);

  EXPECT_EQ(decision, SyncSchedulerImpl::DROP);
}

TEST_F(SyncSchedulerWhiteboxTest, ContinueCanaryJobConfig) {
  InitializeSyncerOnNormalMode();
  SetMode(SyncScheduler::CONFIGURATION_MODE);
  SetWaitIntervalToExponentialBackoff();

  struct SyncSchedulerImpl::SyncSessionJob job;
  job.purpose = SyncSchedulerImpl::SyncSessionJob::CONFIGURATION;
  job.scheduled_start = TimeTicks::Now();
  job.is_canary_job = true;
  SyncSchedulerImpl::JobProcessDecision decision = DecideOnJob(job);

  EXPECT_EQ(decision, SyncSchedulerImpl::CONTINUE);
}

}  // namespace syncer
