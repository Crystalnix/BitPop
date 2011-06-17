// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views/ime/input_method_base.h"
#include "views/view.h"
#include "views/widget/widget.h"

#include "base/logging.h"

namespace views {

InputMethodBase::InputMethodBase()
    : delegate_(NULL),
      widget_(NULL),
      focused_view_(NULL),
      widget_focused_(false) {
}

InputMethodBase::~InputMethodBase() {
  if (widget_) {
    widget_->GetFocusManager()->RemoveFocusChangeListener(this);
    widget_ = NULL;
  }
}

void InputMethodBase::set_delegate(internal::InputMethodDelegate* delegate) {
  DCHECK(delegate);
  delegate_ = delegate;
}

void InputMethodBase::Init(Widget* widget) {
  DCHECK(widget);
  DCHECK(widget->GetFocusManager());

  if (widget_) {
    NOTREACHED() << "The input method is already initialized.";
    return;
  }

  widget_ = widget;
  widget->GetFocusManager()->AddFocusChangeListener(this);
}

void InputMethodBase::OnFocus() {
  widget_focused_ = true;
}

void InputMethodBase::OnBlur() {
  widget_focused_ = false;
}

TextInputClient* InputMethodBase::GetTextInputClient() const {
  return (widget_focused_ && focused_view_) ?
      focused_view_->GetTextInputClient() : NULL;
}

ui::TextInputType InputMethodBase::GetTextInputType() const {
  TextInputClient* client = GetTextInputClient();
  return client ? client->GetTextInputType() : ui::TEXT_INPUT_TYPE_NONE;
}

void InputMethodBase::FocusWillChange(View* focused_before, View* focused) {
  DCHECK_EQ(focused_view_, focused_before);
  FocusedViewWillChange();
  focused_view_ = focused;
  FocusedViewDidChange();
}

bool InputMethodBase::IsViewFocused(View* view) const {
  return widget_focused_ && view && focused_view_ == view;
}

bool InputMethodBase::IsTextInputTypeNone() const {
  return GetTextInputType() == ui::TEXT_INPUT_TYPE_NONE;
}

void InputMethodBase::OnInputMethodChanged() const {
  TextInputClient* client = GetTextInputClient();
  if (client && client->GetTextInputType() != ui::TEXT_INPUT_TYPE_NONE)
    client->OnInputMethodChanged();
}

void InputMethodBase::DispatchKeyEventPostIME(const KeyEvent& key) const {
  if (delegate_)
    delegate_->DispatchKeyEventPostIME(key);
}

bool InputMethodBase::GetCaretBoundsInWidget(gfx::Rect* rect) const {
  DCHECK(rect);
  TextInputClient* client = GetTextInputClient();
  if (!client || client->GetTextInputType() == ui::TEXT_INPUT_TYPE_NONE)
    return false;

  *rect = client->GetCaretBounds();
  gfx::Point origin = rect->origin();
  gfx::Point end = gfx::Point(rect->right(), rect->bottom());

  View::ConvertPointToWidget(focused_view_, &origin);
  View::ConvertPointToWidget(focused_view_, &end);
  rect->SetRect(origin.x(), origin.y(),
                end.x() - origin.x(), end.y() - origin.y());

  // We need to do coordinate conversion if the focused view is inside a child
  // Widget.
  if (focused_view_->GetWidget() != widget_)
    return Widget::ConvertRect(focused_view_->GetWidget(), widget_, rect);
  return true;
}

void InputMethodBase::FocusedViewWillChange() {
}

void InputMethodBase::FocusedViewDidChange() {
}

}  // namespace views
