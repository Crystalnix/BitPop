// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_NACL_HOST_NACL_PROCESS_HOST_H_
#define CHROME_BROWSER_NACL_HOST_NACL_PROCESS_HOST_H_
#pragma once

#include "build/build_config.h"

#include "base/file_path.h"
#include "base/file_util_proxy.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/process.h"
#include "chrome/common/nacl_types.h"
#include "content/public/browser/browser_child_process_host_delegate.h"

class ChromeRenderMessageFilter;

namespace content {
class BrowserChildProcessHost;
}

// Represents the browser side of the browser <--> NaCl communication
// channel. There will be one NaClProcessHost per NaCl process
// The browser is responsible for starting the NaCl process
// when requested by the renderer.
// After that, most of the communication is directly between NaCl plugin
// running in the renderer and NaCl processes.
class NaClProcessHost : public content::BrowserChildProcessHostDelegate {
 public:
  explicit NaClProcessHost(const std::wstring& url);
  virtual ~NaClProcessHost();

  // Do any minimal work that must be done at browser startup.
  static void EarlyStartup();

  // Initialize the new NaCl process, returning true on success. On success,
  // the NaCl process host will assume responsibility for sending the reply
  // message. On failure, the reply will not be sent and this is the caller's
  // responsibility to avoid hanging the renderer.
  bool Launch(ChromeRenderMessageFilter* chrome_render_message_filter,
              int socket_count,
              IPC::Message* reply_msg);

  void OnProcessLaunchedByBroker(base::ProcessHandle handle);

 private:
  // Internal class that holds the nacl::Handle objecs so that
  // nacl_process_host.h doesn't include NaCl headers.  Needed since it's
  // included by src\content, which can't depend on the NaCl gyp file because it
  // depends on chrome.gyp (circular dependency).
  struct NaClInternal;

  bool LaunchSelLdr();

  // BrowserChildProcessHostDelegate implementation:
  virtual bool OnMessageReceived(const IPC::Message& msg) OVERRIDE;
  virtual void OnProcessCrashed(int exit_code) OVERRIDE;
  virtual void OnProcessLaunched() OVERRIDE;

  void IrtReady();
  void SendStart(base::PlatformFile irt_file);

 private:
  // The ChromeRenderMessageFilter that requested this NaCl process.  We use
  // this for sending the reply once the process has started.
  scoped_refptr<ChromeRenderMessageFilter> chrome_render_message_filter_;

  // The reply message to send. We must always send this message when the
  // sub-process either succeeds or fails to unblock the renderer waiting for
  // the reply. NULL when there is no reply to send.
  IPC::Message* reply_msg_;

  // Socket pairs for the NaCl process and renderer.
  scoped_ptr<NaClInternal> internal_;

  base::WeakPtrFactory<NaClProcessHost> weak_factory_;

  scoped_ptr<content::BrowserChildProcessHost> process_;

  DISALLOW_COPY_AND_ASSIGN(NaClProcessHost);
};

#endif  // CHROME_BROWSER_NACL_HOST_NACL_PROCESS_HOST_H_
