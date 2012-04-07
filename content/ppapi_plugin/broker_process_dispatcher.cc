// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/ppapi_plugin/broker_process_dispatcher.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "content/common/child_process.h"

namespace {

// How long we wait before releasing the broker process.
const int kBrokerReleaseTimeSeconds = 30;

}  // namespace

BrokerProcessDispatcher::BrokerProcessDispatcher(
    base::ProcessHandle remote_process_handle,
    PP_ConnectInstance_Func connect_instance)
    : ppapi::proxy::BrokerSideDispatcher(remote_process_handle,
                                         connect_instance) {
  ChildProcess::current()->AddRefProcess();
}

BrokerProcessDispatcher::~BrokerProcessDispatcher() {
  DVLOG(1) << "BrokerProcessDispatcher::~BrokerProcessDispatcher()";
  // Don't free the process right away. This timer allows the child process
  // to be re-used if the user rapidly goes to a new page that requires this
  // plugin. This is the case for common plugins where they may be used on a
  // source and destination page of a navigation. We don't want to tear down
  // and re-start processes each time in these cases.
  MessageLoop::current()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&ChildProcess::ReleaseProcess,
                 base::Unretained(ChildProcess::current())),
      base::TimeDelta::FromSeconds(kBrokerReleaseTimeSeconds));
}
