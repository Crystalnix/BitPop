// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/threading/thread_restrictions.h"
#include "build/build_config.h"
#include "chrome/browser/metrics/metrics_service.h"
#include "chrome/browser/metrics/thread_watcher.h"
#include "content/common/notification_service.h"

#if defined(OS_WIN)
#include <Objbase.h>
#endif

// static
const int ThreadWatcher::kPingCount = 3;

// ThreadWatcher methods and members.
ThreadWatcher::ThreadWatcher(const BrowserThread::ID& thread_id,
                             const std::string& thread_name,
                             const base::TimeDelta& sleep_time,
                             const base::TimeDelta& unresponsive_time)
    : thread_id_(thread_id),
      thread_name_(thread_name),
      sleep_time_(sleep_time),
      unresponsive_time_(unresponsive_time),
      ping_time_(base::TimeTicks::Now()),
      ping_sequence_number_(0),
      active_(false),
      ping_count_(kPingCount),
      histogram_(NULL),
      ALLOW_THIS_IN_INITIALIZER_LIST(method_factory_(this)) {
  Initialize();
}

ThreadWatcher::~ThreadWatcher() {}

// static
void ThreadWatcher::StartWatching(const BrowserThread::ID& thread_id,
                                  const std::string& thread_name,
                                  const base::TimeDelta& sleep_time,
                                  const base::TimeDelta& unresponsive_time) {
  DCHECK_GE(sleep_time.InMilliseconds(), 0);
  DCHECK_GE(unresponsive_time.InMilliseconds(), sleep_time.InMilliseconds());

  // If we are not on WatchDogThread, then post a task to call StartWatching on
  // WatchDogThread.
  if (!WatchDogThread::CurrentlyOnWatchDogThread()) {
    WatchDogThread::PostTask(
        FROM_HERE,
        NewRunnableFunction(
            &ThreadWatcher::StartWatching,
            thread_id, thread_name, sleep_time, unresponsive_time));
    return;
  }

  DCHECK(WatchDogThread::CurrentlyOnWatchDogThread());

  // Create a new thread watcher object for the given thread and activate it.
  ThreadWatcher* watcher =
      new ThreadWatcher(thread_id, thread_name, sleep_time, unresponsive_time);
  DCHECK(watcher);
  // If we couldn't register the thread watcher object, we are shutting down,
  // then don't activate thread watching.
  if (!ThreadWatcherList::IsRegistered(thread_id))
    return;
  watcher->ActivateThreadWatching();
}

void ThreadWatcher::ActivateThreadWatching() {
  DCHECK(WatchDogThread::CurrentlyOnWatchDogThread());
  if (active_) return;
  active_ = true;
  ping_count_ = kPingCount;
  MessageLoop::current()->PostTask(
      FROM_HERE,
      method_factory_.NewRunnableMethod(&ThreadWatcher::PostPingMessage));
}

void ThreadWatcher::DeActivateThreadWatching() {
  DCHECK(WatchDogThread::CurrentlyOnWatchDogThread());
  active_ = false;
  ping_count_ = 0;
  method_factory_.RevokeAll();
}

void ThreadWatcher::WakeUp() {
  DCHECK(WatchDogThread::CurrentlyOnWatchDogThread());
  // There is some user activity, PostPingMessage task of thread watcher if
  // needed.
  if (!active_) return;

  if (ping_count_ <= 0) {
    ping_count_ = kPingCount;
    PostPingMessage();
  } else {
    ping_count_ = kPingCount;
  }
}

void ThreadWatcher::PostPingMessage() {
  DCHECK(WatchDogThread::CurrentlyOnWatchDogThread());
  // If we have stopped watching or if the user is idle, then stop sending
  // ping messages.
  if (!active_ || ping_count_ <= 0)
    return;

  // Save the current time when we have sent ping message.
  ping_time_ = base::TimeTicks::Now();

  // Send a ping message to the watched thread.
  Task* callback_task = method_factory_.NewRunnableMethod(
      &ThreadWatcher::OnPongMessage, ping_sequence_number_);
  if (BrowserThread::PostTask(
          thread_id(),
          FROM_HERE,
          NewRunnableFunction(
              &ThreadWatcher::OnPingMessage, thread_id_, callback_task))) {
      // Post a task to check the responsiveness of watched thread.
      MessageLoop::current()->PostDelayedTask(
          FROM_HERE,
          method_factory_.NewRunnableMethod(
              &ThreadWatcher::OnCheckResponsiveness, ping_sequence_number_),
          unresponsive_time_.InMilliseconds());
  } else {
    // Watched thread might have gone away, stop watching it.
    delete callback_task;
    DeActivateThreadWatching();
  }
}

void ThreadWatcher::OnPongMessage(uint64 ping_sequence_number) {
  DCHECK(WatchDogThread::CurrentlyOnWatchDogThread());
  // Record watched thread's response time.
  base::TimeDelta response_time = base::TimeTicks::Now() - ping_time_;
  histogram_->AddTime(response_time);

  // Check if there are any extra pings in flight.
  DCHECK_EQ(ping_sequence_number_, ping_sequence_number);
  if (ping_sequence_number_ != ping_sequence_number)
    return;

  // Increment sequence number for the next ping message to indicate watched
  // thread is responsive.
  ++ping_sequence_number_;

  // If we have stopped watching or if the user is idle, then stop sending
  // ping messages.
  if (!active_ || --ping_count_ <= 0)
    return;

  MessageLoop::current()->PostDelayedTask(
      FROM_HERE,
      method_factory_.NewRunnableMethod(&ThreadWatcher::PostPingMessage),
      sleep_time_.InMilliseconds());
}

bool ThreadWatcher::OnCheckResponsiveness(uint64 ping_sequence_number) {
  DCHECK(WatchDogThread::CurrentlyOnWatchDogThread());
  // If we have stopped watching then consider thread as responding.
  if (!active_)
    return true;
  // If the latest ping_sequence_number_ is not same as the ping_sequence_number
  // that is passed in, then we can assume OnPongMessage was called.
  // OnPongMessage increments ping_sequence_number_.
  return ping_sequence_number_ != ping_sequence_number;
}

void ThreadWatcher::Initialize() {
  ThreadWatcherList::Register(this);
  const std::string histogram_name =
      "ThreadWatcher.ResponseTime." + thread_name_;
  histogram_ = base::Histogram::FactoryTimeGet(
      histogram_name,
      base::TimeDelta::FromMilliseconds(1),
      base::TimeDelta::FromSeconds(100), 50,
      base::Histogram::kUmaTargetedHistogramFlag);
}

// static
void ThreadWatcher::OnPingMessage(const BrowserThread::ID& thread_id,
                                  Task* callback_task) {
  // This method is called on watched thread.
  DCHECK(BrowserThread::CurrentlyOn(thread_id));
  WatchDogThread::PostTask(FROM_HERE, callback_task);
}

// ThreadWatcherList methods and members.
//
// static
ThreadWatcherList* ThreadWatcherList::global_ = NULL;

ThreadWatcherList::ThreadWatcherList()
    : last_wakeup_time_(base::TimeTicks::Now()) {
  // Assert we are not running on WATCHDOG thread. Would be ideal to assert we
  // are on UI thread, but Unit tests are not running on UI thread.
  DCHECK(!WatchDogThread::CurrentlyOnWatchDogThread());
  CHECK(!global_);
  global_ = this;
  // Register Notifications observer.
  MetricsService::SetUpNotifications(&registrar_, this);
}

ThreadWatcherList::~ThreadWatcherList() {
  base::AutoLock auto_lock(lock_);
  DCHECK(this == global_);
  global_ = NULL;
}

// static
void ThreadWatcherList::Register(ThreadWatcher* watcher) {
  if (!global_)
    return;
  base::AutoLock auto_lock(global_->lock_);
  DCHECK(!global_->PreLockedFind(watcher->thread_id()));
  global_->registered_[watcher->thread_id()] = watcher;
}

// static
bool ThreadWatcherList::IsRegistered(const BrowserThread::ID thread_id) {
  return NULL != ThreadWatcherList::Find(thread_id);
}

// static
void ThreadWatcherList::StartWatchingAll() {
  if (!WatchDogThread::CurrentlyOnWatchDogThread()) {
    WatchDogThread::PostDelayedTask(
        FROM_HERE,
        NewRunnableFunction(&ThreadWatcherList::StartWatchingAll),
        base::TimeDelta::FromSeconds(5).InMilliseconds());
    return;
  }
  DCHECK(WatchDogThread::CurrentlyOnWatchDogThread());
  const base::TimeDelta kSleepTime = base::TimeDelta::FromSeconds(5);
  const base::TimeDelta kUnresponsiveTime = base::TimeDelta::FromSeconds(10);
  if (BrowserThread::IsMessageLoopValid(BrowserThread::UI)) {
    ThreadWatcher::StartWatching(BrowserThread::UI, "UI", kSleepTime,
                                 kUnresponsiveTime);
  }
  if (BrowserThread::IsMessageLoopValid(BrowserThread::IO)) {
    ThreadWatcher::StartWatching(BrowserThread::IO, "IO", kSleepTime,
                                 kUnresponsiveTime);
  }
  if (BrowserThread::IsMessageLoopValid(BrowserThread::DB)) {
    ThreadWatcher::StartWatching(BrowserThread::DB, "DB", kSleepTime,
                                 kUnresponsiveTime);
  }
  if (BrowserThread::IsMessageLoopValid(BrowserThread::FILE)) {
    ThreadWatcher::StartWatching(BrowserThread::FILE, "FILE", kSleepTime,
                                 kUnresponsiveTime);
  }
  if (BrowserThread::IsMessageLoopValid(BrowserThread::CACHE)) {
    ThreadWatcher::StartWatching(BrowserThread::CACHE, "CACHE", kSleepTime,
                                 kUnresponsiveTime);
  }
}

// static
void ThreadWatcherList::StopWatchingAll() {
  // Assert we are not running on WATCHDOG thread. Would be ideal to assert we
  // are on UI thread, but Unit tests are not running on UI thread.
  DCHECK(!WatchDogThread::CurrentlyOnWatchDogThread());
  if (!global_)
    return;

  // Remove all notifications for all watched threads.
  RemoveNotifications();

  // Delete all thread watcher objects on WatchDogThread.
  WatchDogThread::PostTask(
      FROM_HERE,
      NewRunnableMethod(global_, &ThreadWatcherList::DeleteAll));
}

// static
void ThreadWatcherList::RemoveNotifications() {
  // Assert we are not running on WATCHDOG thread. Would be ideal to assert we
  // are on UI thread, but Unit tests are not running on UI thread.
  DCHECK(!WatchDogThread::CurrentlyOnWatchDogThread());
  if (!global_)
    return;
  base::AutoLock auto_lock(global_->lock_);
  global_->registrar_.RemoveAll();
}

void ThreadWatcherList::DeleteAll() {
  DCHECK(WatchDogThread::CurrentlyOnWatchDogThread());
  base::AutoLock auto_lock(lock_);
  while (!registered_.empty()) {
    RegistrationList::iterator it = registered_.begin();
    delete it->second;
    registered_.erase(it->first);
  }
}

void ThreadWatcherList::Observe(NotificationType type,
                                const NotificationSource& source,
                                const NotificationDetails& details) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  // There is some user activity, see if thread watchers are to be awakened.
  bool need_to_awaken = false;
  base::TimeTicks now = base::TimeTicks::Now();
  {
    base::AutoLock lock(lock_);
    if (now - last_wakeup_time_ > base::TimeDelta::FromSeconds(2)) {
      need_to_awaken = true;
      last_wakeup_time_ = now;
    }
  }
  if (need_to_awaken) {
    WatchDogThread::PostTask(
        FROM_HERE,
        NewRunnableMethod(this, &ThreadWatcherList::WakeUpAll));
  }
}

void ThreadWatcherList::WakeUpAll() {
  DCHECK(WatchDogThread::CurrentlyOnWatchDogThread());
  if (!global_)
    return;
  base::AutoLock auto_lock(lock_);
  for (RegistrationList::iterator it = global_->registered_.begin();
       global_->registered_.end() != it;
       ++it)
    it->second->WakeUp();
}

// static
ThreadWatcher* ThreadWatcherList::Find(const BrowserThread::ID& thread_id) {
  if (!global_)
    return NULL;
  base::AutoLock auto_lock(global_->lock_);
  return global_->PreLockedFind(thread_id);
}

ThreadWatcher* ThreadWatcherList::PreLockedFind(
    const BrowserThread::ID& thread_id) {
  RegistrationList::iterator it = registered_.find(thread_id);
  if (registered_.end() == it)
    return NULL;
  return it->second;
}

// WatchDogThread methods and members.
//
// static
base::Lock WatchDogThread::lock_;
// static
WatchDogThread* WatchDogThread::watchdog_thread_ = NULL;

// The WatchDogThread object must outlive any tasks posted to the IO thread
// before the Quit task.
DISABLE_RUNNABLE_METHOD_REFCOUNT(WatchDogThread);

WatchDogThread::WatchDogThread() : Thread("WATCHDOG") {
}

WatchDogThread::~WatchDogThread() {
  // We cannot rely on our base class to stop the thread since we want our
  // CleanUp function to run.
  Stop();
}

// static
bool WatchDogThread::CurrentlyOnWatchDogThread() {
  base::AutoLock lock(lock_);
  return watchdog_thread_ &&
    watchdog_thread_->message_loop() == MessageLoop::current();
}

// static
bool WatchDogThread::PostTask(const tracked_objects::Location& from_here,
                              Task* task) {
  return PostTaskHelper(from_here, task, 0);
}

// static
bool WatchDogThread::PostDelayedTask(const tracked_objects::Location& from_here,
                                     Task* task,
                                     int64 delay_ms) {
  return PostTaskHelper(from_here, task, delay_ms);
}

// static
bool WatchDogThread::PostTaskHelper(
    const tracked_objects::Location& from_here,
    Task* task,
    int64 delay_ms) {
  {
    base::AutoLock lock(lock_);

    MessageLoop* message_loop = watchdog_thread_ ?
        watchdog_thread_->message_loop() : NULL;
    if (message_loop) {
      message_loop->PostDelayedTask(from_here, task, delay_ms);
      return true;
    }
  }
  delete task;

  return false;
}

void WatchDogThread::Init() {
  // This thread shouldn't be allowed to perform any blocking disk I/O.
  base::ThreadRestrictions::SetIOAllowed(false);

#if defined(OS_WIN)
  // Initializes the COM library on the current thread.
  HRESULT result = CoInitialize(NULL);
  CHECK(result == S_OK);
#endif

  base::AutoLock lock(lock_);
  CHECK(!watchdog_thread_);
  watchdog_thread_ = this;
}

void WatchDogThread::CleanUp() {
  base::AutoLock lock(lock_);
  watchdog_thread_ = NULL;
}

void WatchDogThread::CleanUpAfterMessageLoopDestruction() {
#if defined(OS_WIN)
  // Closes the COM library on the current thread. CoInitialize must
  // be balanced by a corresponding call to CoUninitialize.
  CoUninitialize();
#endif
}
