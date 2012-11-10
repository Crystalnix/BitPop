// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_AURA_TEST_AURA_TEST_HELPER_H_
#define UI_AURA_TEST_AURA_TEST_HELPER_H_

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"

#if defined(OS_WIN)
#include "ui/base/win/scoped_ole_initializer.h"
#endif

class MessageLoopForUI;

namespace ui {
class InputMethod;
}

namespace aura {
class FocusManager;
class RootWindow;
namespace shared {
class RootWindowCaptureClient;
}
namespace test {
class TestActivationClient;
class TestStackingClient;

// A helper class owned by tests that does common initialization required for
// Aura use. This class creates a root window with clients and other objects
// that are necessary to run test on Aura.
class AuraTestHelper {
 public:
  explicit AuraTestHelper(MessageLoopForUI* message_loop);
  ~AuraTestHelper();

  // Creates and initializes (shows and sizes) the RootWindow for use in tests.
  void SetUp();

  // Clean up objects that are created for tests. This also delete
  // aura::Env object.
  void TearDown();

  // Flushes message loop.
  void RunAllPendingInMessageLoop();

  RootWindow* root_window() { return root_window_.get(); }

 private:
  MessageLoopForUI* message_loop_;
  bool setup_called_;
  bool teardown_called_;
  bool owns_root_window_;
  scoped_ptr<RootWindow> root_window_;
  scoped_ptr<TestStackingClient> stacking_client_;
  scoped_ptr<TestActivationClient> test_activation_client_;
  scoped_ptr<shared::RootWindowCaptureClient> root_window_capture_client_;
  scoped_ptr<ui::InputMethod> test_input_method_;
  scoped_ptr<FocusManager> focus_manager_;

#if defined(OS_WIN)
  ui::ScopedOleInitializer ole_initializer_;
#endif

  DISALLOW_COPY_AND_ASSIGN(AuraTestHelper);
};

}  // namespace test
}  // namespace aura

#endif  // UI_AURA_TEST_AURA_TEST_HELPER_H_
