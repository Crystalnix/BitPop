// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/test/views_test_base.h"

#if defined(OS_WIN)
#include <ole2.h>
#endif

#if defined(USE_AURA)
#include "base/compiler_specific.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/root_window.h"
#include "ui/aura/test/test_activation_client.h"
#include "ui/base/ime/input_method.h"

namespace {

class DummyInputMethod : public ui::InputMethod {
 public:
  DummyInputMethod() {}
  virtual ~DummyInputMethod() {}

  // ui::InputMethod overrides:
  virtual void SetDelegate(
      ui::internal::InputMethodDelegate* delegate) OVERRIDE {}
  virtual void Init(bool focused) OVERRIDE {}
  virtual void OnFocus() OVERRIDE {}
  virtual void OnBlur() OVERRIDE {}
  virtual void SetFocusedTextInputClient(
      ui::TextInputClient* client) OVERRIDE {}
  virtual ui::TextInputClient* GetTextInputClient() const OVERRIDE {
    return NULL;
  }
  virtual void DispatchKeyEvent(
      const base::NativeEvent& native_key_event) OVERRIDE {}
  virtual void OnTextInputTypeChanged(
      const ui::TextInputClient* client) OVERRIDE {}
  virtual void OnCaretBoundsChanged(
      const ui::TextInputClient* client) OVERRIDE {}
  virtual void CancelComposition(const ui::TextInputClient* client) OVERRIDE {}
  virtual std::string GetInputLocale() OVERRIDE { return ""; }
  virtual base::i18n::TextDirection GetInputTextDirection() OVERRIDE {
    return base::i18n::UNKNOWN_DIRECTION;
  }
  virtual bool IsActive() OVERRIDE { return true; }
  virtual ui::TextInputType GetTextInputType() const OVERRIDE {
    return ui::TEXT_INPUT_TYPE_NONE;
  }
  virtual bool CanComposeInline() const OVERRIDE {
    return true;
  }
};

}  // namespace
#endif

namespace views {

ViewsTestBase::ViewsTestBase()
    : setup_called_(false),
      teardown_called_(false) {
#if defined(OS_WIN)
  OleInitialize(NULL);
#endif
#if defined(USE_AURA)
  test_activation_client_.reset(new aura::test::TestActivationClient);
  test_input_method_.reset(new DummyInputMethod);
  aura::RootWindow::GetInstance()->SetProperty(
      aura::client::kRootWindowInputMethod,
      test_input_method_.get());
#endif
}

ViewsTestBase::~ViewsTestBase() {
#if defined(OS_WIN)
  OleUninitialize();
#endif
  CHECK(setup_called_)
      << "You have overridden SetUp but never called super class's SetUp";
  CHECK(teardown_called_)
      << "You have overrideen TearDown but never called super class's TearDown";
}

void ViewsTestBase::SetUp() {
  testing::Test::SetUp();
  setup_called_ = true;
  if (!views_delegate_.get())
    views_delegate_.reset(new TestViewsDelegate());
}

void ViewsTestBase::TearDown() {
  // Flush the message loop because we have pending release tasks
  // and these tasks if un-executed would upset Valgrind.
  RunPendingMessages();
  teardown_called_ = true;
  views_delegate_.reset();
  testing::Test::TearDown();
}

void ViewsTestBase::RunPendingMessages() {
#if defined(USE_AURA)
  message_loop_.RunAllPendingWithDispatcher(
      aura::RootWindow::GetInstance()->GetDispatcher());
#else
  message_loop_.RunAllPending();
#endif
}

}  // namespace views
