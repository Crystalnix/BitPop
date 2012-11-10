// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/test/test_launcher.h"

#include <stack>

#include "base/command_line.h"
#include "base/logging.h"
#include "base/memory/linked_ptr.h"
#include "base/run_loop.h"
#include "base/scoped_temp_dir.h"
#include "base/test/test_file_util.h"
#include "chrome/app/chrome_main_delegate.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/test/base/chrome_test_suite.h"
#include "content/public/app/content_main.h"
#include "content/public/browser/browser_thread.h"

#if defined(OS_MACOSX)
#include "chrome/browser/chrome_browser_application_mac.h"
#endif  // defined(OS_MACOSX)

#if defined(OS_WIN)
#include "content/public/app/startup_helper_win.h"
#include "sandbox/win/src/sandbox_types.h"
#endif  // defined(OS_WIN)

#if defined(TOOLKIT_VIEWS)
#include "ui/views/focus/accelerator_handler.h"
#endif

const char kEmptyTestName[] = "InProcessBrowserTest.Empty";

class ChromeTestLauncherDelegate : public test_launcher::TestLauncherDelegate {
 public:
  ChromeTestLauncherDelegate() {}
  virtual ~ChromeTestLauncherDelegate() {}

  virtual std::string GetEmptyTestName() OVERRIDE {
    return kEmptyTestName;
  }

  virtual bool Run(int argc, char** argv, int* return_code) OVERRIDE {
#if defined(OS_WIN) || defined(OS_LINUX)
    CommandLine* command_line = CommandLine::ForCurrentProcess();
    bool launch_chrome =
        command_line->HasSwitch(switches::kProcessType) ||
        command_line->HasSwitch(ChromeTestSuite::kLaunchAsBrowser);
#endif
#if defined(OS_WIN)
    if (launch_chrome) {
      sandbox::SandboxInterfaceInfo sandbox_info = {0};
      content::InitializeSandboxInfo(&sandbox_info);
      ChromeMainDelegate chrome_main_delegate;
      *return_code = content::ContentMain(GetModuleHandle(NULL),
                                          &sandbox_info,
                                          &chrome_main_delegate);
      return true;
    }
#elif defined(OS_LINUX)
    if (launch_chrome) {
      ChromeMainDelegate chrome_main_delegate;
      *return_code = content::ContentMain(argc,
                                          const_cast<const char**>(argv),
                                          &chrome_main_delegate);
      return true;
    }
#endif  // defined(OS_WIN)

    return false;
  }

  virtual int RunTestSuite(int argc, char** argv) OVERRIDE {
    return ChromeTestSuite(argc, argv).Run();
  }

  virtual bool AdjustChildProcessCommandLine(
      CommandLine* command_line) OVERRIDE {
    CommandLine new_command_line(command_line->GetProgram());
    CommandLine::SwitchMap switches = command_line->GetSwitches();

    // Strip out user-data-dir if present.  We will add it back in again later.
    switches.erase(switches::kUserDataDir);

    for (CommandLine::SwitchMap::const_iterator iter = switches.begin();
         iter != switches.end(); ++iter) {
      new_command_line.AppendSwitchNative((*iter).first, (*iter).second);
    }

    // Clean up previous temp dir.
    // We Take() the directory and delete it ourselves so that the next
    // CreateUniqueTempDir will succeed even if deleting the directory fails.
    if (!temp_dir_.path().empty() &&
        !file_util::DieFileDie(temp_dir_.Take(), true)) {
      LOG(ERROR) << "Error deleting previous temp profile directory";
    }

    // Create a new user data dir and pass it to the child.
    if (!temp_dir_.CreateUniqueTempDir() || !temp_dir_.IsValid()) {
      LOG(ERROR) << "Error creating temp profile directory";
      return false;
    }
    new_command_line.AppendSwitchPath(switches::kUserDataDir, temp_dir_.path());

    // file:// access for ChromeOS.
    new_command_line.AppendSwitch(switches::kAllowFileAccess);

    *command_line = new_command_line;
    return true;
  }

  virtual void PreRunMessageLoop(base::RunLoop* run_loop) OVERRIDE {
#if !defined(USE_AURA) && defined(TOOLKIT_VIEWS)
    if (content::BrowserThread::CurrentlyOn(content::BrowserThread::UI)) {
      linked_ptr<views::AcceleratorHandler> handler(
          new views::AcceleratorHandler);
      handlers_.push(handler);
      run_loop->set_dispatcher(handler.get());
    }
#endif
  }

  virtual void PostRunMessageLoop() {
#if !defined(USE_AURA) && defined(TOOLKIT_VIEWS)
    if (content::BrowserThread::CurrentlyOn(content::BrowserThread::UI)) {
      DCHECK_EQ(handlers_.empty(), false);
      handlers_.pop();
    }
#endif
  }

 private:
  ScopedTempDir temp_dir_;

#if !defined(USE_AURA) && defined(TOOLKIT_VIEWS)
  std::stack<linked_ptr<views::AcceleratorHandler> > handlers_;
#endif

  DISALLOW_COPY_AND_ASSIGN(ChromeTestLauncherDelegate);
};

int main(int argc, char** argv) {
#if defined(OS_MACOSX)
  chrome_browser_application_mac::RegisterBrowserCrApp();
#endif
  ChromeTestLauncherDelegate launcher_delegate;
  return test_launcher::LaunchTests(&launcher_delegate, argc, argv);
}
