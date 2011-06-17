// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef JINGLE_GLUE_THREAD_WRAPPER_H_
#define JINGLE_GLUE_THREAD_WRAPPER_H_

#include <map>

#include "base/message_loop.h"
#include "base/synchronization/lock.h"
#include "third_party/libjingle/source/talk/base/thread.h"

class MessageLoop;

namespace jingle_glue {

// JingleThreadWrapper wraps Chromium threads using talk_base::Thread
// interface. The object must be created on a thread it belongs
// to. Each JingleThreadWrapper deletes itself when MessageLoop is
// destroyed. Currently only the bare minimum that is used by P2P
// part of libjingle is implemented.
class JingleThreadWrapper
    : public MessageLoop::DestructionObserver,
      public talk_base::Thread {
 public:
  // Create JingleThreadWrapper for the current thread if it hasn't
  // been created yet.
  static void EnsureForCurrentThread();

  JingleThreadWrapper(MessageLoop* message_loop);

  // MessageLoop::DestructionObserver implementation.
  virtual void WillDestroyCurrentMessageLoop() OVERRIDE;

  // talk_base::MessageQueue overrides.
  virtual void Post(talk_base::MessageHandler *phandler, uint32 id = 0,
                    talk_base::MessageData *pdata = NULL,
                    bool time_sensitive = false) OVERRIDE;
  virtual void PostDelayed(
      int delay_ms, talk_base::MessageHandler* handler, uint32 id = 0,
      talk_base::MessageData* data = NULL) OVERRIDE;
  virtual void Clear(talk_base::MessageHandler* handler,
                     uint32 id = talk_base::MQID_ANY,
                     talk_base::MessageList* removed = NULL) OVERRIDE;

  // Following methods are not supported.They are overriden just to
  // ensure that they are not called (each of them contain NOTREACHED
  // in the body). Some of this methods can be implemented if it
  // becomes neccessary to use libjingle code that calls them.
  virtual void Quit() OVERRIDE;
  virtual bool IsQuitting() OVERRIDE;
  virtual void Restart() OVERRIDE;
  virtual bool Get(talk_base::Message* msg, int delay_ms = talk_base::kForever,
                   bool process_io = true) OVERRIDE;
  virtual bool Peek(talk_base::Message* msg, int delay_ms = 0) OVERRIDE;
  virtual void PostAt(
      uint32 timestamp, talk_base::MessageHandler* handler,
      uint32 id = 0, talk_base::MessageData* data = NULL) OVERRIDE;
  virtual void Dispatch(talk_base::Message* msg) OVERRIDE;
  virtual void ReceiveSends() OVERRIDE;
  virtual int GetDelay() OVERRIDE;

  // talk_base::Thread overrides.
  virtual void Stop() OVERRIDE;
  virtual void Run() OVERRIDE;

 private:
  typedef std::map<int, talk_base::Message> MessagesQueue;

  virtual ~JingleThreadWrapper();

  void PostTaskInternal(
      int delay_ms, talk_base::MessageHandler* handler,
      uint32 message_id, talk_base::MessageData* data);
  void RunTask(int task_id);

  // Chromium thread used to execute messages posted on this thread.
  MessageLoop* message_loop_;

  // |lock_| must be locked when accessing |messages_|.
  base::Lock lock_;
  int last_task_id_;
  MessagesQueue messages_;
};

}

// Safe to disable refcounting because JingleThreadWrapper deletes
// itself with the thread.
DISABLE_RUNNABLE_METHOD_REFCOUNT(jingle_glue::JingleThreadWrapper);

#endif  // JINGLE_GLUE_THREAD_WRAPPER_H_
