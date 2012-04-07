// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/shell/shell_browser_main.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/message_loop.h"
#include "base/string_number_conversions.h"
#include "base/threading/thread.h"
#include "base/threading/thread_restrictions.h"
#include "content/browser/browser_process_sub_thread.h"
#include "content/browser/download/download_file_manager.h"
#include "content/browser/download/save_file_manager.h"
#include "content/browser/plugin_service_impl.h"
#include "content/public/common/content_switches.h"
#include "content/shell/shell.h"
#include "content/shell/shell_browser_context.h"
#include "content/shell/shell_content_browser_client.h"
#include "content/shell/shell_devtools_delegate.h"
#include "net/base/net_module.h"
#include "ui/base/clipboard/clipboard.h"

namespace content {

static GURL GetStartupURL() {
  const CommandLine::StringVector& args =
      CommandLine::ForCurrentProcess()->GetArgs();
  if (args.empty())
    return GURL("http://www.google.com/");

  return GURL(args[0]);
}

ShellBrowserMainParts::ShellBrowserMainParts(
    const content::MainFunctionParams& parameters)
    : BrowserMainParts(),
      devtools_delegate_(NULL) {
  ShellContentBrowserClient* shell_browser_client =
      static_cast<ShellContentBrowserClient*>(
          content::GetContentClient()->browser());
  shell_browser_client->set_shell_browser_main_parts(this);
}

ShellBrowserMainParts::~ShellBrowserMainParts() {
}

int ShellBrowserMainParts::PreCreateThreads() {
  return 0;
}

void ShellBrowserMainParts::PreMainMessageLoopRun() {
  browser_context_.reset(new ShellBrowserContext(this));

  Shell::PlatformInitialize();
  net::NetModule::SetResourceProvider(Shell::PlatformResourceProvider);

  const CommandLine& command_line = *CommandLine::ForCurrentProcess();
  if (command_line.HasSwitch(switches::kRemoteDebuggingPort)) {
    std::string port_str =
        command_line.GetSwitchValueASCII(switches::kRemoteDebuggingPort);
    int port;
    if (base::StringToInt(port_str, &port) && port > 0 && port < 65535) {
      devtools_delegate_ = new ShellDevToolsDelegate(
          port,
          browser_context_->GetRequestContext());
    } else {
      DLOG(WARNING) << "Invalid http debugger port number " << port;
    }
  }

  Shell::CreateNewWindow(browser_context_.get(),
                         GetStartupURL(),
                         NULL,
                         MSG_ROUTING_NONE,
                         NULL);
}

void ShellBrowserMainParts::PostMainMessageLoopRun() {
  if (devtools_delegate_)
    devtools_delegate_->Stop();
  browser_context_.reset();
}

bool ShellBrowserMainParts::MainMessageLoopRun(int* result_code) {
  return false;
}

ui::Clipboard* ShellBrowserMainParts::GetClipboard() {
  if (!clipboard_.get())
    clipboard_.reset(new ui::Clipboard());
  return clipboard_.get();
}

}  // namespace
