// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_SHELL_RENDER_PROCESS_OBSERVER_H_
#define CONTENT_SHELL_SHELL_RENDER_PROCESS_OBSERVER_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/file_path.h"
#include "base/memory/scoped_ptr.h"
#include "content/public/renderer/render_process_observer.h"

namespace WebKit {
class WebFrame;
}

namespace WebTestRunner {
class WebTestDelegate;
class WebTestInterfaces;
}

namespace content {

class RenderView;
class WebKitTestRunner;

class ShellRenderProcessObserver : public RenderProcessObserver {
 public:
  static ShellRenderProcessObserver* GetInstance();

  ShellRenderProcessObserver();
  virtual ~ShellRenderProcessObserver();

  void SetMainWindow(RenderView* view,
                     WebKitTestRunner* test_runner,
                     WebTestRunner::WebTestDelegate* delegate);
  void BindTestRunnersToWindow(WebKit::WebFrame* frame);

  // RenderProcessObserver implementation.
  virtual void WebKitInitialized() OVERRIDE;
  virtual bool OnControlMessageReceived(const IPC::Message& message) OVERRIDE;

  WebTestRunner::WebTestDelegate* test_delegate() const {
    return test_delegate_;
  }
  WebTestRunner::WebTestInterfaces* test_interfaces() const {
    return test_interfaces_.get();
  }
  WebKitTestRunner* main_test_runner() const { return main_test_runner_; }
  const FilePath& webkit_source_dir() const { return webkit_source_dir_; }

 private:
  // Message handlers.
  void OnResetAll();
  void OnSetWebKitSourceDir(const FilePath& webkit_source_dir);

  scoped_ptr<WebTestRunner::WebTestInterfaces> test_interfaces_;
  RenderView* main_render_view_;
  WebKitTestRunner* main_test_runner_;
  WebTestRunner::WebTestDelegate* test_delegate_;

  FilePath webkit_source_dir_;

  DISALLOW_COPY_AND_ASSIGN(ShellRenderProcessObserver);
};

}  // namespace content

#endif  // CONTENT_SHELL_SHELL_RENDER_PROCESS_OBSERVER_H_
