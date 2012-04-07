// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gfx/native_theme_aura.h"

#include "base/basictypes.h"
#include "base/logging.h"
#include "grit/gfx_resources.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/size.h"
#include "ui/gfx/skbitmap_operations.h"

namespace {

const SkColor kMenuBackgroundColor = SkColorSetRGB(0xED, 0xED, 0xED);

// Theme colors returned by GetSystemColor().
const SkColor kInvalidColorIdColor = SkColorSetRGB(255, 0, 128);
// Dialogs:
const SkColor kDialogBackgroundColor = SK_ColorWHITE;
// FocusableBorder:
const SkColor kFocusedBorderColor = SkColorSetRGB(0x4D, 0x90, 0xFE);
const SkColor kUnfocusedBorderColor = SkColorSetRGB(0xD9, 0xD9, 0xD9);
// TextButton:
const SkColor kTextButtonBackgroundColor = SkColorSetRGB(0xDE, 0xDE, 0xDE);
const SkColor kTextButtonEnabledColor = SkColorSetRGB(0x44, 0x44, 0x44);
const SkColor kTextButtonDisabledColor = SkColorSetRGB(0x99, 0x99, 0x99);
const SkColor kTextButtonHighlightColor = SkColorSetRGB(0, 0, 0);
const SkColor kTextButtonHoverColor = kTextButtonEnabledColor;
// MenuItem:
const SkColor kEnabledMenuItemForegroundColor = SK_ColorBLACK;
const SkColor kDisabledMenuItemForegroundColor =
    SkColorSetRGB(0x80, 0x80, 0x80);
const SkColor kFocusedMenuItemBackgroundColor = SkColorSetRGB(0xDC, 0xE4, 0xFA);
// Textfield:
const SkColor kTextfieldDefaultColor = SK_ColorBLACK;
const SkColor kTextfieldDefaultBackground = SK_ColorWHITE;
const SkColor kTextfieldSelectionColor = SK_ColorWHITE;
const SkColor kTextfieldSelectionBackgroundFocused =
    SkColorSetRGB(0x1D, 0x90, 0xFF);
const SkColor kTextfieldSelectionBackgroundUnfocused = SK_ColorLTGRAY;

}  // namespace

namespace gfx {

// static
const NativeTheme* NativeTheme::instance() {
  return NativeThemeAura::instance();
}

// static
const NativeThemeAura* NativeThemeAura::instance() {
  CR_DEFINE_STATIC_LOCAL(NativeThemeAura, s_native_theme, ());
  return &s_native_theme;
}

NativeThemeAura::NativeThemeAura() {
}

NativeThemeAura::~NativeThemeAura() {
}

SkColor NativeThemeAura::GetSystemColor(ColorId color_id) const {
  // This implementation returns hardcoded colors.
  switch (color_id) {

    // Dialogs
    case kColorId_DialogBackground:
      return kDialogBackgroundColor;

    // FocusableBorder
    case kColorId_FocusedBorderColor:
      return kFocusedBorderColor;
    case kColorId_UnfocusedBorderColor:
      return kUnfocusedBorderColor;

    // TextButton
    case kColorId_TextButtonBackgroundColor:
      return kTextButtonBackgroundColor;
    case kColorId_TextButtonEnabledColor:
      return kTextButtonEnabledColor;
    case kColorId_TextButtonDisabledColor:
      return kTextButtonDisabledColor;
    case kColorId_TextButtonHighlightColor:
      return kTextButtonHighlightColor;
    case kColorId_TextButtonHoverColor:
      return kTextButtonHoverColor;

    // MenuItem
    case kColorId_EnabledMenuItemForegroundColor:
      return kEnabledMenuItemForegroundColor;
    case kColorId_DisabledMenuItemForegroundColor:
      return kDisabledMenuItemForegroundColor;
    case kColorId_FocusedMenuItemBackgroundColor:
      return kFocusedMenuItemBackgroundColor;

    // Textfield
    case kColorId_TextfieldDefaultColor:
      return kTextfieldDefaultColor;
    case kColorId_TextfieldDefaultBackground:
      return kTextfieldDefaultBackground;
    case kColorId_TextfieldSelectionColor:
      return kTextfieldSelectionColor;
    case kColorId_TextfieldSelectionBackgroundFocused:
      return kTextfieldSelectionBackgroundFocused;
    case kColorId_TextfieldSelectionBackgroundUnfocused:
      return kTextfieldSelectionBackgroundUnfocused;

    default:
      NOTREACHED() << "Invalid color_id: " << color_id;
      break;
  }

  // Return InvalidColor
  return kInvalidColorIdColor;
}

void NativeThemeAura::PaintMenuPopupBackground(
    SkCanvas* canvas,
    State state,
    const gfx::Rect& rect,
    const MenuListExtraParams& menu_list) const {
  canvas->drawColor(kMenuBackgroundColor, SkXfermode::kSrc_Mode);
}

void NativeThemeAura::PaintScrollbarTrack(
    SkCanvas* canvas,
    Part part,
    State state,
    const ScrollbarTrackExtraParams& extra_params,
    const gfx::Rect& rect) const {
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  if (part == kScrollbarVerticalTrack) {
    SkBitmap* background = rb.GetBitmapNamed(IDR_SCROLL_BACKGROUND);
    SkBitmap* border_up = rb.GetBitmapNamed(IDR_SCROLL_BACKGROUND_BORDER_UP);
    SkBitmap* border_down =
        rb.GetBitmapNamed(IDR_SCROLL_BACKGROUND_BORDER_DOWN);
    // Draw track background.
    DrawBitmapInt(
        canvas, *background,
        0, 0, background->width(), 1,
        rect.x(), rect.y(), rect.width(), rect.height());
    // Draw up button lower border.
    canvas->drawBitmap(*border_up, extra_params.track_x, extra_params.track_y);
    // Draw down button upper border.
    canvas->drawBitmap(
        *border_down,
        extra_params.track_x,
        extra_params.track_y + extra_params.track_height - border_down->height()
        );
  } else {
    SkBitmap* background =
        GetHorizontalBitmapNamed(IDR_SCROLL_BACKGROUND);
    SkBitmap* border_left =
        GetHorizontalBitmapNamed(IDR_SCROLL_BACKGROUND_BORDER_UP);
    SkBitmap* border_right =
        GetHorizontalBitmapNamed(IDR_SCROLL_BACKGROUND_BORDER_DOWN);
    // Draw track background.
    DrawBitmapInt(
        canvas, *background,
        0, 0, 1, background->height(),
        rect.x(), rect.y(), rect.width(), rect.height());
    // Draw left button right border.
    canvas->drawBitmap(*border_left,extra_params.track_x, extra_params.track_y);
    // Draw right button left border.
    canvas->drawBitmap(
        *border_right,
        extra_params.track_x + extra_params.track_width - border_right->width(),
        extra_params.track_y);
  }
}

void NativeThemeAura::PaintArrowButton(SkCanvas* canvas,
    const gfx::Rect& rect, Part part, State state) const {
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  int resource_id =
      (part == kScrollbarUpArrow || part == kScrollbarLeftArrow) ?
          IDR_SCROLL_ARROW_UP : IDR_SCROLL_ARROW_DOWN;
  if (state == kHovered)
    resource_id++;
  else if (state == kPressed)
    resource_id += 2;
  SkBitmap* bitmap;
  if (part == kScrollbarUpArrow || part == kScrollbarDownArrow)
    bitmap = rb.GetBitmapNamed(resource_id);
  else
    bitmap = GetHorizontalBitmapNamed(resource_id);
  DrawBitmapInt(canvas, *bitmap,
      0, 0, bitmap->width(), bitmap->height(),
      rect.x(), rect.y(), rect.width(), rect.height());
}

void NativeThemeAura::PaintScrollbarThumb(SkCanvas* canvas,
    Part part, State state, const gfx::Rect& rect) const {
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  int resource_id = IDR_SCROLL_THUMB;
  if (state == kHovered)
    resource_id++;
  else if (state == kPressed)
    resource_id += 2;
  if (part == kScrollbarVerticalThumb) {
    SkBitmap* bitmap = rb.GetBitmapNamed(resource_id);
    // Top
    DrawBitmapInt(
        canvas, *bitmap,
        0, 1, bitmap->width(), 5,
        rect.x(), rect.y(), rect.width(), 5);
    // Middle
    DrawBitmapInt(
        canvas, *bitmap,
        0, 7, bitmap->width(), 1,
        rect.x(), rect.y() + 5, rect.width(), rect.height() - 10);
    // Bottom
    DrawBitmapInt(
        canvas, *bitmap,
        0, 8, bitmap->width(), 5,
        rect.x(), rect.y() + rect.height() - 5, rect.width(), 5);
  } else {
    SkBitmap* bitmap = GetHorizontalBitmapNamed(resource_id);
    // Left
    DrawBitmapInt(
        canvas, *bitmap,
        1, 0, 5, bitmap->height(),
        rect.x(), rect.y(), 5, rect.height());
    // Middle
    DrawBitmapInt(
        canvas, *bitmap,
        7, 0, 1, bitmap->height(),
        rect.x() + 5, rect.y(), rect.width() - 10, rect.height());
    // Right
    DrawBitmapInt(
        canvas, *bitmap,
        8, 0, 5, bitmap->height(),
        rect.x() + rect.width() - 5, rect.y(), 5, rect.height());
  }
}

SkBitmap* NativeThemeAura::GetHorizontalBitmapNamed(int resource_id) const {
  SkImageMap::const_iterator found = horizontal_bitmaps_.find(resource_id);
  if (found != horizontal_bitmaps_.end())
    return found->second;

  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  SkBitmap* vertical_bitmap = rb.GetBitmapNamed(resource_id);

  if (vertical_bitmap) {
    SkBitmap transposed_bitmap =
        SkBitmapOperations::CreateTransposedBtmap(*vertical_bitmap);
    SkBitmap* horizontal_bitmap = new SkBitmap(transposed_bitmap);

    horizontal_bitmaps_[resource_id] = horizontal_bitmap;
    return horizontal_bitmap;
  }
  return NULL;
}

}  // namespace gfx
