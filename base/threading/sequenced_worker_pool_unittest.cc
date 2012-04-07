// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>

#include "base/bind.h"
#include "base/memory/ref_counted.h"
#include "base/synchronization/condition_variable.h"
#include "base/synchronization/lock.h"
#include "base/threading/platform_thread.h"
#include "base/threading/sequenced_worker_pool.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {

// IMPORTANT NOTE:
//
// Many of these tests have failure modes where they'll hang forever. These
// tests should not be flaky, and hangling indicates a type of failure. Do not
// mark as flaky if they're hanging, it's likely an actual bug.

namespace {

const size_t kNumWorkerThreads = 3;

// Allows a number of threads to all be blocked on the same event, and
// provides a way to unblock a certain number of them.
class ThreadBlocker {
 public:
  ThreadBlocker() : lock_(), cond_var_(&lock_), unblock_counter_(0) {
  }

  void Block() {
    {
      base::AutoLock lock(lock_);
      while (unblock_counter_ == 0)
        cond_var_.Wait();
      unblock_counter_--;
    }
    cond_var_.Signal();
  }

  void Unblock(size_t count) {
    {
      base::AutoLock lock(lock_);
      DCHECK(unblock_counter_ == 0);
      unblock_counter_ = count;
    }
    cond_var_.Signal();
  }

 private:
  base::Lock lock_;
  base::ConditionVariable cond_var_;

  size_t unblock_counter_;
};

class TestTracker : public base::RefCountedThreadSafe<TestTracker> {
 public:
  TestTracker()
      : lock_(),
        cond_var_(&lock_),
        started_events_(0) {
  }

  // Each of these tasks appends the argument to the complete sequence vector
  // so calling code can see what order they finished in.
  void FastTask(int id) {
    SignalWorkerDone(id);
  }
  void SlowTask(int id) {
    base::PlatformThread::Sleep(1000);
    SignalWorkerDone(id);
  }

  void BlockTask(int id, ThreadBlocker* blocker) {
    // Note that this task has started and signal anybody waiting for that
    // to happen.
    {
      base::AutoLock lock(lock_);
      started_events_++;
    }
    cond_var_.Signal();

    blocker->Block();
    SignalWorkerDone(id);
  }

  // Waits until the given number of tasks have started executing.
  void WaitUntilTasksBlocked(size_t count) {
    {
      base::AutoLock lock(lock_);
      while (started_events_ < count)
        cond_var_.Wait();
    }
    cond_var_.Signal();
  }

  // Blocks the current thread until at least the given number of tasks are in
  // the completed vector, and then returns a copy.
  std::vector<int> WaitUntilTasksComplete(size_t num_tasks) {
    std::vector<int> ret;
    {
      base::AutoLock lock(lock_);
      while (complete_sequence_.size() < num_tasks)
        cond_var_.Wait();
      ret = complete_sequence_;
    }
    cond_var_.Signal();
    return ret;
  }

  void ClearCompleteSequence() {
    base::AutoLock lock(lock_);
    complete_sequence_.clear();
    started_events_ = 0;
  }

 private:
  void SignalWorkerDone(int id) {
    {
      base::AutoLock lock(lock_);
      complete_sequence_.push_back(id);
    }
    cond_var_.Signal();
  }

  // Protects the complete_sequence.
  base::Lock lock_;

  base::ConditionVariable cond_var_;

  // Protected by lock_.
  std::vector<int> complete_sequence_;

  // Counter of the number of "block" workers that have started.
  size_t started_events_;
};

class SequencedWorkerPoolTest : public testing::Test,
                                public SequencedWorkerPool::TestingObserver {
 public:
  SequencedWorkerPoolTest()
      : pool_(kNumWorkerThreads, "test"),
        tracker_(new TestTracker) {
    pool_.SetTestingObserver(this);
  }
  ~SequencedWorkerPoolTest() {
  }

  virtual void SetUp() {
  }
  virtual void TearDown() {
    pool_.Shutdown();
  }

  SequencedWorkerPool& pool() { return pool_; }
  TestTracker* tracker() { return tracker_.get(); }

  // Ensures that the given number of worker threads is created by adding
  // tasks and waiting until they complete. Worker thread creation is
  // serialized, can happen on background threads asynchronously, and doesn't
  // happen any more at shutdown. This means that if a test posts a bunch of
  // tasks and calls shutdown, fewer workers will be created than the test may
  // expect.
  //
  // This function ensures that this condition can't happen so tests can make
  // assumptions about the number of workers active. See the comment in
  // PrepareToStartAdditionalThreadIfNecessary in the .cc file for more
  // details.
  //
  // It will post tasks to the queue with id -1. It also assumes this is the
  // first thing called in a test since it will clear the complete_sequence_.
  void EnsureAllWorkersCreated() {
    // Create a bunch of threads, all waiting. This will cause that may
    // workers to be created.
    ThreadBlocker blocker;
    for (size_t i = 0; i < kNumWorkerThreads; i++) {
      pool().PostWorkerTask(FROM_HERE,
                            base::Bind(&TestTracker::BlockTask,
                                       tracker(), -1, &blocker));
    }
    tracker()->WaitUntilTasksBlocked(kNumWorkerThreads);

    // Now wake them up and wait until they're done.
    blocker.Unblock(kNumWorkerThreads);
    tracker()->WaitUntilTasksComplete(kNumWorkerThreads);

    // Clean up the task IDs we added.
    tracker()->ClearCompleteSequence();
  }

 protected:
  // This closure will be executed right before the pool blocks on shutdown.
  base::Closure before_wait_for_shutdown_;

 private:
  // SequencedWorkerPool::TestingObserver implementation.
  virtual void WillWaitForShutdown() {
    if (!before_wait_for_shutdown_.is_null())
      before_wait_for_shutdown_.Run();
  }

  SequencedWorkerPool pool_;
  scoped_refptr<TestTracker> tracker_;
};

// Checks that the given number of entries are in the tasks to complete of
// the given tracker, and then signals the given event the given number of
// times. This is used to wakt up blocked background threads before blocking
// on shutdown.
void EnsureTasksToCompleteCountAndUnblock(scoped_refptr<TestTracker> tracker,
                                          size_t expected_tasks_to_complete,
                                          ThreadBlocker* blocker,
                                          size_t threads_to_awake) {
  EXPECT_EQ(
      expected_tasks_to_complete,
      tracker->WaitUntilTasksComplete(expected_tasks_to_complete).size());

  blocker->Unblock(threads_to_awake);
}

}  // namespace

// Tests that same-named tokens have the same ID.
TEST_F(SequencedWorkerPoolTest, NamedTokens) {
  const std::string name1("hello");
  SequencedWorkerPool::SequenceToken token1 =
      pool().GetNamedSequenceToken(name1);

  SequencedWorkerPool::SequenceToken token2 = pool().GetSequenceToken();

  const std::string name3("goodbye");
  SequencedWorkerPool::SequenceToken token3 =
      pool().GetNamedSequenceToken(name3);

  // All 3 tokens should be different.
  EXPECT_FALSE(token1.Equals(token2));
  EXPECT_FALSE(token1.Equals(token3));
  EXPECT_FALSE(token2.Equals(token3));

  // Requesting the same name again should give the same value.
  SequencedWorkerPool::SequenceToken token1again =
      pool().GetNamedSequenceToken(name1);
  EXPECT_TRUE(token1.Equals(token1again));

  SequencedWorkerPool::SequenceToken token3again =
      pool().GetNamedSequenceToken(name3);
  EXPECT_TRUE(token3.Equals(token3again));
}

// Tests that posting a bunch of tasks (many more than the number of worker
// threads) runs them all.
TEST_F(SequencedWorkerPoolTest, LotsOfTasks) {
  pool().PostWorkerTask(FROM_HERE,
                        base::Bind(&TestTracker::SlowTask, tracker(), 0));

  const size_t kNumTasks = 20;
  for (size_t i = 1; i < kNumTasks; i++) {
    pool().PostWorkerTask(FROM_HERE,
                          base::Bind(&TestTracker::FastTask, tracker(), i));
  }

  std::vector<int> result = tracker()->WaitUntilTasksComplete(kNumTasks);
  EXPECT_EQ(kNumTasks, result.size());
}

// Test that tasks with the same sequence token are executed in order but don't
// affect other tasks.
TEST_F(SequencedWorkerPoolTest, Sequence) {
  // Fill all the worker threads except one.
  const size_t kNumBackgroundTasks = kNumWorkerThreads - 1;
  ThreadBlocker background_blocker;
  for (size_t i = 0; i < kNumBackgroundTasks; i++) {
    pool().PostWorkerTask(FROM_HERE,
                          base::Bind(&TestTracker::BlockTask,
                                     tracker(), i, &background_blocker));
  }
  tracker()->WaitUntilTasksBlocked(kNumBackgroundTasks);

  // Create two tasks with the same sequence token, one that will block on the
  // event, and one which will just complete quickly when it's run. Since there
  // is one worker thread free, the first task will start and then block, and
  // the second task should be waiting.
  ThreadBlocker blocker;
  SequencedWorkerPool::SequenceToken token1 = pool().GetSequenceToken();
  pool().PostSequencedWorkerTask(
      token1, FROM_HERE,
      base::Bind(&TestTracker::BlockTask, tracker(), 100, &blocker));
  pool().PostSequencedWorkerTask(
      token1, FROM_HERE,
      base::Bind(&TestTracker::FastTask, tracker(), 101));
  EXPECT_EQ(0u, tracker()->WaitUntilTasksComplete(0).size());

  // Create another two tasks as above with a different token. These will be
  // blocked since there are no slots to run.
  SequencedWorkerPool::SequenceToken token2 = pool().GetSequenceToken();
  pool().PostSequencedWorkerTask(
      token2, FROM_HERE,
      base::Bind(&TestTracker::FastTask, tracker(), 200));
  pool().PostSequencedWorkerTask(
      token2, FROM_HERE,
      base::Bind(&TestTracker::FastTask, tracker(), 201));
  EXPECT_EQ(0u, tracker()->WaitUntilTasksComplete(0).size());

  // Let one background task complete. This should then let both tasks of
  // token2 run to completion in order. The second task of token1 should still
  // be blocked.
  background_blocker.Unblock(1);
  std::vector<int> result = tracker()->WaitUntilTasksComplete(3);
  ASSERT_EQ(3u, result.size());
  EXPECT_EQ(200, result[1]);
  EXPECT_EQ(201, result[2]);

  // Finish the rest of the background tasks. This should leave some workers
  // free with the second token1 task still blocked on the first.
  background_blocker.Unblock(kNumBackgroundTasks - 1);
  EXPECT_EQ(kNumBackgroundTasks + 2,
            tracker()->WaitUntilTasksComplete(kNumBackgroundTasks + 2).size());

  // Allow the first task of token1 to complete. This should run the second.
  blocker.Unblock(1);
  result = tracker()->WaitUntilTasksComplete(kNumBackgroundTasks + 4);
  ASSERT_EQ(kNumBackgroundTasks + 4, result.size());
  EXPECT_EQ(100, result[result.size() - 2]);
  EXPECT_EQ(101, result[result.size() - 1]);
}

// Tests that unrun tasks are discarded properly according to their shutdown
// mode.
TEST_F(SequencedWorkerPoolTest, DiscardOnShutdown) {
  // Start tasks to take all the threads and block them.
  EnsureAllWorkersCreated();
  ThreadBlocker blocker;
  for (size_t i = 0; i < kNumWorkerThreads; i++) {
    pool().PostWorkerTask(FROM_HERE,
                          base::Bind(&TestTracker::BlockTask,
                                     tracker(), i, &blocker));
  }
  tracker()->WaitUntilTasksBlocked(kNumWorkerThreads);

  // Create some tasks with different shutdown modes.
  pool().PostWorkerTaskWithShutdownBehavior(
      FROM_HERE,
      base::Bind(&TestTracker::FastTask, tracker(), 100),
      SequencedWorkerPool::CONTINUE_ON_SHUTDOWN);
  pool().PostWorkerTaskWithShutdownBehavior(
      FROM_HERE,
      base::Bind(&TestTracker::FastTask, tracker(), 101),
      SequencedWorkerPool::SKIP_ON_SHUTDOWN);
  pool().PostWorkerTaskWithShutdownBehavior(
      FROM_HERE,
      base::Bind(&TestTracker::FastTask, tracker(), 102),
      SequencedWorkerPool::BLOCK_SHUTDOWN);

  // Shutdown the worker pool. This should discard all non-blocking tasks.
  before_wait_for_shutdown_ =
      base::Bind(&EnsureTasksToCompleteCountAndUnblock,
                 scoped_refptr<TestTracker>(tracker()), 0,
                 &blocker, kNumWorkerThreads);
  pool().Shutdown();

  std::vector<int> result = tracker()->WaitUntilTasksComplete(4);

  // The kNumWorkerThread items should have completed, plus the BLOCK_SHUTDOWN
  // one, in no particular order.
  ASSERT_EQ(4u, result.size());
  for (size_t i = 0; i < kNumWorkerThreads; i++) {
    EXPECT_TRUE(std::find(result.begin(), result.end(), static_cast<int>(i)) !=
                result.end());
  }
  EXPECT_TRUE(std::find(result.begin(), result.end(), 102) != result.end());
}

// Tests that CONTINUE_ON_SHUTDOWN tasks don't block shutdown.
TEST_F(SequencedWorkerPoolTest, ContinueOnShutdown) {
  EnsureAllWorkersCreated();
  ThreadBlocker blocker;
  pool().PostWorkerTaskWithShutdownBehavior(
      FROM_HERE,
      base::Bind(&TestTracker::BlockTask,
                 tracker(), 0, &blocker),
      SequencedWorkerPool::CONTINUE_ON_SHUTDOWN);
  tracker()->WaitUntilTasksBlocked(1);

  // This should not block. If this test hangs, it means it failed.
  pool().Shutdown();

  // The task should not have completed yet.
  EXPECT_EQ(0u, tracker()->WaitUntilTasksComplete(0).size());

  // Posting more tasks should fail.
  EXPECT_FALSE(pool().PostWorkerTaskWithShutdownBehavior(
      FROM_HERE, base::Bind(&TestTracker::FastTask, tracker(), 0),
      SequencedWorkerPool::CONTINUE_ON_SHUTDOWN));

  // Continue the background thread and make sure the task can complete.
  blocker.Unblock(1);
  std::vector<int> result = tracker()->WaitUntilTasksComplete(1);
  EXPECT_EQ(1u, result.size());
}

}  // namespace base
