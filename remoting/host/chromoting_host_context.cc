// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/chromoting_host_context.h"

#include <string>

#include "base/bind.h"
#include "base/threading/thread.h"
#include "remoting/host/url_request_context.h"

namespace remoting {

ChromotingHostContext::ChromotingHostContext(
    scoped_refptr<base::SingleThreadTaskRunner> ui_task_runner)
    : network_thread_("ChromotingNetworkThread"),
      capture_thread_("ChromotingCaptureThread"),
      encode_thread_("ChromotingEncodeThread"),
      desktop_thread_("ChromotingDesktopThread"),
      file_thread_("ChromotingFileIOThread"),
      ui_task_runner_(ui_task_runner) {
}

ChromotingHostContext::~ChromotingHostContext() {
}

bool ChromotingHostContext::Start() {
  // Start all the threads.
  bool started = capture_thread_.Start() && encode_thread_.Start() &&
      network_thread_.StartWithOptions(base::Thread::Options(
          MessageLoop::TYPE_IO, 0)) &&
      desktop_thread_.Start() &&
      file_thread_.StartWithOptions(
          base::Thread::Options(MessageLoop::TYPE_IO, 0));
  if (!started)
    return false;

  url_request_context_getter_ = new URLRequestContextGetter(
      ui_task_runner(), network_task_runner(),
      static_cast<MessageLoopForIO*>(file_thread_.message_loop()));
  return true;
}

base::SingleThreadTaskRunner* ChromotingHostContext::capture_task_runner() {
  return capture_thread_.message_loop_proxy();
}

base::SingleThreadTaskRunner* ChromotingHostContext::encode_task_runner() {
  return encode_thread_.message_loop_proxy();
}

base::SingleThreadTaskRunner* ChromotingHostContext::network_task_runner() {
  return network_thread_.message_loop_proxy();
}

base::SingleThreadTaskRunner* ChromotingHostContext::desktop_task_runner() {
  return desktop_thread_.message_loop_proxy();
}

base::SingleThreadTaskRunner* ChromotingHostContext::ui_task_runner() {
  return ui_task_runner_;
}

base::SingleThreadTaskRunner* ChromotingHostContext::file_task_runner() {
  return file_thread_.message_loop_proxy();
}

const scoped_refptr<net::URLRequestContextGetter>&
ChromotingHostContext::url_request_context_getter() {
  DCHECK(url_request_context_getter_.get());
  return url_request_context_getter_;
}

}  // namespace remoting
