// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views/examples/link_example.h"

#include "views/controls/link.h"
#include "views/layout/fill_layout.h"
#include "views/view.h"

namespace examples {

LinkExample::LinkExample(ExamplesMain* main) : ExampleBase(main) {
}

LinkExample::~LinkExample() {
}

std::wstring LinkExample::GetExampleTitle() {
  return L"Link";
}

void LinkExample::CreateExampleView(views::View* container) {
  link_ = new views::Link(L"Click me!");
  link_->set_listener(this);

  container->SetLayoutManager(new views::FillLayout);
  container->AddChildView(link_);
}

void LinkExample::LinkClicked(views::Link* source, int event_flags) {
  PrintStatus(L"Link clicked");
}

}  // namespace examples
