// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_TEST_BASE_THREAD_OBSERVER_HELPER_H_
#define CHROME_TEST_BASE_THREAD_OBSERVER_HELPER_H_

#include "base/bind.h"
#include "base/memory/ref_counted.h"
#include "base/sequenced_task_runner_helpers.h"
#include "base/synchronization/waitable_event.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/test/mock_notification_observer.h"

// Helper class to add and remove observers on a non-UI thread from
// the UI thread.
template <class T, typename Traits>
class ThreadObserverHelper : public base::RefCountedThreadSafe<T, Traits> {
 public:
  explicit ThreadObserverHelper(content::BrowserThread::ID id)
      : id_(id), done_event_(false, false) {}

  void Init() {
    using content::BrowserThread;
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    BrowserThread::PostTask(
        id_,
        FROM_HERE,
        base::Bind(&ThreadObserverHelper::RegisterObserversTask, this));
    done_event_.Wait();
  }

  virtual ~ThreadObserverHelper() {
    DCHECK(content::BrowserThread::CurrentlyOn(id_));
    registrar_.RemoveAll();
  }

  content::MockNotificationObserver* observer() {
    return &observer_;
  }

 protected:
  friend class base::RefCountedThreadSafe<T>;

  virtual void RegisterObservers() = 0;

  content::NotificationRegistrar registrar_;
  content::MockNotificationObserver observer_;

 private:
  void RegisterObserversTask() {
    DCHECK(content::BrowserThread::CurrentlyOn(id_));
    RegisterObservers();
    done_event_.Signal();
  }

  content::BrowserThread::ID id_;
  base::WaitableEvent done_event_;
};

class DBThreadObserverHelper;
typedef ThreadObserverHelper<
    DBThreadObserverHelper,
    content::BrowserThread::DeleteOnDBThread> DBThreadObserverHelperBase;

class DBThreadObserverHelper : public DBThreadObserverHelperBase {
 public:
  DBThreadObserverHelper() :
      DBThreadObserverHelperBase(content::BrowserThread::DB) {}

 protected:
  friend struct content::BrowserThread::DeleteOnThread<
      content::BrowserThread::DB>;
  friend class base::DeleteHelper<DBThreadObserverHelper>;

  virtual ~DBThreadObserverHelper() {}
};

#endif  // CHROME_TEST_BASE_THREAD_OBSERVER_HELPER_H_
