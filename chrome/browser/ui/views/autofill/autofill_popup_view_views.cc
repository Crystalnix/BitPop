// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/autofill/autofill_popup_view_views.h"

#include "chrome/browser/ui/autofill/autofill_popup_controller.h"
#include "grit/ui_resources.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebAutofillClient.h"
#include "ui/base/keycodes/keyboard_codes.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/display.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/point.h"
#include "ui/gfx/rect.h"
#include "ui/gfx/screen.h"
#include "ui/views/border.h"
#include "ui/views/widget/widget.h"

using WebKit::WebAutofillClient;

namespace {

const SkColor kBorderColor = SkColorSetARGB(0xFF, 0xC7, 0xCA, 0xCE);
const SkColor kHoveredBackgroundColor = SkColorSetARGB(0xFF, 0xCD, 0xCD, 0xCD);
const SkColor kLabelTextColor = SkColorSetARGB(0xFF, 0x7F, 0x7F, 0x7F);
const SkColor kPopupBackground = SkColorSetARGB(0xFF, 0xFF, 0xFF, 0xFF);
const SkColor kValueTextColor = SkColorSetARGB(0xFF, 0x00, 0x00, 0x00);

}  // namespace

AutofillPopupViewViews::AutofillPopupViewViews(
    AutofillPopupController* controller)
    : controller_(controller),
      observing_widget_(NULL) {}

AutofillPopupViewViews::~AutofillPopupViewViews() {
  if (observing_widget_)
    observing_widget_->RemoveObserver(this);

  controller_->ViewDestroyed();
}

void AutofillPopupViewViews::Hide() {
  if (GetWidget()) {
    // This deletes |this|.
    GetWidget()->Close();
  } else {
    delete this;
  }
}

void AutofillPopupViewViews::OnPaint(gfx::Canvas* canvas) {
  canvas->DrawColor(kPopupBackground);
  OnPaintBorder(canvas);

  for (size_t i = 0; i < controller_->autofill_values().size(); ++i) {
    gfx::Rect line_rect = controller_->GetRectForRow(i, width());

    if (controller_->autofill_unique_ids()[i] ==
            WebAutofillClient::MenuItemIDSeparator) {
      canvas->DrawRect(line_rect, kLabelTextColor);
    } else {
      DrawAutofillEntry(canvas, i, line_rect);
    }
  }
}

void AutofillPopupViewViews::OnMouseCaptureLost() {
  controller_->ClearSelectedLine();
}

bool AutofillPopupViewViews::OnMouseDragged(const ui::MouseEvent& event) {
  if (HitTestPoint(gfx::Point(event.x(), event.y()))) {
    controller_->SetSelectedPosition(event.x(), event.y());

    // We must return true in order to get future OnMouseDragged and
    // OnMouseReleased events.
    return true;
  }

  // If we move off of the popup, we lose the selection.
  controller_->ClearSelectedLine();
  return false;
}

void AutofillPopupViewViews::OnMouseExited(const ui::MouseEvent& event) {
  controller_->ClearSelectedLine();
}

void AutofillPopupViewViews::OnMouseMoved(const ui::MouseEvent& event) {
  controller_->SetSelectedPosition(event.x(), event.y());
}

bool AutofillPopupViewViews::OnMousePressed(const ui::MouseEvent& event) {
  // We must return true in order to get the OnMouseReleased event later.
  return true;
}

void AutofillPopupViewViews::OnMouseReleased(const ui::MouseEvent& event) {
  // We only care about the left click.
  if (event.IsOnlyLeftMouseButton() &&
      HitTestPoint(gfx::Point(event.x(), event.y())))
    controller_->AcceptSelectedPosition(event.x(), event.y());
}

void AutofillPopupViewViews::OnWidgetBoundsChanged(
    views::Widget* widget,
    const gfx::Rect& new_bounds) {
  Hide();
}

void AutofillPopupViewViews::Show() {
  if (!GetWidget()) {
    // The widget is destroyed by the corresponding NativeWidget, so we use
    // a weak pointer to hold the reference and don't have to worry about
    // deletion.
    views::Widget* widget = new views::Widget;
    views::Widget::InitParams params(views::Widget::InitParams::TYPE_POPUP);
    params.delegate = this;
    params.can_activate = false;
    params.transparent = true;
    params.parent = controller_->container_view();
    widget->Init(params);
    widget->SetContentsView(this);
    widget->Show();

    // Allow the popup to appear anywhere on the screen, since it may need
    // to go beyond the bounds of the window.
    // TODO(csharp): allow the popup to still appear on the border of
    // two screens.
    widget->SetBounds(gfx::Rect(GetScreenSize()));

    // Setup an observer to check for when the browser moves or changes size,
    // since the popup should always be hidden in those cases.
    observing_widget_ = views::Widget::GetTopLevelWidgetForNativeView(
        controller_->container_view());
    observing_widget_->AddObserver(this);
  }

  set_border(views::Border::CreateSolidBorder(kBorderThickness, kBorderColor));

  SetInitialBounds();
  UpdateBoundsAndRedrawPopup();
}

void AutofillPopupViewViews::InvalidateRow(size_t row) {
  SchedulePaintInRect(controller_->GetRectForRow(row, width()));
}

void AutofillPopupViewViews::UpdateBoundsAndRedrawPopup() {
  SetBoundsRect(controller_->popup_bounds());
  SchedulePaintInRect(controller_->popup_bounds());
}

void AutofillPopupViewViews::DrawAutofillEntry(gfx::Canvas* canvas,
                                               int index,
                                               const gfx::Rect& entry_rect) {
  // TODO(csharp): support RTL

  if (controller_->selected_line() == index)
    canvas->FillRect(entry_rect, kHoveredBackgroundColor);

  canvas->DrawStringInt(
      controller_->autofill_values()[index],
      controller_->value_font(),
      kValueTextColor,
      kEndPadding,
      entry_rect.y(),
      canvas->GetStringWidth(controller_->autofill_values()[index],
                             controller_->value_font()),
      entry_rect.height(),
      gfx::Canvas::TEXT_ALIGN_CENTER);

  // Use this to figure out where all the other Autofill items should be placed.
  int x_align_left = entry_rect.width() - kEndPadding;

  // Draw the delete icon, if one is needed.
  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  int row_height = controller_->GetRowHeightFromId(
      controller_->autofill_unique_ids()[index]);
  if (controller_->CanDelete(controller_->autofill_unique_ids()[index])) {
    x_align_left -= kDeleteIconWidth;

    // TODO(csharp): Create a custom resource for the delete icon.
    // http://crbug.com/131801
    canvas->DrawImageInt(
        *rb.GetImageSkiaNamed(IDR_CLOSE_BAR),
        x_align_left,
        entry_rect.y() + ((row_height - kDeleteIconHeight) / 2));

    x_align_left -= kIconPadding;
  }

  // Draw the Autofill icon, if one exists
  if (!controller_->autofill_icons()[index].empty()) {
    int icon =
        controller_->GetIconResourceID(controller_->autofill_icons()[index]);
    DCHECK_NE(-1, icon);
    int icon_y = entry_rect.y() + (row_height - kAutofillIconHeight) / 2;

    x_align_left -= kAutofillIconWidth;

    canvas->DrawImageInt(*rb.GetImageSkiaNamed(icon), x_align_left, icon_y);

    x_align_left -= kIconPadding;
  }

  // Draw the label text.
  x_align_left -= canvas->GetStringWidth(controller_->autofill_labels()[index],
                                         controller_->label_font());

  canvas->DrawStringInt(
      controller_->autofill_labels()[index],
      controller_->label_font(),
      kLabelTextColor,
      x_align_left + kEndPadding,
      entry_rect.y(),
      canvas->GetStringWidth(controller_->autofill_labels()[index],
                             controller_->label_font()),
      entry_rect.height(),
      gfx::Canvas::TEXT_ALIGN_CENTER);
}

void AutofillPopupViewViews::SetInitialBounds() {
  int bottom_of_field = controller_->element_bounds().bottom();
  int popup_height = controller_->GetPopupRequiredHeight();

  // Find the correct top position of the popup so that it doesn't go off
  // the screen.
  int top_of_popup = 0;
  if (GetScreenSize().height() < bottom_of_field + popup_height) {
    // The popup must appear above the field.
    top_of_popup = controller_->element_bounds().y() - popup_height;
  } else {
    // The popup can appear below the field.
    top_of_popup = bottom_of_field;
  }

  controller_->SetPopupBounds(gfx::Rect(
      controller_->element_bounds().x(),
      top_of_popup,
      controller_->GetPopupRequiredWidth(),
      popup_height));
}

gfx::Size AutofillPopupViewViews::GetScreenSize() {
  gfx::Screen* screen =
      gfx::Screen::GetScreenFor(controller_->container_view());
  gfx::Display display =
      screen->GetDisplayNearestPoint(controller_->element_bounds().origin());

  return display.GetSizeInPixel();
}

AutofillPopupView* AutofillPopupView::Create(
    AutofillPopupController* controller) {
  return new AutofillPopupViewViews(controller);
}
