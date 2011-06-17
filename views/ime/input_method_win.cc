// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views/ime/input_method_win.h"

#include "base/basictypes.h"
#include "base/logging.h"
#include "base/string_util.h"
#include "ui/base/keycodes/keyboard_codes.h"
#include "views/events/event.h"

namespace views {

InputMethodWin::InputMethodWin(internal::InputMethodDelegate* delegate)
    : active_(false),
      direction_(base::i18n::UNKNOWN_DIRECTION),
      pending_requested_direction_(base::i18n::UNKNOWN_DIRECTION) {
  set_delegate(delegate);
}

InputMethodWin::~InputMethodWin() {
  if (widget())
    ime_input_.DisableIME(hwnd());
}

void InputMethodWin::Init(Widget* widget) {
  InputMethodBase::Init(widget);

  // Gets the initial input locale and text direction information.
  OnInputLangChange(0, 0);
}

void InputMethodWin::OnFocus() {
  DCHECK(!widget_focused());
  InputMethodBase::OnFocus();
  UpdateIMEState();
}

void InputMethodWin::OnBlur() {
  DCHECK(widget_focused());
  ConfirmCompositionText();
  InputMethodBase::OnBlur();
}

void InputMethodWin::DispatchKeyEvent(const KeyEvent& key) {
  // Handles ctrl-shift key to change text direction and layout alignment.
  if (ui::ImeInput::IsRTLKeyboardLayoutInstalled() && !IsTextInputTypeNone()) {
    ui::KeyboardCode code = key.key_code();
    if (key.type() == ui::ET_KEY_PRESSED) {
      if (code == ui::VKEY_SHIFT) {
        base::i18n::TextDirection dir;
        if (ui::ImeInput::IsCtrlShiftPressed(&dir))
          pending_requested_direction_ = dir;
      } else if (code != ui::VKEY_CONTROL) {
        pending_requested_direction_ = base::i18n::UNKNOWN_DIRECTION;
      }
    } else if (key.type() == ui::ET_KEY_RELEASED &&
               (code == ui::VKEY_SHIFT || code == ui::VKEY_CONTROL) &&
               pending_requested_direction_ != base::i18n::UNKNOWN_DIRECTION) {
      GetTextInputClient()->ChangeTextDirectionAndLayoutAlignment(
          pending_requested_direction_);
      pending_requested_direction_ = base::i18n::UNKNOWN_DIRECTION;
    }
  }

  DispatchKeyEventPostIME(key);
}

void InputMethodWin::OnTextInputTypeChanged(View* view) {
  if (IsViewFocused(view)) {
    ime_input_.CancelIME(hwnd());
    UpdateIMEState();
  }
}

void InputMethodWin::OnCaretBoundsChanged(View* view) {
  gfx::Rect rect;
  if (!IsViewFocused(view) || !GetCaretBoundsInWidget(&rect))
    return;
  ime_input_.UpdateCaretRect(hwnd(), rect);
}

void InputMethodWin::CancelComposition(View* view) {
  if (IsViewFocused(view))
    ime_input_.CancelIME(hwnd());
}

std::string InputMethodWin::GetInputLocale() {
  return locale_;
}

base::i18n::TextDirection InputMethodWin::GetInputTextDirection() {
  return direction_;
}

bool InputMethodWin::IsActive() {
  return active_;
}

void InputMethodWin::FocusedViewWillChange() {
  ConfirmCompositionText();
}

void InputMethodWin::FocusedViewDidChange() {
  UpdateIMEState();
}

void InputMethodWin::OnInputLangChange(DWORD character_set,
                                       HKL input_language_id) {
  active_ = ime_input_.SetInputLanguage();
  locale_ = ime_input_.GetInputLanguageName();
  direction_ = ime_input_.GetTextDirection();
  OnInputMethodChanged();
}

LRESULT InputMethodWin::OnImeSetContext(
    UINT message, WPARAM wparam, LPARAM lparam, BOOL* handled) {
  active_ = (wparam == TRUE);
  if (active_)
    ime_input_.CreateImeWindow(hwnd());

  OnInputMethodChanged();
  return ime_input_.SetImeWindowStyle(hwnd(), message, wparam, lparam, handled);
}

LRESULT InputMethodWin::OnImeStartComposition(
    UINT message, WPARAM wparam, LPARAM lparam, BOOL* handled) {
  // We have to prevent WTL from calling ::DefWindowProc() because the function
  // calls ::ImmSetCompositionWindow() and ::ImmSetCandidateWindow() to
  // over-write the position of IME windows.
  *handled = TRUE;

  if (IsTextInputTypeNone())
    return 0;

  // Reset the composition status and create IME windows.
  ime_input_.CreateImeWindow(hwnd());
  ime_input_.ResetComposition(hwnd());
  return 0;
}

LRESULT InputMethodWin::OnImeComposition(
    UINT message, WPARAM wparam, LPARAM lparam, BOOL* handled) {
  // We have to prevent WTL from calling ::DefWindowProc() because we do not
  // want for the IMM (Input Method Manager) to send WM_IME_CHAR messages.
  *handled = TRUE;

  if (IsTextInputTypeNone())
    return 0;

  // At first, update the position of the IME window.
  ime_input_.UpdateImeWindow(hwnd());

  // Retrieve the result string and its attributes of the ongoing composition
  // and send it to a renderer process.
  ui::CompositionText composition;
  if (ime_input_.GetResult(hwnd(), lparam, &composition.text)) {
    GetTextInputClient()->InsertText(composition.text);
    ime_input_.ResetComposition(hwnd());
    // Fall though and try reading the composition string.
    // Japanese IMEs send a message containing both GCS_RESULTSTR and
    // GCS_COMPSTR, which means an ongoing composition has been finished
    // by the start of another composition.
  }
  // Retrieve the composition string and its attributes of the ongoing
  // composition and send it to a renderer process.
  if (ime_input_.GetComposition(hwnd(), lparam, &composition))
    GetTextInputClient()->SetCompositionText(composition);

  return 0;
}

LRESULT InputMethodWin::OnImeEndComposition(
    UINT message, WPARAM wparam, LPARAM lparam, BOOL* handled) {
  // Let WTL call ::DefWindowProc() and release its resources.
  *handled = FALSE;

  if (IsTextInputTypeNone())
    return 0;

  if (GetTextInputClient()->HasCompositionText())
    GetTextInputClient()->ClearCompositionText();

  ime_input_.ResetComposition(hwnd());
  ime_input_.DestroyImeWindow(hwnd());
  return 0;
}

LRESULT InputMethodWin::OnChar(
    UINT message, WPARAM wparam, LPARAM lparam, BOOL* handled) {
  *handled = TRUE;

  // We need to send character events to the focused text input client event if
  // its text input type is ui::TEXT_INPUT_TYPE_NONE.
  if (!GetTextInputClient())
    return 0;

  int flags = 0;
  flags |= (::GetKeyState(VK_MENU) & 0x80)? ui::EF_ALT_DOWN : 0;
  flags |= (::GetKeyState(VK_SHIFT) & 0x80)? ui::EF_SHIFT_DOWN : 0;
  flags |= (::GetKeyState(VK_CONTROL) & 0x80)? ui::EF_CONTROL_DOWN : 0;
  GetTextInputClient()->InsertChar(static_cast<char16>(wparam), flags);
  return 0;
}

LRESULT InputMethodWin::OnDeadChar(
    UINT message, WPARAM wparam, LPARAM lparam, BOOL* handled) {
  *handled = TRUE;

  if (IsTextInputTypeNone())
    return 0;

  // Shows the dead character as a composition text, so that the user can know
  // what dead key was pressed.
  ui::CompositionText composition;
  composition.text.assign(1, static_cast<char16>(wparam));
  composition.selection = ui::Range(0, 1);
  composition.underlines.push_back(
      ui::CompositionUnderline(0, 1, SK_ColorBLACK, false));
  GetTextInputClient()->SetCompositionText(composition);
  return 0;
}

void InputMethodWin::ConfirmCompositionText() {
  if (!IsTextInputTypeNone()) {
    ime_input_.CleanupComposition(hwnd());
    // Though above line should confirm the client's composition text by sending
    // a result text to us, in case the input method and the client are in
    // inconsistent states, we check the client's composition state again.
    if (GetTextInputClient()->HasCompositionText())
      GetTextInputClient()->ConfirmCompositionText();
  }
}

void InputMethodWin::UpdateIMEState() {
  // Use switch here in case we are going to add more text input types.
  // We disable input method in password field.
  switch (GetTextInputType()) {
    case ui::TEXT_INPUT_TYPE_NONE:
    case ui::TEXT_INPUT_TYPE_PASSWORD:
      ime_input_.DisableIME(hwnd());
      break;
    default:
      ime_input_.EnableIME(hwnd());
      break;
  }
}

}  // namespace views
