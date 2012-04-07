// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_SHELL_BROWSER_MAIN_H_
#define CONTENT_SHELL_SHELL_BROWSER_MAIN_H_
#pragma once

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "content/public/browser/browser_main_parts.h"

namespace base {
class Thread;
}

namespace ui {
class Clipboard;
}

namespace content {

class ShellBrowserContext;
class ShellDevToolsDelegate;
struct MainFunctionParams;

class ShellBrowserMainParts : public BrowserMainParts {
 public:
  explicit ShellBrowserMainParts(const content::MainFunctionParams& parameters);
  virtual ~ShellBrowserMainParts();

  virtual void PreEarlyInitialization() OVERRIDE {}
  virtual void PostEarlyInitialization() OVERRIDE {}
  virtual void PreMainMessageLoopStart() OVERRIDE {}
  virtual void ToolkitInitialized() OVERRIDE {}
  virtual void PostMainMessageLoopStart() OVERRIDE {}
  virtual int PreCreateThreads() OVERRIDE;
  virtual void PreMainMessageLoopRun() OVERRIDE;
  virtual bool MainMessageLoopRun(int* result_code) OVERRIDE;
  virtual void PostMainMessageLoopRun() OVERRIDE;
  virtual void PostDestroyThreads() OVERRIDE {}

  ui::Clipboard* GetClipboard();
  ShellDevToolsDelegate* devtools_delegate() { return devtools_delegate_; }

 private:
  scoped_ptr<ShellBrowserContext> browser_context_;

  scoped_ptr<ui::Clipboard> clipboard_;
  ShellDevToolsDelegate* devtools_delegate_;

  DISALLOW_COPY_AND_ASSIGN(ShellBrowserMainParts);
};

}  // namespace content

#endif  // CONTENT_SHELL_SHELL_BROWSER_MAIN_H_
