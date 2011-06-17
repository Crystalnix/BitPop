// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file defines a WatchDog thread that monitors the responsiveness of other
// browser threads like UI, IO, DB, FILE and CACHED threads. It also defines
// ThreadWatcher class which performs health check on threads that would like to
// be watched. This file also defines ThreadWatcherList class that has list of
// all active ThreadWatcher objects.
//
// ThreadWatcher class sends ping message to the watched thread and the watched
// thread responds back with a pong message. It uploads response time
// (difference between ping and pong times) as a histogram.
//
// TODO(raman): ThreadWatcher can detect hung threads. If a hung thread is
// detected, we should probably just crash, and allow the crash system to gather
// then stack trace.
//
// Example Usage:
//
//   The following is an example for watching responsiveness of IO thread.
//   sleep_time specifies how often ping messages have to be sent to IO thread.
//   unresponsive_time is the wait time after ping message is sent, to check if
//   we have received pong message or not.
//
//   base::TimeDelta sleep_time = base::TimeDelta::FromSeconds(5);
//   base::TimeDelta unresponsive_time = base::TimeDelta::FromSeconds(10);
//   ThreadWatcher::StartWatching(BrowserThread::IO, "IO", sleep_time,
//                                unresponsive_time);

#ifndef CHROME_BROWSER_METRICS_THREAD_WATCHER_H_
#define CHROME_BROWSER_METRICS_THREAD_WATCHER_H_

#include <map>
#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/gtest_prod_util.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "base/metrics/histogram.h"
#include "base/synchronization/lock.h"
#include "base/task.h"
#include "base/threading/thread.h"
#include "base/time.h"
#include "content/browser/browser_thread.h"
#include "content/common/notification_observer.h"
#include "content/common/notification_registrar.h"

class CustomThreadWatcher;
class ThreadWatcherList;

// This class performs health check on threads that would like to be watched.
class ThreadWatcher {
 public:
  // This method starts performing health check on the given thread_id. It will
  // create ThreadWatcher object for the given thread_id, thread_name,
  // sleep_time and unresponsive_time. sleep_time_ is the wait time between ping
  // messages. unresponsive_time_ is the wait time after ping message is sent,
  // to check if we have received pong message or not. It will register that
  // ThreadWatcher object and activate the thread watching of the given
  // thread_id.
  static void StartWatching(const BrowserThread::ID& thread_id,
                            const std::string& thread_name,
                            const base::TimeDelta& sleep_time,
                            const base::TimeDelta& unresponsive_time);

  // Return the thread_id of the thread being watched.
  BrowserThread::ID thread_id() const { return thread_id_; }

  // Return the name of the thread being watched.
  std::string thread_name() const { return thread_name_; }

  // Return the sleep time between ping messages to be sent to the thread.
  base::TimeDelta sleep_time() const { return sleep_time_; }

  // Return the the wait time to check the responsiveness of the thread.
  base::TimeDelta unresponsive_time() const { return unresponsive_time_; }

  // Returns true if we are montioring the thread.
  bool active() const { return active_; }

  // Returns ping_time_ (used by unit tests).
  base::TimeTicks ping_time() const { return ping_time_; }

  // Returns ping_sequence_number_ (used by unit tests).
  uint64 ping_sequence_number() const { return ping_sequence_number_; }

 protected:
  // Construct a ThreadWatcher for the given thread_id. sleep_time_ is the
  // wait time between ping messages. unresponsive_time_ is the wait time after
  // ping message is sent, to check if we have received pong message or not.
  ThreadWatcher(const BrowserThread::ID& thread_id,
                const std::string& thread_name,
                const base::TimeDelta& sleep_time,
                const base::TimeDelta& unresponsive_time);
  virtual ~ThreadWatcher();

  // This method activates the thread watching which starts ping/pong messaging.
  virtual void ActivateThreadWatching();

  // This method de-activates the thread watching and revokes all tasks.
  virtual void DeActivateThreadWatching();

  // This will ensure that the watching is actively taking place, and awaken
  // (i.e., post a PostPingMessage) if the watcher has stopped pinging due to
  // lack of user activity. It will also reset ping_count_ to kPingCount.
  virtual void WakeUp();

  // This method records when ping message was sent and it will Post a task
  // (OnPingMessage) to the watched thread that does nothing but respond with
  // OnPongMessage. It also posts a task (OnCheckResponsiveness) to check
  // responsiveness of monitored thread that would be called after waiting
  // unresponsive_time_.
  // This method is accessible on WatchDogThread.
  virtual void PostPingMessage();

  // This method handles a Pong Message from watched thread. It will track the
  // response time (pong time minus ping time) via histograms. It posts a
  // PostPingMessage task that would be called after waiting sleep_time_.  It
  // increments ping_sequence_number_ by 1.
  // This method is accessible on WatchDogThread.
  virtual void OnPongMessage(uint64 ping_sequence_number);

  // This method will determine if the watched thread is responsive or not. If
  // the latest ping_sequence_number_ is not same as the ping_sequence_number
  // that is passed in, then we can assume that watched thread has responded
  // with a pong message.
  // This method is accessible on WatchDogThread.
  virtual bool OnCheckResponsiveness(uint64 ping_sequence_number);

 private:
  friend class ThreadWatcherList;

  // Allow tests to access our innards for testing purposes.
  FRIEND_TEST_ALL_PREFIXES(ThreadWatcherTest, Registration);
  FRIEND_TEST_ALL_PREFIXES(ThreadWatcherTest, ThreadResponding);
  FRIEND_TEST_ALL_PREFIXES(ThreadWatcherTest, ThreadNotResponding);
  FRIEND_TEST_ALL_PREFIXES(ThreadWatcherTest, MultipleThreadsResponding);
  FRIEND_TEST_ALL_PREFIXES(ThreadWatcherTest, MultipleThreadsNotResponding);

  // Post constructor initialization.
  void Initialize();

  // Watched thread does nothing except post callback_task to the WATCHDOG
  // Thread. This method is called on watched thread.
  static void OnPingMessage(const BrowserThread::ID& thread_id,
                            Task* callback_task);

  // This is the number of ping messages to be sent when the user is idle.
  // ping_count_ will be initialized to kPingCount whenever user becomes active.
  static const int kPingCount;

  // The thread_id of the thread being watched. Only one instance can exist for
  // the given thread_id of the thread being watched.
  const BrowserThread::ID thread_id_;

  // The name of the thread being watched.
  const std::string thread_name_;

  // It is the sleep time between between the receipt of a pong message back,
  // and the sending of another ping message.
  const base::TimeDelta sleep_time_;

  // It is the duration from sending a ping message, until we check status to be
  // sure a pong message has been returned.
  const base::TimeDelta unresponsive_time_;

  // This is the last time when ping message was sent.
  base::TimeTicks ping_time_;

  // This is the sequence number of the next ping for which there is no pong. If
  // the instance is sleeping, then it will be the sequence number for the next
  // ping.
  uint64 ping_sequence_number_;

  // This is set to true if thread watcher is watching.
  bool active_;

  // The counter tracks least number of ping messages that will be sent to
  // watched thread before the ping-pong mechanism will go into an extended
  // sleep. If this value is zero, then the mechanism is in an extended sleep,
  // and awaiting some observed user action before continuing.
  int ping_count_;

  // Histogram that keeps track of response times for the watched thread.
  base::Histogram* histogram_;

  // We use this factory to create callback tasks for ThreadWatcher object. We
  // use this during ping-pong messaging between WatchDog thread and watched
  // thread.
  ScopedRunnableMethodFactory<ThreadWatcher> method_factory_;

  DISALLOW_COPY_AND_ASSIGN(ThreadWatcher);
};

// Class with a list of all active thread watchers.  A thread watcher is active
// if it has been registered, which includes determing the histogram name. This
// class provides utility functions to start and stop watching all browser
// threads. Only one instance of this class exists.
class ThreadWatcherList : public NotificationObserver {
 public:
  // A map from BrowserThread to the actual instances.
  typedef std::map<BrowserThread::ID, ThreadWatcher*> RegistrationList;

  // This singleton holds the global list of registered ThreadWatchers.
  ThreadWatcherList();
  // Destructor deletes all registered ThreadWatcher instances.
  virtual ~ThreadWatcherList();

  // Register() stores a pointer to the given ThreadWatcher in a global map.
  static void Register(ThreadWatcher* watcher);

  // This method returns true if the ThreadWatcher object is registerd.
  static bool IsRegistered(const BrowserThread::ID thread_id);

  // This method posts a task on WatchDogThread to start watching all browser
  // threads.
  // This method is accessible on UI thread.
  static void StartWatchingAll();

  // This method posts a task on WatchDogThread to RevokeAll tasks and to
  // deactive thread watching of other threads and tell NotificationService to
  // stop calling Observe.
  // This method is accessible on UI thread.
  static void StopWatchingAll();

  // RemoveAll NotificationTypes that are being observed.
  // This method is accessible on UI thread.
  static void RemoveNotifications();

 private:
  // Allow tests to access our innards for testing purposes.
  FRIEND_TEST_ALL_PREFIXES(ThreadWatcherTest, Registration);

  // Delete all thread watcher objects and remove them from global map.
  // This method is accessible on WatchDogThread.
  void DeleteAll();

  // This will ensure that the watching is actively taking place. It will wakeup
  // all thread watchers every 2 seconds. This is the implementation of
  // NotificationObserver. When a matching notification is posted to the
  // notification service, this method is called.
  // This method is accessible on UI thread.
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

  // This will ensure that the watching is actively taking place, and awaken
  // all thread watchers that are registered.
  // This method is accessible on WatchDogThread.
  virtual void WakeUpAll();

  // The Find() method can be used to test to see if a given ThreadWatcher was
  // already registered, or to retrieve a pointer to it from the global map.
  static ThreadWatcher* Find(const BrowserThread::ID& thread_id);

  // Helper function should be called only while holding lock_.
  ThreadWatcher* PreLockedFind(const BrowserThread::ID& thread_id);

  static ThreadWatcherList* global_;  // The singleton of this class.

  // Lock for access to registered_.
  base::Lock lock_;

  // Map of all registered watched threads, from thread_id to ThreadWatcher.
  RegistrationList registered_;

  // The registrar that holds NotificationTypes to be observed.
  NotificationRegistrar registrar_;

  // This is the last time when woke all thread watchers up.
  base::TimeTicks last_wakeup_time_;

  DISALLOW_COPY_AND_ASSIGN(ThreadWatcherList);
};

// Class for WatchDogThread and in its Init method, we start watching UI, IO,
// DB, FILE, CACHED threads.
class WatchDogThread : public base::Thread {
 public:
  // Constructor.
  WatchDogThread();

  // Destroys the thread and stops the thread.
  virtual ~WatchDogThread();

  // Callable on any thread.  Returns whether you're currently on a
  // watchdog_thread_.
  static bool CurrentlyOnWatchDogThread();

  // These are the same methods in message_loop.h, but are guaranteed to either
  // get posted to the MessageLoop if it's still alive, or be deleted otherwise.
  // They return true iff the watchdog thread existed and the task was posted.
  // Note that even if the task is posted, there's no guarantee that it will
  // run, since the target thread may already have a Quit message in its queue.
  static bool PostTask(const tracked_objects::Location& from_here, Task* task);
  static bool PostDelayedTask(const tracked_objects::Location& from_here,
                              Task* task,
                              int64 delay_ms);

 protected:
  virtual void Init();
  virtual void CleanUp();
  virtual void CleanUpAfterMessageLoopDestruction();

 private:
  static bool PostTaskHelper(
      const tracked_objects::Location& from_here,
      Task* task,
      int64 delay_ms);

  // This lock protects watchdog_thread_.
  static base::Lock lock_;

  static WatchDogThread* watchdog_thread_;  // The singleton of this class.

  DISALLOW_COPY_AND_ASSIGN(WatchDogThread);
};

// DISABLE_RUNNABLE_METHOD_REFCOUNT is a convenience macro for disabling
// refcounting of ThreadWatcher and ThreadWatcherList classes.
DISABLE_RUNNABLE_METHOD_REFCOUNT(ThreadWatcher);
DISABLE_RUNNABLE_METHOD_REFCOUNT(ThreadWatcherList);

#endif  // CHROME_BROWSER_METRICS_THREAD_WATCHER_H_
