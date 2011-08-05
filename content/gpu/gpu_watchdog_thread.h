// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_GPU_GPU_WATCHDOG_THREAD_H_
#define CONTENT_GPU_GPU_WATCHDOG_THREAD_H_

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "base/task.h"
#include "base/threading/thread.h"
#include "base/time.h"
#include "content/common/gpu/gpu_watchdog.h"

// A thread that intermitently sends tasks to a group of watched message loops
// and deliberately crashes if one of them does not respond after a timeout.
class GpuWatchdogThread : public base::Thread,
                          public GpuWatchdog,
                          public base::RefCountedThreadSafe<GpuWatchdogThread> {
 public:
  explicit GpuWatchdogThread(int timeout);
  virtual ~GpuWatchdogThread();

  // Accessible on watched thread but only modified by watchdog thread.
  bool armed() const { return armed_; }
  void PostAcknowledge();

  // Implement GpuWatchdog.
  virtual void CheckArmed();

 protected:
  virtual void Init();
  virtual void CleanUp();

 private:

  // An object of this type intercepts the reception and completion of all tasks
  // on the watched thread and checks whether the watchdog is armed.
  class GpuWatchdogTaskObserver : public MessageLoop::TaskObserver {
   public:
    explicit GpuWatchdogTaskObserver(GpuWatchdogThread* watchdog);
    virtual ~GpuWatchdogTaskObserver();

    // Implements MessageLoop::TaskObserver.
    virtual void WillProcessTask(base::TimeTicks time_posted) OVERRIDE;
    virtual void DidProcessTask(base::TimeTicks time_posted) OVERRIDE;

   private:
    GpuWatchdogThread* watchdog_;
  };

  void OnAcknowledge();
  void OnCheck();
  void DeliberatelyCrashingToRecoverFromHang();
  void Disable();

  int64 GetWatchedThreadTime();

  MessageLoop* watched_message_loop_;
  int timeout_;
  volatile bool armed_;
  GpuWatchdogTaskObserver task_observer_;

#if defined(OS_WIN)
  void* watched_thread_handle_;
  int64 arm_cpu_time_;
#endif

  base::Time arm_absolute_time_;

  typedef ScopedRunnableMethodFactory<GpuWatchdogThread> MethodFactory;
  scoped_ptr<MethodFactory> method_factory_;

  DISALLOW_COPY_AND_ASSIGN(GpuWatchdogThread);
};

#endif  // CONTENT_GPU_GPU_WATCHDOG_THREAD_H_
