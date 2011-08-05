// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views/widget/native_widget_win.h"

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace views {
namespace {

class NativeWidgetWinTest : public testing::Test {
 public:
  NativeWidgetWinTest() {
    OleInitialize(NULL);
  }

  ~NativeWidgetWinTest() {
    OleUninitialize();
  }

  virtual void TearDown() {
    // Flush the message loop because we have pending release tasks
    // and these tasks if un-executed would upset Valgrind.
    RunPendingMessages();
  }

  // Create a simple widget win. The caller is responsible for taking ownership
  // of the returned value.
  NativeWidgetWin* CreateNativeWidgetWin();

  void RunPendingMessages() {
    message_loop_.RunAllPending();
  }

 private:
  MessageLoopForUI message_loop_;

  DISALLOW_COPY_AND_ASSIGN(NativeWidgetWinTest);
};

NativeWidgetWin* NativeWidgetWinTest::CreateNativeWidgetWin() {
  scoped_ptr<Widget> widget(new Widget);
  Widget::InitParams params(Widget::InitParams::TYPE_WINDOW);
  params.ownership = views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  params.bounds = gfx::Rect(50, 50, 650, 650);
  widget->Init(params);
  return static_cast<NativeWidgetWin*>(widget.release()->native_widget());
}

TEST_F(NativeWidgetWinTest, ZoomWindow) {
  scoped_ptr<NativeWidgetWin> window(CreateNativeWidgetWin());
  window->ShowWindow(SW_HIDE);
  EXPECT_FALSE(window->IsActive());
  window->ShowWindow(SW_MAXIMIZE);
  EXPECT_TRUE(window->IsZoomed());
  window->CloseNow();
}

TEST_F(NativeWidgetWinTest, SetBoundsForZoomedWindow) {
  scoped_ptr<NativeWidgetWin> window(CreateNativeWidgetWin());
  window->ShowWindow(SW_MAXIMIZE);
  EXPECT_TRUE(window->IsZoomed());

  // Create another window, so that it will be active.
  scoped_ptr<NativeWidgetWin> window2(CreateNativeWidgetWin());
  window2->ShowWindow(SW_MAXIMIZE);
  EXPECT_TRUE(window2->IsActive());
  EXPECT_FALSE(window->IsActive());

  // Verify that setting the bounds of a zoomed window will unzoom it and not
  // cause it to be activated.
  window->SetBounds(gfx::Rect(50, 50, 650, 650));
  EXPECT_FALSE(window->IsZoomed());
  EXPECT_FALSE(window->IsActive());

  // Cleanup.
  window->CloseNow();
  window2->CloseNow();
}

}  // namespace
}  // namespace views
