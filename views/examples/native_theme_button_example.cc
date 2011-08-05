// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views/examples/native_theme_button_example.h"

#include "base/logging.h"
#include "base/stringprintf.h"
#include "base/utf_string_conversions.h"
#include "ui/base/animation/throb_animation.h"
#include "ui/base/models/combobox_model.h"
#include "ui/gfx/canvas.h"
#include "views/controls/label.h"
#include "views/native_theme_painter.h"
#include "views/layout/grid_layout.h"

namespace {

class ExampleComboboxModel : public ui::ComboboxModel {
 public:
  ExampleComboboxModel(const wchar_t** strings, int count)
      : strings_(strings), count_(count) {
  }

  ~ExampleComboboxModel() {
  }

  void set_data(const wchar_t** strings, int count) {
    strings_ = strings;
    count_ = count;
  }

  // Overridden from ui::ComboboxModel:
  virtual int GetItemCount() OVERRIDE {
    return count_;
  }
  virtual string16 GetItemAt(int index) OVERRIDE {
    return WideToUTF16Hack(strings_[index]);
  }

 private:
  const wchar_t** strings_;
  int count_;

  DISALLOW_COPY_AND_ASSIGN(ExampleComboboxModel);
};

const wchar_t* kParts[] = {
    L"PushButton",
    L"RadioButton",
    L"Checkbox",
};

const wchar_t* kStates[] = {
    L"Disabled",
    L"Normal",
    L"Hot",
    L"Pressed",
    L"<Dynamic>",
};

}  // anonymous namespace

namespace examples {

ExampleNativeThemeButton::ExampleNativeThemeButton(
    views::ButtonListener* listener,
    views::Combobox* cb_part,
    views::Combobox* cb_state)
    : CustomButton(listener),
      cb_part_(cb_part),
      cb_state_(cb_state),
      count_(0),
      is_checked_(false),
      is_indeterminate_(false) {
  cb_part_->set_listener(this);
  cb_state_->set_listener(this);

  painter_.reset(new views::NativeThemePainter(this));
  set_background(views::Background::CreateBackgroundPainter(
      false, painter_.get()));
}

ExampleNativeThemeButton::~ExampleNativeThemeButton() {
}

std::wstring ExampleNativeThemeButton::MessWithState() {
  const wchar_t* message = NULL;
  switch(GetThemePart()) {
  case gfx::NativeTheme::kPushButton:
    message = L"Pressed! count:%d";
    break;
  case gfx::NativeTheme::kRadio:
    is_checked_ = !is_checked_;
    message = is_checked_ ? L"Checked! count:%d" : L"Unchecked! count:%d";
    break;
  case gfx::NativeTheme::kCheckbox:
    if (is_indeterminate_) {
      is_checked_ = false;
      is_indeterminate_ = false;
    } else if (!is_checked_) {
      is_checked_ = true;
    } else {
      is_checked_ = false;
      is_indeterminate_ = true;
    }

    message = is_checked_ ? L"Checked! count:%d" :
      is_indeterminate_ ? L"Indeterminate! count:%d" : L"Unchecked! count:%d";
    break;
  default:
    DCHECK(false);
  }

  return base::StringPrintf(message, ++count_);
}

void ExampleNativeThemeButton::ItemChanged(views::Combobox* combo_box,
                                           int prev_index,
                                           int new_index) {
  SchedulePaint();
}

gfx::NativeTheme::Part ExampleNativeThemeButton::GetThemePart() const {
  int selected = cb_part_->selected_item();
  switch(selected) {
    case 0:
      return gfx::NativeTheme::kPushButton;
    case 1:
      return gfx::NativeTheme::kRadio;
    case 2:
      return gfx::NativeTheme::kCheckbox;
    default:
      DCHECK(false);
  }
  return gfx::NativeTheme::kPushButton;
}

gfx::Rect ExampleNativeThemeButton::GetThemePaintRect() const {
  return bounds();
}

gfx::NativeTheme::State ExampleNativeThemeButton::GetThemeState(
    gfx::NativeTheme::ExtraParams* params) const {
  GetExtraParams(params);

  int selected = cb_state_->selected_item();
  if (selected > 3) {
    switch(state()) {
      case BS_DISABLED:
        return gfx::NativeTheme::kDisabled;
      case BS_NORMAL:
        return gfx::NativeTheme::kNormal;
      case BS_HOT:
        return gfx::NativeTheme::kHovered;
      case BS_PUSHED:
        return gfx::NativeTheme::kPressed;
      default:
        DCHECK(false);
    }
  }

  switch(selected) {
    case 0:
      return gfx::NativeTheme::kDisabled;
    case 1:
      return gfx::NativeTheme::kNormal;
    case 2:
      return gfx::NativeTheme::kHovered;
    case 3:
      return gfx::NativeTheme::kPressed;
    default:
      DCHECK(false);
  }
  return gfx::NativeTheme::kNormal;
}

void ExampleNativeThemeButton::GetExtraParams(
    gfx::NativeTheme::ExtraParams* params) const {

  params->button.checked = is_checked_;
  params->button.indeterminate = is_indeterminate_;
  params->button.is_default = false;
  params->button.has_border = false;
  params->button.classic_state = 0;
  params->button.background_color = SkColorSetARGB(0, 0, 0, 0);
}

const ui::Animation* ExampleNativeThemeButton::GetThemeAnimation() const {
  int selected = cb_state_->selected_item();
  return selected <= 3 ? NULL : hover_animation_.get();
}

gfx::NativeTheme::State ExampleNativeThemeButton::GetBackgroundThemeState(
    gfx::NativeTheme::ExtraParams* params) const {
  GetExtraParams(params);
  return gfx::NativeTheme::kNormal;
}

gfx::NativeTheme::State ExampleNativeThemeButton::GetForegroundThemeState(
    gfx::NativeTheme::ExtraParams* params) const {
  GetExtraParams(params);
  return gfx::NativeTheme::kHovered;
}

gfx::Size ExampleNativeThemeButton::GetPreferredSize() {
  return painter_.get() == NULL ? gfx::Size() : painter_->GetPreferredSize();
}

void ExampleNativeThemeButton::OnPaintBackground(gfx::Canvas* canvas) {
  // Fill the background with a known colour so that we know where the bounds
  // of the View are.
  canvas->FillRectInt(SkColorSetRGB(255, 128, 128), 0, 0, width(), height());
  CustomButton::OnPaintBackground(canvas);
}

////////////////////////////////////////////////////////////////////////////////

NativeThemeButtonExample::NativeThemeButtonExample(ExamplesMain* main)
    : ExampleBase(main) {
}

NativeThemeButtonExample::~NativeThemeButtonExample() {
}

std::wstring NativeThemeButtonExample::GetExampleTitle() {
  return L"Native Theme Button";
}

void NativeThemeButtonExample::CreateExampleView(views::View* container) {
  views::GridLayout* layout = new views::GridLayout(container);
  container->SetLayoutManager(layout);

  layout->AddPaddingRow(0, 8);

  views::ColumnSet* column_set = layout->AddColumnSet(0);
  column_set->AddPaddingColumn(0, 8);
  column_set->AddColumn(views::GridLayout::LEADING, views::GridLayout::FILL,
                        0.1f, views::GridLayout::USE_PREF, 0, 0);
  column_set->AddColumn(views::GridLayout::FILL, views::GridLayout::FILL,
                        0.9f, views::GridLayout::USE_PREF, 0, 0);
  column_set->AddPaddingColumn(0, 8);

  layout->StartRow(0, 0);
  layout->AddView(new views::Label(L"Part:"));
  views::Combobox* cb_part = new views::Combobox(
      new ExampleComboboxModel(kParts, arraysize(kParts)));
  cb_part->SetSelectedItem(0);
  layout->AddView(cb_part);

  layout->StartRow(0, 0);
  layout->AddView(new views::Label(L"State:"));
  views::Combobox* cb_state = new views::Combobox(
      new ExampleComboboxModel(kStates, arraysize(kStates)));
  cb_state->SetSelectedItem(0);
  layout->AddView(cb_state);

  layout->AddPaddingRow(0, 32);

  button_ = new ExampleNativeThemeButton(this, cb_part, cb_state);

  column_set = layout->AddColumnSet(1);
  column_set->AddPaddingColumn(0, 16);
  column_set->AddColumn(views::GridLayout::FILL, views::GridLayout::FILL,
                        1, views::GridLayout::USE_PREF, 0, 0);
  column_set->AddPaddingColumn(0, 16);
  layout->StartRow(1, 1);
  layout->AddView(button_);

  layout->AddPaddingRow(0, 8);
}

void NativeThemeButtonExample::ButtonPressed(views::Button* sender,
                                             const views::Event& event) {
  PrintStatus(button_->MessWithState().c_str());
}

}  // namespace examples
