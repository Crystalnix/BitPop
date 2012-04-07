// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_UTILITY_PROCESS_HOST_H_
#define CONTENT_BROWSER_UTILITY_PROCESS_HOST_H_
#pragma once

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/memory/ref_counted.h"
#include "base/process_util.h"
#include "base/memory/weak_ptr.h"
#include "content/common/content_export.h"
#include "content/public/browser/browser_child_process_host_delegate.h"
#include "content/public/browser/browser_thread.h"
#include "ipc/ipc_message.h"

class BrowserChildProcessHostImpl;

// This class acts as the browser-side host to a utility child process.  A
// utility process is a short-lived sandboxed process that is created to run
// a specific task.  This class lives solely on the IO thread.
// If you need a single method call in the sandbox, use StartFooBar(p).
// If you need multiple batches of work to be done in the sandboxed process,
// use StartBatchMode(), then multiple calls to StartFooBar(p),
// then finish with EndBatchMode().
//
// Note: If your class keeps a ptr to an object of this type, grab a weak ptr to
// avoid a use after free.  See http://crbug.com/108871.
class CONTENT_EXPORT UtilityProcessHost
    : public content::BrowserChildProcessHostDelegate,
      public IPC::Message::Sender,
      public base::SupportsWeakPtr<UtilityProcessHost> {
 public:
  // An interface to be implemented by consumers of the utility process to
  // get results back.  All functions are called on the thread passed along
  // to UtilityProcessHost.
  class CONTENT_EXPORT Client : public base::RefCountedThreadSafe<Client> {
   public:
    Client();

    // Called when the process has crashed.
    virtual void OnProcessCrashed(int exit_code);

    // Allow the client to filter IPC messages.
    virtual bool OnMessageReceived(const IPC::Message& message);

   protected:
    friend class base::RefCountedThreadSafe<Client>;

    virtual ~Client();

   private:
    friend class UtilityProcessHost;

    DISALLOW_COPY_AND_ASSIGN(Client);
  };

  UtilityProcessHost(Client* client,
                     content::BrowserThread::ID client_thread_id);
  virtual ~UtilityProcessHost();

  // IPC::Message::Sender implementation:
  virtual bool Send(IPC::Message* message) OVERRIDE;

  // Starts utility process in batch mode. Caller must call EndBatchMode()
  // to finish the utility process.
  bool StartBatchMode();

  // Ends the utility process. Must be called after StartBatchMode().
  void EndBatchMode();

  void set_exposed_dir(const FilePath& dir) { exposed_dir_ = dir; }
  void set_no_sandbox(bool flag) { no_sandbox_ = flag; }
  void set_child_flags(int flags) { child_flags_ = flags; }
  void set_use_linux_zygote(bool flag) { use_linux_zygote_ = flag; }
#if defined(OS_POSIX)
  void set_env(const base::environment_vector& env) { env_ = env; }
#endif

 protected:
  // Allow these methods to be overridden for tests.
  virtual FilePath GetUtilityProcessCmd();

 private:
  // Starts a process if necessary.  Returns true if it succeeded or a process
  // has already been started via StartBatchMode().
  bool StartProcess();

  // BrowserChildProcessHost:
  virtual bool OnMessageReceived(const IPC::Message& message) OVERRIDE;
  virtual void OnProcessCrashed(int exit_code) OVERRIDE;

  // A pointer to our client interface, who will be informed of progress.
  scoped_refptr<Client> client_;
  content::BrowserThread::ID client_thread_id_;
  // True when running in batch mode, i.e., StartBatchMode() has been called
  // and the utility process will run until EndBatchMode().
  bool is_batch_mode_;

  // Allows a directory to be opened through the sandbox, in case it's needed by
  // the operation.
  FilePath exposed_dir_;

  // Whether to pass switches::kNoSandbox to the child.
  bool no_sandbox_;

  // Flags defined in ChildProcessHost with which to start the process.
  int child_flags_;

  // If the |no_sandbox_| flag is off, and we are on Linux, launch the
  // utility process from the zygote. Defaults to false.
  // Can only be used for tasks that do not require FS access.
  bool use_linux_zygote_;

  base::environment_vector env_;

  bool started_;

  scoped_ptr<BrowserChildProcessHostImpl> process_;

  DISALLOW_COPY_AND_ASSIGN(UtilityProcessHost);
};

#endif  // CONTENT_BROWSER_UTILITY_PROCESS_HOST_H_
