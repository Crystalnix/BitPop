// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/controls/combobox/native_combobox_views.h"

#include <algorithm>

#include "base/command_line.h"
#include "base/utf_string_conversions.h"
#include "grit/ui_resources.h"
#include "ui/base/keycodes/keyboard_codes.h"
#include "ui/base/models/combobox_model.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/canvas_skia.h"
#include "ui/gfx/font.h"
#include "ui/gfx/path.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/focusable_border.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/menu/submenu_view.h"
#include "ui/views/widget/root_view.h"
#include "ui/views/widget/widget.h"

namespace {

// Define the size of the insets.
const int kTopInsetSize = 4;
const int kLeftInsetSize = 4;
const int kBottomInsetSize = 4;
const int kRightInsetSize = 4;

// Limit how small a combobox can be.
const int kMinComboboxWidth = 148;

// Size of the combobox arrow margins
const int kDisclosureArrowLeftPadding = 7;
const int kDisclosureArrowRightPadding = 7;

// Color settings for text and border.
// These are tentative, and should be derived from theme, system
// settings and current settings.
const SkColor kTextColor = SK_ColorBLACK;

// Define the id of the first item in the menu (since it needs to be > 0)
const int kFirstMenuItemId = 1000;

}  // namespace

namespace views {

const char NativeComboboxViews::kViewClassName[] =
    "views/NativeComboboxViews";

NativeComboboxViews::NativeComboboxViews(Combobox* parent)
    : combobox_(parent),
      text_border_(new FocusableBorder()),
      disclosure_arrow_(ResourceBundle::GetSharedInstance().GetBitmapNamed(
          IDR_DISCLOSURE_ARROW)),
      dropdown_open_(false),
      selected_item_(-1),
      content_width_(0),
      content_height_(0) {
  set_border(text_border_);
}

NativeComboboxViews::~NativeComboboxViews() {
}

////////////////////////////////////////////////////////////////////////////////
// NativeComboboxViews, View overrides:

bool NativeComboboxViews::OnMousePressed(const views::MouseEvent& mouse_event) {
  combobox_->RequestFocus();
  if (mouse_event.IsLeftMouseButton()) {
    UpdateFromModel();
    ShowDropDownMenu();
  }

  return true;
}

bool NativeComboboxViews::OnMouseDragged(const views::MouseEvent& mouse_event) {
  return true;
}

bool NativeComboboxViews::OnKeyPressed(const views::KeyEvent& key_event) {
  // TODO(oshima): handle IME.
  DCHECK(key_event.type() == ui::ET_KEY_PRESSED);

  // Check if we are in the default state (-1) and set to first item.
  if(selected_item_ == -1)
    selected_item_ = 0;

  int new_item = selected_item_;
  switch(key_event.key_code()){

    // move to the next element if any
    case ui::VKEY_DOWN:
      if (new_item < (combobox_->model()->GetItemCount() - 1))
        new_item++;
      break;

    // move to the end of the list
    case ui::VKEY_END:
    case ui::VKEY_NEXT:
      new_item = combobox_->model()->GetItemCount() - 1;
      break;

    // move to the top of the list
   case ui::VKEY_HOME:
   case ui::VKEY_PRIOR:
      new_item = 0;
      break;

    // move to the previous element if any
    case ui::VKEY_UP:
      if (new_item > 0)
        new_item--;
      break;

    default:
      return false;

  }

  if(new_item != selected_item_) {
    selected_item_ = new_item;
    combobox_->SelectionChanged();
    SchedulePaint();
  }

  return true;
}

bool NativeComboboxViews::OnKeyReleased(const views::KeyEvent& key_event) {
  return true;
}

void NativeComboboxViews::OnPaint(gfx::Canvas* canvas) {
  text_border_->set_has_focus(combobox_->HasFocus());
  OnPaintBackground(canvas);
  PaintText(canvas);
  OnPaintBorder(canvas);
}

void NativeComboboxViews::OnFocus() {
  NOTREACHED();
}

void NativeComboboxViews::OnBlur() {
  NOTREACHED();
}

/////////////////////////////////////////////////////////////////
// NativeComboboxViews, NativeComboboxWrapper overrides:

void NativeComboboxViews::UpdateFromModel() {
  int max_width = 0;
  const gfx::Font &font = GetFont();

  MenuItemView* menu = new MenuItemView(this);
  // MenuRunner owns |menu|.
  dropdown_list_menu_runner_.reset(new MenuRunner(menu));

  int num_items = combobox_->model()->GetItemCount();
  for (int i = 0; i < num_items; ++i) {
    string16 text = combobox_->model()->GetItemAt(i);

    // Inserting the Unicode formatting characters if necessary so that the
    // text is displayed correctly in right-to-left UIs.
    base::i18n::AdjustStringForLocaleDirection(&text);

    menu->AppendMenuItem(i + kFirstMenuItemId, text, MenuItemView::NORMAL);
    max_width = std::max(max_width, font.GetStringWidth(text));
  }

  content_width_ = max_width;
  content_height_ = font.GetFontSize();
}

void NativeComboboxViews::UpdateSelectedItem() {
  selected_item_ = combobox_->selected_item();
}

void NativeComboboxViews::UpdateEnabled() {
  SetEnabled(combobox_->enabled());
}

int NativeComboboxViews::GetSelectedItem() const {
  return selected_item_;
}

bool NativeComboboxViews::IsDropdownOpen() const {
  return dropdown_open_;
}

gfx::Size NativeComboboxViews::GetPreferredSize() {
  if (content_width_ == 0)
    UpdateFromModel();

  // The preferred size will drive the local bounds which in turn is used to set
  // the minimum width for the dropdown list.
  gfx::Insets insets = GetInsets();
  int total_width = content_width_ + (2 * insets.width())
      + kDisclosureArrowLeftPadding + disclosure_arrow_->width()
      + kDisclosureArrowRightPadding;
  return gfx::Size(std::min(kMinComboboxWidth, total_width),
                   content_height_ + (2 * insets.height()));
}

View* NativeComboboxViews::GetView() {
  return this;
}

void NativeComboboxViews::SetFocus() {
  text_border_->set_has_focus(true);
}

bool NativeComboboxViews::HandleKeyPressed(const KeyEvent& e) {
  return OnKeyPressed(e);
}

bool NativeComboboxViews::HandleKeyReleased(const KeyEvent& e) {
  return true;
}

void NativeComboboxViews::HandleFocus() {
  SchedulePaint();
}

void NativeComboboxViews::HandleBlur() {
}

gfx::NativeView NativeComboboxViews::GetTestingHandle() const {
  NOTREACHED();
  return NULL;
}

/////////////////////////////////////////////////////////////////
// NativeComboboxViews, views::MenuDelegate overrides:
// (note that the id received is offset by kFirstMenuItemId)

bool NativeComboboxViews::IsItemChecked(int id) const {
  return false;
}

bool NativeComboboxViews::IsCommandEnabled(int id) const {
  return true;
}

void NativeComboboxViews::ExecuteCommand(int id) {
  // revert menu offset to map back to combobox model
  id -= kFirstMenuItemId;
  DCHECK_LT(id, combobox_->model()->GetItemCount());
  selected_item_ = id;
  combobox_->SelectionChanged();
  SchedulePaint();
}

bool NativeComboboxViews::GetAccelerator(int id, ui::Accelerator* accel) {
  return false;
}

/////////////////////////////////////////////////////////////////
// NativeComboboxViews private methods:

const gfx::Font& NativeComboboxViews::GetFont() const {
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  return rb.GetFont(ResourceBundle::BaseFont);
}

void NativeComboboxViews::AdjustBoundsForRTLUI(gfx::Rect* rect) const {
  rect->set_x(GetMirroredXForRect(*rect));
}

void NativeComboboxViews::PaintText(gfx::Canvas* canvas) {
  gfx::Insets insets = GetInsets();

  canvas->Save();
  canvas->ClipRect(GetContentsBounds());

  int x = insets.left();
  int y = insets.top();
  int text_height = height() - insets.height();
  SkColor text_color = kTextColor;

  int index = GetSelectedItem();
  if (index < 0 || index > combobox_->model()->GetItemCount())
    index = 0;
  string16 text = combobox_->model()->GetItemAt(index);

  int disclosure_arrow_offset = width() - disclosure_arrow_->width()
      - kDisclosureArrowLeftPadding - kDisclosureArrowRightPadding;

  const gfx::Font& font = GetFont();
  int text_width = font.GetStringWidth(text);
  if ((text_width + insets.width()) > disclosure_arrow_offset)
    text_width = disclosure_arrow_offset - insets.width();

  gfx::Rect text_bounds(x, y, text_width, text_height);
  AdjustBoundsForRTLUI(&text_bounds);
  canvas->DrawStringInt(text, font, text_color, text_bounds);

  gfx::Rect arrow_bounds(disclosure_arrow_offset + kDisclosureArrowLeftPadding,
                         height() / 2 - disclosure_arrow_->height() / 2,
                         disclosure_arrow_->width(),
                         disclosure_arrow_->height());
  AdjustBoundsForRTLUI(&arrow_bounds);
  canvas->DrawBitmapInt(*disclosure_arrow_, arrow_bounds.x(), arrow_bounds.y());

  canvas->Restore();
}

void NativeComboboxViews::ShowDropDownMenu() {

  if (!dropdown_list_menu_runner_.get())
    UpdateFromModel();

  // Extend the menu to the width of the combobox.
  MenuItemView* menu = dropdown_list_menu_runner_->GetMenu();
  SubmenuView* submenu = menu->CreateSubmenu();
  submenu->set_minimum_preferred_width(size().width());

#if defined(USE_AURA)
  // Aura style is to have the menu over the bounds. Below bounds is default.
  menu->set_menu_position(views::MenuItemView::POSITION_OVER_BOUNDS);
#endif

  gfx::Rect lb = GetLocalBounds();
  gfx::Point menu_position(lb.origin());
  View::ConvertPointToScreen(this, &menu_position);
  if (menu_position.x() < 0)
      menu_position.set_x(0);

  gfx::Rect bounds(menu_position, lb.size());

  dropdown_open_ = true;
  if (dropdown_list_menu_runner_->RunMenuAt(
          GetWidget(), NULL, bounds, MenuItemView::TOPLEFT,
          MenuRunner::HAS_MNEMONICS) == MenuRunner::MENU_DELETED)
    return;
  dropdown_open_ = false;

  // Need to explicitly clear mouse handler so that events get sent
  // properly after the menu finishes running. If we don't do this, then
  // the first click to other parts of the UI is eaten.
  SetMouseHandler(NULL);
}

////////////////////////////////////////////////////////////////////////////////
// NativeComboboxWrapper, public:

#if defined(USE_AURA)
// static
NativeComboboxWrapper* NativeComboboxWrapper::CreateWrapper(
    Combobox* combobox) {
  return new NativeComboboxViews(combobox);
}
#endif

}  // namespace views
