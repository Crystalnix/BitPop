// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/test/content_browser_test.h"

#include "base/command_line.h"
#include "base/debug/stack_trace.h"
#include "base/file_path.h"
#include "base/logging.h"
#include "base/message_loop.h"
#include "base/path_service.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/url_constants.h"
#include "content/shell/shell.h"
#include "content/shell/shell_browser_context.h"
#include "content/shell/shell_content_browser_client.h"
#include "content/shell/shell_content_renderer_client.h"
#include "content/shell/shell_main_delegate.h"
#include "content/shell/shell_switches.h"
#include "content/test/test_content_client.h"

#if defined(OS_MACOSX)
#include "base/mac/scoped_nsautorelease_pool.h"
#endif

namespace content {

ContentBrowserTest::ContentBrowserTest() {
#if defined(OS_MACOSX)
  // See comment in InProcessBrowserTest::InProcessBrowserTest().
  FilePath content_shell_path;
  CHECK(PathService::Get(base::FILE_EXE, &content_shell_path));
  content_shell_path = content_shell_path.DirName();
  content_shell_path = content_shell_path.Append(
      FILE_PATH_LITERAL("Content Shell.app/Contents/MacOS/Content Shell"));
  CHECK(PathService::Override(base::FILE_EXE, content_shell_path));
#endif
  CreateTestServer("content/test/data");
}

ContentBrowserTest::~ContentBrowserTest() {
}

void ContentBrowserTest::SetUp() {
  shell_main_delegate_.reset(new ShellMainDelegate);
  shell_main_delegate_->PreSandboxStartup();

  CommandLine* command_line = CommandLine::ForCurrentProcess();
  command_line->AppendSwitch(switches::kContentBrowserTest);

#if defined(OS_LINUX)
  // http://crbug.com/139209
  command_line->AppendSwitch(switches::kDisableGpuProcessPrelaunch);
#endif

  SetUpCommandLine(command_line);

  // Single-process mode is not set in BrowserMain, so process it explicitly,
  // and set up renderer.
  if (command_line->HasSwitch(switches::kSingleProcess)) {
    RenderProcessHost::set_run_renderer_in_process(true);
    single_process_renderer_client_.reset(new ShellContentRendererClient);
    content::GetContentClient()->set_renderer_for_testing(
        single_process_renderer_client_.get());
  }

#if defined(OS_MACOSX)
  // See InProcessBrowserTest::PrepareTestCommandLine().
  FilePath subprocess_path;
  PathService::Get(base::FILE_EXE, &subprocess_path);
  subprocess_path = subprocess_path.DirName().DirName();
  DCHECK_EQ(subprocess_path.BaseName().value(), "Contents");
  subprocess_path = subprocess_path.Append(
      "Frameworks/Content Shell Helper.app/Contents/MacOS/Content Shell Helper");
  command_line->AppendSwitchPath(switches::kBrowserSubprocessPath,
                                 subprocess_path);
#endif

  BrowserTestBase::SetUp();
}

void ContentBrowserTest::TearDown() {
  BrowserTestBase::TearDown();

  shell_main_delegate_.reset();
}

#if defined(OS_POSIX)
// On SIGTERM (sent by the runner on timeouts), dump a stack trace (to make
// debugging easier) and also exit with a known error code (so that the test
// framework considers this a failure -- http://crbug.com/57578).
static void DumpStackTraceSignalHandler(int signal) {
  base::debug::StackTrace().PrintBacktrace();
  _exit(128 + signal);
}
#endif  // defined(OS_POSIX)

void ContentBrowserTest::RunTestOnMainThreadLoop() {
  CHECK_EQ(Shell::windows().size(), 1u);
  shell_ = Shell::windows()[0];

#if defined(OS_POSIX)
  signal(SIGTERM, DumpStackTraceSignalHandler);
#endif  // defined(OS_POSIX)

#if defined(OS_MACOSX)
  // On Mac, without the following autorelease pool, code which is directly
  // executed (as opposed to executed inside a message loop) would autorelease
  // objects into a higher-level pool. This pool is not recycled in-sync with
  // the message loops' pools and causes problems with code relying on
  // deallocation via an autorelease pool (such as browser window closure and
  // browser shutdown). To avoid this, the following pool is recycled after each
  // time code is directly executed.
  base::mac::ScopedNSAutoreleasePool pool;
#endif

  // Pump startup related events.
  MessageLoopForUI::current()->RunAllPending();

#if defined(OS_MACOSX)
  pool.Recycle();
#endif

  SetUpOnMainThread();

  RunTestOnMainThread();
#if defined(OS_MACOSX)
  pool.Recycle();
#endif

  for (RenderProcessHost::iterator i(RenderProcessHost::AllHostsIterator());
       !i.IsAtEnd(); i.Advance()) {
    i.GetCurrentValue()->FastShutdownIfPossible();
  }

  Shell::CloseAllWindows();
}

Shell* ContentBrowserTest::CreateBrowser() {
  ShellContentBrowserClient* browser_client =
      static_cast<ShellContentBrowserClient*>(GetContentClient()->browser());
  return Shell::CreateNewWindow(
      browser_client->browser_context(),
      GURL(chrome::kAboutBlankURL),
      NULL,
      MSG_ROUTING_NONE,
      NULL);
}

Shell* ContentBrowserTest::CreateOffTheRecordBrowser() {
  ShellContentBrowserClient* browser_client =
      static_cast<ShellContentBrowserClient*>(GetContentClient()->browser());
  return Shell::CreateNewWindow(
      browser_client->off_the_record_browser_context(),
      GURL(chrome::kAboutBlankURL),
      NULL,
      MSG_ROUTING_NONE,
      NULL);
}

}  // namespace content
