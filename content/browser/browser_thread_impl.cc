// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/browser_thread_impl.h"

#include "base/atomicops.h"
#include "base/bind.h"
#include "base/lazy_instance.h"
#include "base/message_loop.h"
#include "base/message_loop_proxy.h"
#include "base/threading/sequenced_worker_pool.h"
#include "base/threading/thread_restrictions.h"

namespace content {

namespace {

// Friendly names for the well-known threads.
static const char* g_browser_thread_names[BrowserThread::ID_COUNT] = {
  "",  // UI (name assembled in browser_main.cc).
  "Chrome_DBThread",  // DB
  "Chrome_WebKitThread",  // WEBKIT_DEPRECATED
  "Chrome_FileThread",  // FILE
  "Chrome_FileUserBlockingThread",  // FILE_USER_BLOCKING
  "Chrome_ProcessLauncherThread",  // PROCESS_LAUNCHER
  "Chrome_CacheThread",  // CACHE
  "Chrome_IOThread",  // IO
};

struct BrowserThreadGlobals {
  BrowserThreadGlobals()
      : blocking_pool(new base::SequencedWorkerPool(3, "BrowserBlocking")) {
    memset(threads, 0,
           BrowserThread::ID_COUNT * sizeof(BrowserThreadImpl*));
    memset(thread_delegates, 0,
           BrowserThread::ID_COUNT * sizeof(BrowserThreadDelegate*));
  }

  // This lock protects |threads|. Do not read or modify that array
  // without holding this lock. Do not block while holding this lock.
  base::Lock lock;

  // This array is protected by |lock|. The threads are not owned by this
  // array. Typically, the threads are owned on the UI thread by
  // content::BrowserMainLoop. BrowserThreadImpl objects remove themselves from
  // this array upon destruction.
  BrowserThreadImpl* threads[BrowserThread::ID_COUNT];

  // Only atomic operations are used on this array. The delegates are not owned
  // by this array, rather by whoever calls BrowserThread::SetDelegate.
  BrowserThreadDelegate* thread_delegates[BrowserThread::ID_COUNT];

  // This pointer is deliberately leaked on shutdown. This allows the pool to
  // implement "continue on shutdown" semantics.
  base::SequencedWorkerPool* blocking_pool;
};

base::LazyInstance<BrowserThreadGlobals>::Leaky
    g_globals = LAZY_INSTANCE_INITIALIZER;

}  // namespace

BrowserThreadImpl::BrowserThreadImpl(ID identifier)
    : Thread(g_browser_thread_names[identifier]),
      identifier_(identifier) {
  Initialize();
}

BrowserThreadImpl::BrowserThreadImpl(ID identifier,
                                     MessageLoop* message_loop)
    : Thread(message_loop->thread_name().c_str()),
      identifier_(identifier) {
  set_message_loop(message_loop);
  Initialize();
}

// static
void BrowserThreadImpl::ShutdownThreadPool() {
  BrowserThreadGlobals& globals = g_globals.Get();
  globals.blocking_pool->Shutdown();
}

void BrowserThreadImpl::Init() {
  BrowserThreadGlobals& globals = g_globals.Get();

  using base::subtle::AtomicWord;
  AtomicWord* storage =
      reinterpret_cast<AtomicWord*>(&globals.thread_delegates[identifier_]);
  AtomicWord stored_pointer = base::subtle::NoBarrier_Load(storage);
  BrowserThreadDelegate* delegate =
      reinterpret_cast<BrowserThreadDelegate*>(stored_pointer);
  if (delegate)
    delegate->Init();
}

void BrowserThreadImpl::CleanUp() {
  BrowserThreadGlobals& globals = g_globals.Get();

  using base::subtle::AtomicWord;
  AtomicWord* storage =
      reinterpret_cast<AtomicWord*>(&globals.thread_delegates[identifier_]);
  AtomicWord stored_pointer = base::subtle::NoBarrier_Load(storage);
  BrowserThreadDelegate* delegate =
      reinterpret_cast<BrowserThreadDelegate*>(stored_pointer);

  if (delegate)
    delegate->CleanUp();
}

void BrowserThreadImpl::Initialize() {
  BrowserThreadGlobals& globals = g_globals.Get();

  base::AutoLock lock(globals.lock);
  DCHECK(identifier_ >= 0 && identifier_ < ID_COUNT);
  DCHECK(globals.threads[identifier_] == NULL);
  globals.threads[identifier_] = this;
}

BrowserThreadImpl::~BrowserThreadImpl() {
  // All Thread subclasses must call Stop() in the destructor. This is
  // doubly important here as various bits of code check they are on
  // the right BrowserThread.
  Stop();

  BrowserThreadGlobals& globals = g_globals.Get();
  base::AutoLock lock(globals.lock);
  globals.threads[identifier_] = NULL;
#ifndef NDEBUG
  // Double check that the threads are ordered correctly in the enumeration.
  for (int i = identifier_ + 1; i < ID_COUNT; ++i) {
    DCHECK(!globals.threads[i]) <<
        "Threads must be listed in the reverse order that they die";
  }
#endif
}

// static
bool BrowserThreadImpl::PostTaskHelper(
    BrowserThread::ID identifier,
    const tracked_objects::Location& from_here,
    const base::Closure& task,
    int64 delay_ms,
    bool nestable) {
  DCHECK(identifier >= 0 && identifier < ID_COUNT);
  // Optimization: to avoid unnecessary locks, we listed the ID enumeration in
  // order of lifetime.  So no need to lock if we know that the other thread
  // outlives this one.
  // Note: since the array is so small, ok to loop instead of creating a map,
  // which would require a lock because std::map isn't thread safe, defeating
  // the whole purpose of this optimization.
  BrowserThread::ID current_thread;
  bool guaranteed_to_outlive_target_thread =
      GetCurrentThreadIdentifier(&current_thread) &&
      current_thread <= identifier;

  BrowserThreadGlobals& globals = g_globals.Get();
  if (!guaranteed_to_outlive_target_thread)
    globals.lock.Acquire();

  MessageLoop* message_loop = globals.threads[identifier] ?
      globals.threads[identifier]->message_loop() : NULL;
  if (message_loop) {
    base::TimeDelta delay = base::TimeDelta::FromMilliseconds(delay_ms);
    if (nestable) {
      message_loop->PostDelayedTask(from_here, task, delay);
    } else {
      message_loop->PostNonNestableDelayedTask(from_here, task, delay);
    }
  }

  if (!guaranteed_to_outlive_target_thread)
    globals.lock.Release();

  return !!message_loop;
}

// An implementation of MessageLoopProxy to be used in conjunction
// with BrowserThread.
class BrowserThreadMessageLoopProxy : public base::MessageLoopProxy {
 public:
  explicit BrowserThreadMessageLoopProxy(BrowserThread::ID identifier)
      : id_(identifier) {
  }

  // MessageLoopProxy implementation.
  virtual bool PostTask(const tracked_objects::Location& from_here,
                        const base::Closure& task) {
    return BrowserThread::PostTask(id_, from_here, task);
  }

  virtual bool PostDelayedTask(const tracked_objects::Location& from_here,
                               const base::Closure& task, int64 delay_ms) {
    return BrowserThread::PostDelayedTask(id_, from_here, task, delay_ms);
  }

  virtual bool PostNonNestableTask(const tracked_objects::Location& from_here,
                                   const base::Closure& task) {
    return BrowserThread::PostNonNestableTask(id_, from_here, task);
  }

  virtual bool PostNonNestableDelayedTask(
      const tracked_objects::Location& from_here,
      const base::Closure& task,
      int64 delay_ms) {
    return BrowserThread::PostNonNestableDelayedTask(id_, from_here, task,
                                                     delay_ms);
  }

  virtual bool BelongsToCurrentThread() {
    return BrowserThread::CurrentlyOn(id_);
  }

 private:
  BrowserThread::ID id_;
  DISALLOW_COPY_AND_ASSIGN(BrowserThreadMessageLoopProxy);
};

// static
bool BrowserThread::PostBlockingPoolTask(
    const tracked_objects::Location& from_here,
    const base::Closure& task) {
  return g_globals.Get().blocking_pool->PostWorkerTask(from_here, task);
}

// static
bool BrowserThread::PostBlockingPoolSequencedTask(
    const std::string& sequence_token_name,
    const tracked_objects::Location& from_here,
    const base::Closure& task) {
  return g_globals.Get().blocking_pool->PostNamedSequencedWorkerTask(
      sequence_token_name, from_here, task);
}

// static
base::SequencedWorkerPool* BrowserThread::GetBlockingPool() {
  return g_globals.Get().blocking_pool;
}

// static
bool BrowserThread::IsWellKnownThread(ID identifier) {
  BrowserThreadGlobals& globals = g_globals.Get();
  base::AutoLock lock(globals.lock);
  return (identifier >= 0 && identifier < ID_COUNT &&
          globals.threads[identifier]);
}

// static
bool BrowserThread::CurrentlyOn(ID identifier) {
  // We shouldn't use MessageLoop::current() since it uses LazyInstance which
  // may be deleted by ~AtExitManager when a WorkerPool thread calls this
  // function.
  // http://crbug.com/63678
  base::ThreadRestrictions::ScopedAllowSingleton allow_singleton;
  BrowserThreadGlobals& globals = g_globals.Get();
  base::AutoLock lock(globals.lock);
  DCHECK(identifier >= 0 && identifier < ID_COUNT);
  return globals.threads[identifier] &&
         globals.threads[identifier]->message_loop() ==
             MessageLoop::current();
}

// static
bool BrowserThread::IsMessageLoopValid(ID identifier) {
  BrowserThreadGlobals& globals = g_globals.Get();
  base::AutoLock lock(globals.lock);
  DCHECK(identifier >= 0 && identifier < ID_COUNT);
  return globals.threads[identifier] &&
         globals.threads[identifier]->message_loop();
}

// static
bool BrowserThread::PostTask(ID identifier,
                             const tracked_objects::Location& from_here,
                             const base::Closure& task) {
  return BrowserThreadImpl::PostTaskHelper(
      identifier, from_here, task, 0, true);
}

// static
bool BrowserThread::PostDelayedTask(ID identifier,
                                    const tracked_objects::Location& from_here,
                                    const base::Closure& task,
                                    int64 delay_ms) {
  return BrowserThreadImpl::PostTaskHelper(
      identifier, from_here, task, delay_ms, true);
}

// static
bool BrowserThread::PostNonNestableTask(
    ID identifier,
    const tracked_objects::Location& from_here,
    const base::Closure& task) {
  return BrowserThreadImpl::PostTaskHelper(
      identifier, from_here, task, 0, false);
}

// static
bool BrowserThread::PostNonNestableDelayedTask(
    ID identifier,
    const tracked_objects::Location& from_here,
    const base::Closure& task,
    int64 delay_ms) {
  return BrowserThreadImpl::PostTaskHelper(
      identifier, from_here, task, delay_ms, false);
}

// static
bool BrowserThread::PostTaskAndReply(
    ID identifier,
    const tracked_objects::Location& from_here,
    const base::Closure& task,
    const base::Closure& reply) {
  return GetMessageLoopProxyForThread(identifier)->PostTaskAndReply(from_here,
                                                                    task,
                                                                    reply);
}

// static
bool BrowserThread::GetCurrentThreadIdentifier(ID* identifier) {
  // We shouldn't use MessageLoop::current() since it uses LazyInstance which
  // may be deleted by ~AtExitManager when a WorkerPool thread calls this
  // function.
  // http://crbug.com/63678
  base::ThreadRestrictions::ScopedAllowSingleton allow_singleton;
  MessageLoop* cur_message_loop = MessageLoop::current();
  BrowserThreadGlobals& globals = g_globals.Get();
  for (int i = 0; i < ID_COUNT; ++i) {
    if (globals.threads[i] &&
        globals.threads[i]->message_loop() == cur_message_loop) {
      *identifier = globals.threads[i]->identifier_;
      return true;
    }
  }

  return false;
}

// static
scoped_refptr<base::MessageLoopProxy>
BrowserThread::GetMessageLoopProxyForThread(ID identifier) {
  scoped_refptr<base::MessageLoopProxy> proxy(
      new BrowserThreadMessageLoopProxy(identifier));
  return proxy;
}

// static
MessageLoop* BrowserThread::UnsafeGetMessageLoopForThread(ID identifier) {
  BrowserThreadGlobals& globals = g_globals.Get();
  base::AutoLock lock(globals.lock);
  base::Thread* thread = globals.threads[identifier];
  DCHECK(thread);
  MessageLoop* loop = thread->message_loop();
  return loop;
}

// static
void BrowserThread::SetDelegate(ID identifier,
                                BrowserThreadDelegate* delegate) {
  using base::subtle::AtomicWord;
  BrowserThreadGlobals& globals = g_globals.Get();
  AtomicWord* storage = reinterpret_cast<AtomicWord*>(
      &globals.thread_delegates[identifier]);
  AtomicWord old_pointer = base::subtle::NoBarrier_AtomicExchange(
      storage, reinterpret_cast<AtomicWord>(delegate));

  // This catches registration when previously registered.
  DCHECK(!delegate || !old_pointer);
}

}  // namespace content
