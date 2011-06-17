// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views/controls/button/radio_button.h"

#include "base/logging.h"
#include "ui/base/accessibility/accessible_view_state.h"
#include "views/widget/root_view.h"

namespace views {

// static
const char RadioButton::kViewClassName[] = "views/RadioButton";

////////////////////////////////////////////////////////////////////////////////
// RadioButton, public:

RadioButton::RadioButton(const std::wstring& label, int group_id)
    : Checkbox(label) {
  SetGroup(group_id);
}

RadioButton::~RadioButton() {
}

////////////////////////////////////////////////////////////////////////////////
// RadioButton, Checkbox overrides:

void RadioButton::SetChecked(bool checked) {
  if (checked == RadioButton::checked())
    return;
  if (native_wrapper_ &&
      !native_wrapper_->UsesNativeRadioButtonGroup() && checked) {
    // We can't just get the root view here because sometimes the radio
    // button isn't attached to a root view (e.g., if it's part of a tab page
    // that is currently not active).
    View* container = parent();
    while (container && container->parent())
      container = container->parent();
    if (container) {
      std::vector<View*> other;
      container->GetViewsWithGroup(GetGroup(), &other);
      std::vector<View*>::iterator i;
      for (i = other.begin(); i != other.end(); ++i) {
        if (*i != this) {
          if ((*i)->GetClassName() != kViewClassName) {
            NOTREACHED() << "radio-button has same group as other non "
                            "radio-button views.";
            continue;
          }
          RadioButton* peer = static_cast<RadioButton*>(*i);
          peer->SetChecked(false);
        }
      }
    }
  }
  Checkbox::SetChecked(checked);
}

////////////////////////////////////////////////////////////////////////////////
// RadioButton, View overrides:

void RadioButton::GetAccessibleState(ui::AccessibleViewState* state) {
  Checkbox::GetAccessibleState(state);
  state->role = ui::AccessibilityTypes::ROLE_RADIOBUTTON;
}

View* RadioButton::GetSelectedViewForGroup(int group_id) {
  std::vector<View*> views;
  GetRootView()->GetViewsWithGroup(group_id, &views);
  if (views.empty())
    return NULL;

  for (std::vector<View*>::const_iterator iter = views.begin();
       iter != views.end(); ++iter) {
    RadioButton* radio_button = static_cast<RadioButton*>(*iter);
    if (radio_button->checked())
      return radio_button;
  }
  return NULL;
}

bool RadioButton::IsGroupFocusTraversable() const {
  // When focusing a radio button with tab/shift+tab, only the selected button
  // from the group should be focused.
  return false;
}

void RadioButton::OnMouseReleased(const MouseEvent& event) {
  // Set the checked state to true only if we are unchecked, since we can't
  // be toggled on and off like a checkbox.
  if (!checked() && HitTestLabel(event))
    SetChecked(true);

  OnMouseCaptureLost();
}

void RadioButton::OnMouseCaptureLost() {
  native_wrapper_->SetPushed(false);
  ButtonPressed();
}

std::string RadioButton::GetClassName() const {
  return kViewClassName;
}

////////////////////////////////////////////////////////////////////////////////
// RadioButton, NativeButton overrides:

NativeButtonWrapper* RadioButton::CreateWrapper() {
  NativeButtonWrapper* native_wrapper =
      NativeButtonWrapper::CreateRadioButtonWrapper(this);
  native_wrapper->UpdateLabel();
  native_wrapper->UpdateChecked();
  return native_wrapper;
}

}  // namespace views
