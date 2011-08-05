// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/service/service_process_control.h"

#include "base/command_line.h"
#include "base/file_path.h"
#include "base/process_util.h"
#include "base/stl_util-inl.h"
#include "base/threading/thread.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/upgrade_detector.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/service_messages.h"
#include "chrome/common/service_process_util.h"
#include "content/browser/browser_thread.h"
#include "content/common/child_process_host.h"
#include "content/common/notification_service.h"
#include "ui/base/ui_base_switches.h"


// ServiceProcessControl implementation.
ServiceProcessControl::ServiceProcessControl(Profile* profile)
    : profile_(profile) {
}

ServiceProcessControl::~ServiceProcessControl() {
  STLDeleteElements(&connect_done_tasks_);
  STLDeleteElements(&connect_success_tasks_);
  STLDeleteElements(&connect_failure_tasks_);
}

void ServiceProcessControl::ConnectInternal() {
  // If the channel has already been established then we run the task
  // and return.
  if (channel_.get()) {
    RunConnectDoneTasks();
    return;
  }

  // Actually going to connect.
  VLOG(1) << "Connecting to Service Process IPC Server";

  // TODO(hclam): Handle error connecting to channel.
  const IPC::ChannelHandle channel_id = GetServiceProcessChannel();
  channel_.reset(new IPC::SyncChannel(
      channel_id, IPC::Channel::MODE_NAMED_CLIENT, this,
      BrowserThread::GetMessageLoopProxyForThread(BrowserThread::IO), true,
      g_browser_process->shutdown_event()));
}

void ServiceProcessControl::RunConnectDoneTasks() {
  // The tasks executed here may add more tasks to the vector. So copy
  // them to the stack before executing them. This way recursion is
  // avoided.
  TaskList tasks;
  tasks.swap(connect_done_tasks_);
  RunAllTasksHelper(&tasks);
  DCHECK(tasks.empty());

  if (is_connected()) {
    tasks.swap(connect_success_tasks_);
    RunAllTasksHelper(&tasks);
    DCHECK(tasks.empty());

    STLDeleteElements(&connect_failure_tasks_);
  } else {
    tasks.swap(connect_failure_tasks_);
    RunAllTasksHelper(&tasks);
    DCHECK(tasks.empty());

    STLDeleteElements(&connect_success_tasks_);
  }

  DCHECK(connect_done_tasks_.empty());
  DCHECK(connect_success_tasks_.empty());
  DCHECK(connect_failure_tasks_.empty());
}

// static
void ServiceProcessControl::RunAllTasksHelper(TaskList* task_list) {
  TaskList::iterator index = task_list->begin();
  while (index != task_list->end()) {
    (*index)->Run();
    delete (*index);
    index = task_list->erase(index);
  }
}

void ServiceProcessControl::Launch(Task* success_task, Task* failure_task) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (success_task) {
    if (success_task == failure_task) {
      // If the tasks are the same, then the same task needs to be invoked
      // for success and failure.
      failure_task = NULL;
      connect_done_tasks_.push_back(success_task);
    } else {
      connect_success_tasks_.push_back(success_task);
    }
  }

  if (failure_task)
    connect_failure_tasks_.push_back(failure_task);

  // If we already in the process of launching, then we are done.
  if (launcher_) {
    return;
  }

  // If the service process is already running then connects to it.
  if (CheckServiceProcessReady()) {
    ConnectInternal();
    return;
  }

  // A service process should have a different mechanism for starting, but now
  // we start it as if it is a child process.
  FilePath exe_path = ChildProcessHost::GetChildPath(true);
  if (exe_path.empty()) {
    NOTREACHED() << "Unable to get service process binary name.";
  }

  CommandLine* cmd_line = new CommandLine(exe_path);
  cmd_line->AppendSwitchASCII(switches::kProcessType,
                              switches::kServiceProcess);

  const CommandLine& browser_command_line = *CommandLine::ForCurrentProcess();
  FilePath user_data_dir =
      browser_command_line.GetSwitchValuePath(switches::kUserDataDir);
  if (!user_data_dir.empty())
    cmd_line->AppendSwitchPath(switches::kUserDataDir, user_data_dir);

  std::string logging_level = browser_command_line.GetSwitchValueASCII(
      switches::kLoggingLevel);
  if (!logging_level.empty())
    cmd_line->AppendSwitchASCII(switches::kLoggingLevel, logging_level);

  std::string v_level = browser_command_line.GetSwitchValueASCII(
      switches::kV);
  if (!v_level.empty())
    cmd_line->AppendSwitchASCII(switches::kV, v_level);

  std::string v_modules = browser_command_line.GetSwitchValueASCII(
      switches::kVModule);
  if (!v_modules.empty())
    cmd_line->AppendSwitchASCII(switches::kVModule, v_modules);

  if (browser_command_line.HasSwitch(switches::kWaitForDebuggerChildren)) {
    cmd_line->AppendSwitch(switches::kWaitForDebugger);
  }

  if (browser_command_line.HasSwitch(switches::kEnableLogging)) {
    cmd_line->AppendSwitch(switches::kEnableLogging);
  }

  std::string locale = g_browser_process->GetApplicationLocale();
  cmd_line->AppendSwitchASCII(switches::kLang, locale);

  // And then start the process asynchronously.
  launcher_ = new Launcher(this, cmd_line);
  launcher_->Run(
      NewRunnableMethod(this, &ServiceProcessControl::OnProcessLaunched));
}

void ServiceProcessControl::OnProcessLaunched() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (launcher_->launched()) {
    // After we have successfully created the service process we try to connect
    // to it. The launch task is transfered to a connect task.
    ConnectInternal();
  } else {
    // If we don't have process handle that means launching the service process
    // has failed.
    RunConnectDoneTasks();
  }

  // We don't need the launcher anymore.
  launcher_ = NULL;
}

bool ServiceProcessControl::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(ServiceProcessControl, message)
    IPC_MESSAGE_HANDLER(ServiceHostMsg_CloudPrintProxy_Info,
                        OnCloudPrintProxyInfo)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void ServiceProcessControl::OnChannelConnected(int32 peer_pid) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  channel_->set_sync_messages_with_no_timeout_allowed(false);

  // We just established a channel with the service process. Notify it if an
  // upgrade is available.
  if (UpgradeDetector::GetInstance()->notify_upgrade()) {
    Send(new ServiceMsg_UpdateAvailable);
  } else {
    if (registrar_.IsEmpty())
      registrar_.Add(this, NotificationType::UPGRADE_RECOMMENDED,
                     NotificationService::AllSources());
  }
  RunConnectDoneTasks();
}

void ServiceProcessControl::OnChannelError() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  channel_.reset();
  RunConnectDoneTasks();
}

bool ServiceProcessControl::Send(IPC::Message* message) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (!channel_.get())
    return false;
  return channel_->Send(message);
}

// NotificationObserver implementation.
void ServiceProcessControl::Observe(NotificationType type,
                                    const NotificationSource& source,
                                    const NotificationDetails& details) {
  if (type == NotificationType::UPGRADE_RECOMMENDED) {
    Send(new ServiceMsg_UpdateAvailable);
  }
}

void ServiceProcessControl::OnCloudPrintProxyInfo(
    const cloud_print::CloudPrintProxyInfo& proxy_info) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (cloud_print_info_callback_ != NULL) {
    cloud_print_info_callback_->Run(proxy_info);
    cloud_print_info_callback_.reset();
  }
}

bool ServiceProcessControl::GetCloudPrintProxyInfo(
    CloudPrintProxyInfoHandler* cloud_print_info_callback) {
  DCHECK(cloud_print_info_callback);
  cloud_print_info_callback_.reset(cloud_print_info_callback);
  return Send(new ServiceMsg_GetCloudPrintProxyInfo());
}

bool ServiceProcessControl::Shutdown() {
  bool ret = Send(new ServiceMsg_Shutdown());
  channel_.reset();
  return ret;
}

DISABLE_RUNNABLE_METHOD_REFCOUNT(ServiceProcessControl);

ServiceProcessControl::Launcher::Launcher(ServiceProcessControl* process,
                                          CommandLine* cmd_line)
    : process_(process),
      cmd_line_(cmd_line),
      launched_(false),
      retry_count_(0) {
}

// Execute the command line to start the process asynchronously.
// After the command is executed, |task| is called with the process handle on
// the UI thread.
void ServiceProcessControl::Launcher::Run(Task* task) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  notify_task_.reset(task);
  BrowserThread::PostTask(BrowserThread::PROCESS_LAUNCHER, FROM_HERE,
                         NewRunnableMethod(this, &Launcher::DoRun));
}

ServiceProcessControl::Launcher::~Launcher() {}

void ServiceProcessControl::Launcher::Notify() {
  DCHECK(notify_task_.get());
  notify_task_->Run();
  notify_task_.reset();
}

#if !defined(OS_MACOSX)
void ServiceProcessControl::Launcher::DoDetectLaunched() {
  DCHECK(notify_task_.get());
  const uint32 kMaxLaunchDetectRetries = 10;
  launched_ = CheckServiceProcessReady();
  if (launched_ || (retry_count_ >= kMaxLaunchDetectRetries)) {
    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
        NewRunnableMethod(this, &Launcher::Notify));
    return;
  }
  retry_count_++;

  // If the service process is not launched yet then check again in 2 seconds.
  const int kDetectLaunchRetry = 2000;
  MessageLoop::current()->PostDelayedTask(
      FROM_HERE,
      NewRunnableMethod(this, &Launcher::DoDetectLaunched),
      kDetectLaunchRetry);
}

void ServiceProcessControl::Launcher::DoRun() {
  DCHECK(notify_task_.get());
  if (base::LaunchApp(*cmd_line_, false, true, NULL)) {
    BrowserThread::PostTask(BrowserThread::IO, FROM_HERE,
                            NewRunnableMethod(this,
                                              &Launcher::DoDetectLaunched));
  } else {
    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                            NewRunnableMethod(this, &Launcher::Notify));
  }
}
#endif  // !OS_MACOSX
