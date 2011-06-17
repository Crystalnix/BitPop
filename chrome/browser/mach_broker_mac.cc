// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/mach_broker_mac.h"

#include "base/command_line.h"
#include "base/logging.h"
#include "base/mach_ipc_mac.h"
#include "base/string_util.h"
#include "base/sys_string_conversions.h"
#include "base/threading/platform_thread.h"
#include "chrome/browser/extensions/extension_host.h"
#include "chrome/common/chrome_switches.h"
#include "content/browser/browser_thread.h"
#include "content/browser/renderer_host/render_process_host.h"
#include "content/common/child_process_info.h"
#include "content/common/notification_service.h"

namespace {
// Prints a string representation of a Mach error code.
std::string MachErrorCode(kern_return_t err) {
  return StringPrintf("0x%x %s", err, mach_error_string(err));
}
}  // namespace

// Required because notifications happen on the UI thread.
class RegisterNotificationTask : public Task {
 public:
  RegisterNotificationTask(
      MachBroker* broker)
      : broker_(broker) { }

  virtual void Run() {
    broker_->registrar_.Add(broker_,
        NotificationType::RENDERER_PROCESS_CLOSED,
        NotificationService::AllSources());
    broker_->registrar_.Add(broker_,
        NotificationType::RENDERER_PROCESS_TERMINATED,
        NotificationService::AllSources());
    broker_->registrar_.Add(broker_,
        NotificationType::CHILD_PROCESS_CRASHED,
        NotificationService::AllSources());
    broker_->registrar_.Add(broker_,
        NotificationType::CHILD_PROCESS_HOST_DISCONNECTED,
        NotificationService::AllSources());
    broker_->registrar_.Add(broker_,
        NotificationType::EXTENSION_PROCESS_TERMINATED,
        NotificationService::AllSources());
  }

 private:
  MachBroker* broker_;
  DISALLOW_COPY_AND_ASSIGN(RegisterNotificationTask);
};

class MachListenerThreadDelegate : public base::PlatformThread::Delegate {
 public:
  MachListenerThreadDelegate(MachBroker* broker) : broker_(broker) {
    DCHECK(broker_);
    std::string port_name = MachBroker::GetMachPortName();

    // Create the receive port in the constructor, not in ThreadMain().  It is
    // important to create and register the receive port before starting the
    // thread so that child processes will always have someone who's listening.
    receive_port_.reset(new base::ReceivePort(port_name.c_str()));
  }

  // Implement |PlatformThread::Delegate|.
  void ThreadMain() {
    base::MachReceiveMessage message;
    kern_return_t err;
    while ((err = receive_port_->WaitForMessage(&message,
                                                MACH_MSG_TIMEOUT_NONE)) ==
           KERN_SUCCESS) {
      // 0 was the secret message id.  Reject any messages that don't have it.
      if (message.GetMessageID() != 0) {
        LOG(ERROR) << "Received message with incorrect id: "
                   << message.GetMessageID();
        continue;
      }

      const task_t child_task = message.GetTranslatedPort(0);
      if (child_task == MACH_PORT_NULL) {
        LOG(ERROR) << "parent GetTranslatedPort(0) failed.";
        continue;
      }

      // It is possible for the child process to die after the call to
      // |pid_for_task()| but before the call to |FinalizePid()|.  To prevent
      // leaking MachBroker map entries in this case, lock around both these
      // calls.  If the child dies, the death notification will be processed
      // after the call to FinalizePid(), ensuring proper cleanup.
      base::AutoLock lock(broker_->GetLock());

      int pid;
      err = pid_for_task(child_task, &pid);
      if (err == KERN_SUCCESS) {
        broker_->FinalizePid(pid,
                             MachBroker::MachInfo().SetTask(child_task));
      } else {
        LOG(ERROR) << "Error getting pid for task " << child_task
                   << ": " << MachErrorCode(err);
      }
    }

    LOG(ERROR) << "Mach listener thread exiting; "
               << "parent WaitForMessage() likely failed: "
               << MachErrorCode(err);
  }

 private:
  // The Mach port to listen on.  Created on thread startup.
  scoped_ptr<base::ReceivePort> receive_port_;

  // The MachBroker to use when new child task rights are received.  Can be
  // NULL.
  MachBroker* broker_;  // weak

  DISALLOW_COPY_AND_ASSIGN(MachListenerThreadDelegate);
};

// Returns the global MachBroker.
MachBroker* MachBroker::GetInstance() {
  return Singleton<MachBroker, LeakySingletonTraits<MachBroker> >::get();
}

MachBroker::MachBroker() : listener_thread_started_(false) {
}

MachBroker::~MachBroker() {}

void MachBroker::PrepareForFork() {
  if (!listener_thread_started_) {
    listener_thread_started_ = true;

    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE, new RegisterNotificationTask(this));

    // Intentional leak.  This thread is never joined or reaped.
    base::PlatformThread::CreateNonJoinable(
        0, new MachListenerThreadDelegate(this));
  }
}

// Adds a placeholder to the map for the given pid with MACH_PORT_NULL.
void MachBroker::AddPlaceholderForPid(base::ProcessHandle pid) {
  lock_.AssertAcquired();

  MachInfo mach_info;
  DCHECK_EQ(0u, mach_map_.count(pid));
  mach_map_[pid] = mach_info;
}

// Updates the mapping for |pid| to include the given |mach_info|.
void MachBroker::FinalizePid(base::ProcessHandle pid,
                             const MachInfo& mach_info) {
  lock_.AssertAcquired();

  const int count = mach_map_.count(pid);
  if (count == 0) {
    // Do nothing for unknown pids.
    LOG(ERROR) << "Unknown process " << pid << " is sending Mach IPC messages!";
    return;
  }

  DCHECK_EQ(1, count);
  DCHECK(mach_map_[pid].mach_task_ == MACH_PORT_NULL);
  if (mach_map_[pid].mach_task_ == MACH_PORT_NULL)
    mach_map_[pid] = mach_info;
}

// Removes all mappings belonging to |pid| from the broker.
void MachBroker::InvalidatePid(base::ProcessHandle pid) {
  base::AutoLock lock(lock_);
  MachBroker::MachMap::iterator it = mach_map_.find(pid);
  if (it == mach_map_.end())
    return;

  kern_return_t kr = mach_port_deallocate(mach_task_self(),
                                          it->second.mach_task_);
  LOG_IF(WARNING, kr != KERN_SUCCESS)
     << "Failed to mach_port_deallocate mach task " << it->second.mach_task_
     << ", error " << MachErrorCode(kr);
  mach_map_.erase(it);
}

base::Lock& MachBroker::GetLock() {
  return lock_;
}

// Returns the mach task belonging to |pid|.
mach_port_t MachBroker::TaskForPid(base::ProcessHandle pid) const {
  base::AutoLock lock(lock_);
  MachBroker::MachMap::const_iterator it = mach_map_.find(pid);
  if (it == mach_map_.end())
    return MACH_PORT_NULL;
  return it->second.mach_task_;
}

void MachBroker::Observe(NotificationType type,
                         const NotificationSource& source,
                         const NotificationDetails& details) {
  // TODO(rohitrao): These notifications do not always carry the proper PIDs,
  // especially when the renderer is already gone or has crashed.  Find a better
  // way to listen for child process deaths.  http://crbug.com/55734
  base::ProcessHandle handle = 0;
  switch (type.value) {
    case NotificationType::RENDERER_PROCESS_CLOSED:
    case NotificationType::RENDERER_PROCESS_TERMINATED:
      handle = Source<RenderProcessHost>(source)->GetHandle();
      break;
    case NotificationType::EXTENSION_PROCESS_TERMINATED:
      handle =
          Details<ExtensionHost>(details)->render_process_host()->GetHandle();
      break;
    case NotificationType::CHILD_PROCESS_CRASHED:
    case NotificationType::CHILD_PROCESS_HOST_DISCONNECTED:
      handle = Details<ChildProcessInfo>(details)->handle();
      break;
    default:
      NOTREACHED() << "Unexpected notification";
      break;
  }
  InvalidatePid(handle);
}

// static
std::string MachBroker::GetMachPortName() {
  static const char kFormatString[] =
#if defined(GOOGLE_CHROME_BUILD)
      "com.google.Chrome"
#else
      "org.chromium.Chromium"
#endif
      ".rohitfork.%d";

  const CommandLine& command_line = *CommandLine::ForCurrentProcess();
  const bool is_child = command_line.HasSwitch(switches::kProcessType);

  // In non-browser (child) processes, use the parent's pid.
  const pid_t pid = is_child ? getppid() : getpid();
  return StringPrintf(kFormatString, pid);
}
