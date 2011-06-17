// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VIEWS_IME_INPUT_METHOD_WIN_H_
#define VIEWS_IME_INPUT_METHOD_WIN_H_
#pragma once

#include <windows.h>

#include <string>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "ui/base/win/ime_input.h"
#include "views/ime/input_method_base.h"
#include "views/view.h"
#include "views/widget/widget.h"

namespace views {

// An InputMethod implementation based on Windows IMM32 API.
class InputMethodWin : public InputMethodBase {
 public:
  explicit InputMethodWin(internal::InputMethodDelegate* delegate);
  virtual ~InputMethodWin();

  // Overridden from InputMethod:
  virtual void Init(Widget* widget) OVERRIDE;
  virtual void OnFocus() OVERRIDE;
  virtual void OnBlur() OVERRIDE;
  virtual void DispatchKeyEvent(const KeyEvent& key) OVERRIDE;
  virtual void OnTextInputTypeChanged(View* view) OVERRIDE;
  virtual void OnCaretBoundsChanged(View* view) OVERRIDE;
  virtual void CancelComposition(View* view) OVERRIDE;
  virtual std::string GetInputLocale() OVERRIDE;
  virtual base::i18n::TextDirection GetInputTextDirection() OVERRIDE;
  virtual bool IsActive() OVERRIDE;

  // Message handlers. The native widget is responsible for forwarding following
  // messages to the input method.
  void OnInputLangChange(DWORD character_set, HKL input_language_id);
  LRESULT OnImeSetContext(
      UINT message, WPARAM wparam, LPARAM lparam, BOOL* handled);
  LRESULT OnImeStartComposition(
      UINT message, WPARAM wparam, LPARAM lparam, BOOL* handled);
  LRESULT OnImeComposition(
      UINT message, WPARAM wparam, LPARAM lparam, BOOL* handled);
  LRESULT OnImeEndComposition(
      UINT message, WPARAM wparam, LPARAM lparam, BOOL* handled);
  // For both WM_CHAR and WM_SYSCHAR
  LRESULT OnChar(
      UINT message, WPARAM wparam, LPARAM lparam, BOOL* handled);
  // For both WM_DEADCHAR and WM_SYSDEADCHAR
  LRESULT OnDeadChar(
      UINT message, WPARAM wparam, LPARAM lparam, BOOL* handled);

 private:
  // Overridden from InputMethodBase.
  virtual void FocusedViewWillChange() OVERRIDE;
  virtual void FocusedViewDidChange() OVERRIDE;

  // A helper function to return the Widget's native window.
  HWND hwnd() const { return widget()->GetNativeView(); }

  // Asks the client to confirm current composition text.
  void ConfirmCompositionText();

  // Enables or disables the IME according to the current text input type.
  void UpdateIMEState();

  // Indicates if the current input locale has an IME.
  bool active_;

  // Name of the current input locale.
  std::string locale_;

  // The current input text direction.
  base::i18n::TextDirection direction_;

  // The new text direction and layout alignment requested by the user by
  // pressing ctrl-shift. It'll be sent to the text input client when the key
  // is released.
  base::i18n::TextDirection pending_requested_direction_;

  // Windows IMM32 wrapper.
  // (See "ui/base/win/ime_input.h" for its details.)
  ui::ImeInput ime_input_;

  DISALLOW_COPY_AND_ASSIGN(InputMethodWin);
};

}  // namespace views

#endif  // VIEWS_IME_INPUT_METHOD_WIN_H_
