// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/examples/button_example.h"

#include "base/utf_string_conversions.h"
#include "grit/ui_resources.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view.h"

namespace views {
namespace examples {

ButtonExample::ButtonExample()
    : ExampleBase("Text Button"),
      alignment_(TextButton::ALIGN_LEFT),
      use_native_theme_border_(false),
      icon_(NULL),
      count_(0) {
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  icon_ = rb.GetBitmapNamed(IDR_CLOSE_SA_H);
}

ButtonExample::~ButtonExample() {
}

void ButtonExample::CreateExampleView(View* container) {
  TextButton* tb = new TextButton(this, ASCIIToUTF16("Button"));
  button_ = tb;
  container->SetLayoutManager(new FillLayout);
  container->AddChildView(button_);
}

void ButtonExample::ButtonPressed(Button* sender, const Event& event) {
  PrintStatus("Pressed! count: %d", ++count_);

  if (event.IsControlDown()) {
    if (event.IsShiftDown()) {
      if (event.IsAltDown()) {
        button_->SetMultiLine(!button_->multi_line());
        if (button_->multi_line()) {
          button_->SetText(ASCIIToUTF16("Multi-line text\n") +
                           ASCIIToUTF16("is here to stay all the way!\n") +
                           ASCIIToUTF16("123"));
        } else {
          button_->SetText(ASCIIToUTF16("Button"));
        }
      } else {
        switch(button_->icon_placement()) {
          case TextButton::ICON_ON_LEFT:
            button_->set_icon_placement(TextButton::ICON_ON_RIGHT);
            break;
          case TextButton::ICON_ON_RIGHT:
            button_->set_icon_placement(TextButton::ICON_ON_LEFT);
            break;
        }
      }
    } else if (event.IsAltDown()) {
      if (button_->HasIcon())
        button_->SetIcon(SkBitmap());
      else
        button_->SetIcon(*icon_);
    } else {
      switch(alignment_) {
        case TextButton::ALIGN_LEFT:
          alignment_ = TextButton::ALIGN_CENTER;
          break;
        case TextButton::ALIGN_CENTER:
          alignment_ = TextButton::ALIGN_RIGHT;
          break;
        case TextButton::ALIGN_RIGHT:
          alignment_ = TextButton::ALIGN_LEFT;
          break;
      }
      button_->set_alignment(alignment_);
    }
  } else if (event.IsShiftDown()) {
    if (event.IsAltDown()) {
      if (button_->text().length() < 10) {
        button_->SetText(
            ASCIIToUTF16("Startof") +
            ASCIIToUTF16("ReallyReallyReallyReallyReallyReallyReally") +
            ASCIIToUTF16("ReallyReallyReallyReallyReallyReallyReally") +
            ASCIIToUTF16("ReallyReallyReallyReallyReallyReallyReally") +
            ASCIIToUTF16("LongButtonText"));
      } else {
        button_->SetText(ASCIIToUTF16("Button"));
      }
    } else {
      use_native_theme_border_ = !use_native_theme_border_;
      if (use_native_theme_border_)
        button_->set_border(new TextButtonNativeThemeBorder(button_));
      else
        button_->set_border(new TextButtonBorder());
    }
  } else if (event.IsAltDown()) {
    button_->SetIsDefault(!button_->is_default());
  }
}

}  // namespace examples
}  // namespace views
