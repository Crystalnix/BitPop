// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/base/view_event_test_base.h"

#if defined(OS_WIN)
#include <ole2.h>
#endif

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/message_loop.h"
#include "base/string_number_conversions.h"
#include "chrome/browser/automation/ui_controls.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/browser_thread.h"
#include "ui/gfx/compositor/test/compositor_test_support.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

#if defined(USE_AURA)
#include "ash/shell.h"
#include "ui/aura/root_window.h"
#endif

namespace {

// View subclass that allows you to specify the preferred size.
class TestView : public views::View {
 public:
  TestView() {}

  void SetPreferredSize(const gfx::Size& size) {
    preferred_size_ = size;
    PreferredSizeChanged();
  }

  gfx::Size GetPreferredSize() {
    if (!preferred_size_.IsEmpty())
      return preferred_size_;
    return View::GetPreferredSize();
  }

  virtual void Layout() {
    View* child_view = child_at(0);
    child_view->SetBounds(0, 0, width(), height());
  }

 private:
  gfx::Size preferred_size_;

  DISALLOW_COPY_AND_ASSIGN(TestView);
};

// Delay in background thread before posting mouse move.
const int kMouseMoveDelayMS = 200;

}  // namespace

ViewEventTestBase::ViewEventTestBase()
  : window_(NULL),
    content_view_(NULL),
    ui_thread_(content::BrowserThread::UI, &message_loop_) {
}

void ViewEventTestBase::Done() {
  MessageLoop::current()->Quit();

#if defined(OS_WIN)
  // We need to post a message to tickle the Dispatcher getting called and
  // exiting out of the nested loop. Without this the quit never runs.
  PostMessage(window_->GetNativeWindow(), WM_USER, 0, 0);
#endif

  // If we're in a nested message loop, as is the case with menus, we
  // need to quit twice. The second quit does that for us. Finish all
  // pending UI events before posting closure because events it may be
  // executed before UI events are executed.
  ui_controls::RunClosureAfterAllPendingUIEvents(MessageLoop::QuitClosure());
}

void ViewEventTestBase::SetUp() {
#if defined(OS_WIN)
  OleInitialize(NULL);
#endif
  ui::CompositorTestSupport::Initialize();
#if defined(USE_AURA)
  aura::RootWindow::GetInstance();
  ash::Shell::CreateInstance(NULL);
#endif
  window_ = views::Widget::CreateWindow(this);
}

void ViewEventTestBase::TearDown() {
  if (window_) {
#if defined(OS_WIN)
    DestroyWindow(window_->GetNativeWindow());
#else
    window_->Close();
    ui_test_utils::RunAllPendingInMessageLoop();
#endif
    window_ = NULL;
  }
#if defined(USE_AURA)
  ash::Shell::DeleteInstance();
  aura::RootWindow::DeleteInstance();
#endif
  ui::CompositorTestSupport::Terminate();
#if defined(OS_WIN)
  OleUninitialize();
#endif
}

bool ViewEventTestBase::CanResize() const {
  return true;
}

views::View* ViewEventTestBase::GetContentsView() {
  if (!content_view_) {
    // Wrap the real view (as returned by CreateContentsView) in a View so
    // that we can customize the preferred size.
    TestView* test_view = new TestView();
    test_view->SetPreferredSize(GetPreferredSize());
    test_view->AddChildView(CreateContentsView());
    content_view_ = test_view;
  }
  return content_view_;
}

const views::Widget* ViewEventTestBase::GetWidget() const {
  return content_view_->GetWidget();
}

views::Widget* ViewEventTestBase::GetWidget() {
  return content_view_->GetWidget();
}

ViewEventTestBase::~ViewEventTestBase() {
}

void ViewEventTestBase::StartMessageLoopAndRunTest() {
  window_->Show();
  // Make sure the window is the foreground window, otherwise none of the
  // mouse events are going to be targeted correctly.
#if defined(OS_WIN)
  SetForegroundWindow(window_->GetNativeWindow());
#endif

  // Flush any pending events to make sure we start with a clean slate.
  ui_test_utils::RunAllPendingInMessageLoop();

  // Schedule a task that starts the test. Need to do this as we're going to
  // run the message loop.
  MessageLoop::current()->PostTask(
      FROM_HERE,
      base::Bind(&ViewEventTestBase::DoTestOnMessageLoop, this));

  ui_test_utils::RunMessageLoop();
}

gfx::Size ViewEventTestBase::GetPreferredSize() {
  return gfx::Size();
}

void ViewEventTestBase::ScheduleMouseMoveInBackground(int x, int y) {
  if (!dnd_thread_.get()) {
    dnd_thread_.reset(new base::Thread("mouse-move-thread"));
    dnd_thread_->Start();
  }
  dnd_thread_->message_loop()->PostDelayedTask(
      FROM_HERE,
      base::Bind(base::IgnoreResult(&ui_controls::SendMouseMove), x, y),
      kMouseMoveDelayMS);
}

void ViewEventTestBase::StopBackgroundThread() {
  dnd_thread_.reset(NULL);
}

void ViewEventTestBase::RunTestMethod(const base::Closure& task) {
  StopBackgroundThread();

  task.Run();
  if (HasFatalFailure())
    Done();
}
