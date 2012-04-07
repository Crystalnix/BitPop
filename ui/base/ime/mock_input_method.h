// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_BASE_IME_MOCK_INPUT_METHOD_H_
#define UI_BASE_IME_MOCK_INPUT_METHOD_H_
#pragma once

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "ui/base/ime/input_method.h"
#include "ui/base/ui_export.h"

namespace ui {

class TextInputClient;

// A mock ui::InputMethod implementation for minimum input support.
class UI_EXPORT MockInputMethod : public InputMethod {
 public:
  explicit MockInputMethod(internal::InputMethodDelegate* delegate);
  virtual ~MockInputMethod();

  // Overriden from InputMethod.
  virtual void SetDelegate(internal::InputMethodDelegate* delegate) OVERRIDE;
  virtual void Init(bool focused) OVERRIDE;
  virtual void OnFocus() OVERRIDE;
  virtual void OnBlur() OVERRIDE;
  virtual void SetFocusedTextInputClient(TextInputClient* client) OVERRIDE;
  virtual TextInputClient* GetTextInputClient() const OVERRIDE;
  virtual void DispatchKeyEvent(const base::NativeEvent& native_event) OVERRIDE;
  virtual void OnTextInputTypeChanged(const TextInputClient* client) OVERRIDE;
  virtual void OnCaretBoundsChanged(const TextInputClient* client) OVERRIDE;
  virtual void CancelComposition(const TextInputClient* client) OVERRIDE;
  virtual std::string GetInputLocale() OVERRIDE;
  virtual base::i18n::TextDirection GetInputTextDirection() OVERRIDE;
  virtual bool IsActive() OVERRIDE;
  virtual ui::TextInputType GetTextInputType() const OVERRIDE;
  virtual bool CanComposeInline() const OVERRIDE;

 private:
  internal::InputMethodDelegate* delegate_;
  TextInputClient* text_input_client_;

  DISALLOW_COPY_AND_ASSIGN(MockInputMethod);
};

}  // namespace ui

#endif  // UI_BASE_IME_MOCK_INPUT_METHOD_H_
