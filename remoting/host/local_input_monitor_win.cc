// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/local_input_monitor.h"

#include "base/compiler_specific.h"
#include "base/logging.h"
#include "remoting/host/chromoting_host.h"
#include "remoting/host/local_input_monitor_thread_win.h"

namespace {

class LocalInputMonitorWin : public remoting::LocalInputMonitor {
 public:
  LocalInputMonitorWin();
  ~LocalInputMonitorWin();

  virtual void Start(remoting::ChromotingHost* host) OVERRIDE;
  virtual void Stop() OVERRIDE;

 private:
  DISALLOW_COPY_AND_ASSIGN(LocalInputMonitorWin);

  remoting::ChromotingHost* chromoting_host_;
};

}  // namespace


LocalInputMonitorWin::LocalInputMonitorWin() : chromoting_host_(NULL) {
}

LocalInputMonitorWin::~LocalInputMonitorWin() {
  DCHECK(chromoting_host_ == NULL);
}

void LocalInputMonitorWin::Start(remoting::ChromotingHost* host) {
  DCHECK(chromoting_host_ == NULL);
  chromoting_host_ = host;
  remoting::LocalInputMonitorThread::AddHostToInputMonitor(chromoting_host_);
}

void LocalInputMonitorWin::Stop() {
  DCHECK(chromoting_host_ != NULL);
  remoting::LocalInputMonitorThread::RemoveHostFromInputMonitor(
      chromoting_host_);
  chromoting_host_ = NULL;
}

remoting::LocalInputMonitor* remoting::LocalInputMonitor::Create() {
  return new LocalInputMonitorWin;
}
