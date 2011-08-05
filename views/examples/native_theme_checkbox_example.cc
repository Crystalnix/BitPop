// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views/examples/native_theme_checkbox_example.h"

#include "base/stringprintf.h"
#include "views/controls/button/checkbox.h"
#include "views/controls/button/radio_button.h"
#include "views/layout/fill_layout.h"

namespace examples {

NativeThemeCheckboxExample::NativeThemeCheckboxExample(ExamplesMain* main)
    : ExampleBase(main),
      count_(0) {
}

NativeThemeCheckboxExample::~NativeThemeCheckboxExample() {
}

std::wstring NativeThemeCheckboxExample::GetExampleTitle() {
  return L"CheckboxNt";
}

void NativeThemeCheckboxExample::CreateExampleView(views::View* container) {
  //button_ = new views::RadioButtonNt(L"RadioButtonNt", 3);
  button_ = new views::CheckboxNt(L"CheckboxNt");
  button_->set_listener(this);
  container->SetLayoutManager(new views::FillLayout);
  container->AddChildView(button_);
}

void NativeThemeCheckboxExample::ButtonPressed(views::Button* sender,
                                             const views::Event& event) {
  PrintStatus(base::StringPrintf(L"Pressed! count:%d", ++count_).c_str());
}

}  // namespace examples
