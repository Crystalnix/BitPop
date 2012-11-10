// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/controls/label.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "base/i18n/rtl.h"
#include "base/logging.h"
#include "base/string_split.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "ui/base/accessibility/accessible_view_state.h"
#include "ui/base/native_theme/native_theme.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/text/text_elider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/font.h"
#include "ui/gfx/insets.h"
#include "ui/views/background.h"

namespace views {

// static
const char Label::kViewClassName[] = "views/Label";

const int Label::kFocusBorderPadding = 1;

Label::Label() {
  Init(string16(), GetDefaultFont());
}

Label::Label(const string16& text) {
  Init(text, GetDefaultFont());
}

Label::Label(const string16& text, const gfx::Font& font) {
  Init(text, font);
}

Label::~Label() {
}

void Label::SetFont(const gfx::Font& font) {
  font_ = font;
  text_size_valid_ = false;
  PreferredSizeChanged();
  SchedulePaint();
}

void Label::SetText(const string16& text) {
  text_ = text;
  text_size_valid_ = false;
  is_email_ = false;
  PreferredSizeChanged();
  SchedulePaint();
}

void Label::SetEmail(const string16& email) {
  SetText(email);
  is_email_ = true;
}

void Label::SetAutoColorReadabilityEnabled(bool enabled) {
  auto_color_readability_ = enabled;
  RecalculateColors();
}

void Label::SetEnabledColor(SkColor color) {
  requested_enabled_color_ = color;
  RecalculateColors();
}

void Label::SetDisabledColor(SkColor color) {
  requested_disabled_color_ = color;
  RecalculateColors();
}

void Label::SetBackgroundColor(SkColor color) {
  background_color_ = color;
  RecalculateColors();
}

void Label::SetShadowColors(SkColor enabled_color, SkColor disabled_color) {
  enabled_shadow_color_ = enabled_color;
  disabled_shadow_color_ = disabled_color;
  has_shadow_ = true;
}

void Label::SetShadowOffset(int x, int y) {
  shadow_offset_.SetPoint(x, y);
}

void Label::ClearEmbellishing() {
  has_shadow_ = false;
}

void Label::SetHorizontalAlignment(Alignment alignment) {
  // If the View's UI layout is right-to-left and directionality_mode_ is
  // USE_UI_DIRECTIONALITY, we need to flip the alignment so that the alignment
  // settings take into account the text directionality.
  if (base::i18n::IsRTL() && (directionality_mode_ == USE_UI_DIRECTIONALITY) &&
      (alignment != ALIGN_CENTER))
    alignment = (alignment == ALIGN_LEFT) ? ALIGN_RIGHT : ALIGN_LEFT;
  if (horiz_alignment_ != alignment) {
    horiz_alignment_ = alignment;
    SchedulePaint();
  }
}

void Label::SetMultiLine(bool multi_line) {
  DCHECK(!multi_line || elide_behavior_ != ELIDE_IN_MIDDLE);
  if (multi_line != is_multi_line_) {
    is_multi_line_ = multi_line;
    text_size_valid_ = false;
    PreferredSizeChanged();
    SchedulePaint();
  }
}

void Label::SetAllowCharacterBreak(bool allow_character_break) {
  if (allow_character_break != allow_character_break_) {
    allow_character_break_ = allow_character_break;
    text_size_valid_ = false;
    PreferredSizeChanged();
    SchedulePaint();
  }
}

void Label::SetElideBehavior(ElideBehavior elide_behavior) {
  DCHECK(elide_behavior != ELIDE_IN_MIDDLE || !is_multi_line_);
  if (elide_behavior != elide_behavior_) {
    elide_behavior_ = elide_behavior;
    text_size_valid_ = false;
    is_email_ = false;
    PreferredSizeChanged();
    SchedulePaint();
  }
}

void Label::SetTooltipText(const string16& tooltip_text) {
  tooltip_text_ = tooltip_text;
}

void Label::SetMouseOverBackground(Background* background) {
  mouse_over_background_.reset(background);
}

const Background* Label::GetMouseOverBackground() const {
  return mouse_over_background_.get();
}

void Label::SizeToFit(int max_width) {
  DCHECK(is_multi_line_);

  std::vector<string16> lines;
  base::SplitString(text_, '\n', &lines);

  int label_width = 0;
  for (std::vector<string16>::const_iterator iter = lines.begin();
       iter != lines.end(); ++iter) {
    label_width = std::max(label_width, font_.GetStringWidth(*iter));
  }

  label_width += GetInsets().width();

  if (max_width > 0)
    label_width = std::min(label_width, max_width);

  SetBounds(x(), y(), label_width, 0);
  SizeToPreferredSize();
}

void Label::SetHasFocusBorder(bool has_focus_border) {
  has_focus_border_ = has_focus_border;
  if (is_multi_line_) {
    text_size_valid_ = false;
    PreferredSizeChanged();
  }
}

gfx::Insets Label::GetInsets() const {
  gfx::Insets insets = View::GetInsets();
  if (focusable() || has_focus_border_)  {
    insets += gfx::Insets(kFocusBorderPadding, kFocusBorderPadding,
                          kFocusBorderPadding, kFocusBorderPadding);
  }
  return insets;
}

int Label::GetBaseline() const {
  return GetInsets().top() + font_.GetBaseline();
}

gfx::Size Label::GetPreferredSize() {
  // Return a size of (0, 0) if the label is not visible and if the
  // collapse_when_hidden_ flag is set.
  // TODO(munjal): This logic probably belongs to the View class. But for now,
  // put it here since putting it in View class means all inheriting classes
  // need ot respect the collapse_when_hidden_ flag.
  if (!visible() && collapse_when_hidden_)
    return gfx::Size();

  gfx::Size prefsize(GetTextSize());
  gfx::Insets insets = GetInsets();
  prefsize.Enlarge(insets.width(), insets.height());
  return prefsize;
}

int Label::GetHeightForWidth(int w) {
  if (!is_multi_line_)
    return View::GetHeightForWidth(w);

  w = std::max(0, w - GetInsets().width());
  int h = font_.GetHeight();
  gfx::Canvas::SizeStringInt(text_, font_, &w, &h, ComputeDrawStringFlags());
  return h + GetInsets().height();
}

std::string Label::GetClassName() const {
  return kViewClassName;
}

bool Label::HitTest(const gfx::Point& l) const {
  return false;
}

void Label::OnMouseMoved(const MouseEvent& event) {
  UpdateContainsMouse(event);
}

void Label::OnMouseEntered(const MouseEvent& event) {
  UpdateContainsMouse(event);
}

void Label::OnMouseExited(const MouseEvent& event) {
  SetContainsMouse(false);
}

bool Label::GetTooltipText(const gfx::Point& p, string16* tooltip) const {
  DCHECK(tooltip);

  // If a tooltip has been explicitly set, use it.
  if (!tooltip_text_.empty()) {
    tooltip->assign(tooltip_text_);
    return true;
  }

  // Show the full text if the text does not fit.
  if (!is_multi_line_ &&
      (font_.GetStringWidth(text_) > GetAvailableRect().width())) {
    *tooltip = text_;
    return true;
  }
  return false;
}

void Label::GetAccessibleState(ui::AccessibleViewState* state) {
  state->role = ui::AccessibilityTypes::ROLE_STATICTEXT;
  state->state = ui::AccessibilityTypes::STATE_READONLY;
  state->name = text_;
}

void Label::PaintText(gfx::Canvas* canvas,
                      const string16& text,
                      const gfx::Rect& text_bounds,
                      int flags) {
  if (has_shadow_) {
    canvas->DrawStringInt(
        text, font_,
        enabled() ? enabled_shadow_color_ : disabled_shadow_color_,
        text_bounds.x() + shadow_offset_.x(),
        text_bounds.y() + shadow_offset_.y(),
        text_bounds.width(), text_bounds.height(),
        flags);
  }
  canvas->DrawStringInt(text, font_,
      enabled() ? actual_enabled_color_ : actual_disabled_color_,
      text_bounds.x(), text_bounds.y(), text_bounds.width(),
      text_bounds.height(), flags);

  if (HasFocus() || paint_as_focused_) {
    gfx::Rect focus_bounds = text_bounds;
    focus_bounds.Inset(-kFocusBorderPadding, -kFocusBorderPadding);
    canvas->DrawFocusRect(focus_bounds);
  }
}

gfx::Size Label::GetTextSize() const {
  if (!text_size_valid_) {
    // For single-line strings, we supply the largest possible width, because
    // while adding NO_ELLIPSIS to the flags works on Windows for forcing
    // SizeStringInt() to calculate the desired width, it doesn't seem to work
    // on Linux.
    int w = is_multi_line_ ?
        GetAvailableRect().width() : std::numeric_limits<int>::max();
    int h = font_.GetHeight();
    // For single-line strings, ignore the available width and calculate how
    // wide the text wants to be.
    int flags = ComputeDrawStringFlags();
    if (!is_multi_line_)
      flags |= gfx::Canvas::NO_ELLIPSIS;
    gfx::Canvas::SizeStringInt(text_, font_, &w, &h, flags);
    text_size_.SetSize(w, h);
    text_size_valid_ = true;
  }

  return text_size_;
}

void Label::OnBoundsChanged(const gfx::Rect& previous_bounds) {
  text_size_valid_ &= !is_multi_line_;
}

void Label::OnPaint(gfx::Canvas* canvas) {
  OnPaintBackground(canvas);
  // We skip painting the focus border because it is being handled seperately by
  // some subclasses of Label. We do not want View's focus border painting to
  // interfere with that.
  OnPaintBorder(canvas);

  string16 paint_text;
  gfx::Rect text_bounds;
  int flags = 0;
  CalculateDrawStringParams(&paint_text, &text_bounds, &flags);
  PaintText(canvas, paint_text, text_bounds, flags);
}

void Label::OnPaintBackground(gfx::Canvas* canvas) {
  const Background* bg = contains_mouse_ ? GetMouseOverBackground() : NULL;
  if (!bg)
    bg = background();
  if (bg)
    bg->Paint(canvas, this);
}

// static
gfx::Font Label::GetDefaultFont() {
  return ResourceBundle::GetSharedInstance().GetFont(ResourceBundle::BaseFont);
}

void Label::Init(const string16& text, const gfx::Font& font) {
  contains_mouse_ = false;
  font_ = font;
  text_size_valid_ = false;
  requested_enabled_color_ = ui::NativeTheme::instance()->GetSystemColor(
      ui::NativeTheme::kColorId_LabelEnabledColor);
  requested_disabled_color_ = ui::NativeTheme::instance()->GetSystemColor(
      ui::NativeTheme::kColorId_LabelDisabledColor);
  background_color_ = ui::NativeTheme::instance()->GetSystemColor(
      ui::NativeTheme::kColorId_LabelBackgroundColor);
  auto_color_readability_ = true;
  RecalculateColors();
  horiz_alignment_ = ALIGN_CENTER;
  is_multi_line_ = false;
  allow_character_break_ = false;
  elide_behavior_ = NO_ELIDE;
  is_email_ = false;
  collapse_when_hidden_ = false;
  directionality_mode_ = USE_UI_DIRECTIONALITY;
  paint_as_focused_ = false;
  has_focus_border_ = false;
  enabled_shadow_color_ = 0;
  disabled_shadow_color_ = 0;
  shadow_offset_.SetPoint(1, 1);
  has_shadow_ = false;

  SetText(text);
}

void Label::RecalculateColors() {
  actual_enabled_color_ = auto_color_readability_ ?
      color_utils::GetReadableColor(requested_enabled_color_,
                                    background_color_) :
      requested_enabled_color_;
  actual_disabled_color_ = auto_color_readability_ ?
      color_utils::GetReadableColor(requested_disabled_color_,
                                    background_color_) :
      requested_disabled_color_;
}

void Label::UpdateContainsMouse(const MouseEvent& event) {
  SetContainsMouse(GetTextBounds().Contains(event.x(), event.y()));
}

void Label::SetContainsMouse(bool contains_mouse) {
  if (contains_mouse_ == contains_mouse)
    return;
  contains_mouse_ = contains_mouse;
  if (GetMouseOverBackground())
    SchedulePaint();
}

gfx::Rect Label::GetTextBounds() const {
  gfx::Rect available_rect(GetAvailableRect());
  gfx::Size text_size(GetTextSize());
  text_size.set_width(std::min(available_rect.width(), text_size.width()));

  gfx::Insets insets = GetInsets();
  gfx::Point text_origin(insets.left(), insets.top());
  switch (horiz_alignment_) {
    case ALIGN_LEFT:
      break;
    case ALIGN_CENTER:
      // We put any extra margin pixel on the left rather than the right.  We
      // used to do this because measurement on Windows used
      // GetTextExtentPoint32(), which could report a value one too large on the
      // right; we now use DrawText(), and who knows if it can also do this.
      text_origin.Offset((available_rect.width() + 1 - text_size.width()) / 2,
                         0);
      break;
    case ALIGN_RIGHT:
      text_origin.set_x(available_rect.right() - text_size.width());
      break;
    default:
      NOTREACHED();
      break;
  }
  text_origin.Offset(0,
      std::max(0, (available_rect.height() - text_size.height())) / 2);
  return gfx::Rect(text_origin, text_size);
}

int Label::ComputeDrawStringFlags() const {
  int flags = 0;

  // We can't use subpixel rendering if the background is non-opaque.
  if (SkColorGetA(background_color_) != 0xFF)
    flags |= gfx::Canvas::NO_SUBPIXEL_RENDERING;

  if (directionality_mode_ == AUTO_DETECT_DIRECTIONALITY) {
    base::i18n::TextDirection direction =
        base::i18n::GetFirstStrongCharacterDirection(text_);
    if (direction == base::i18n::RIGHT_TO_LEFT)
      flags |= gfx::Canvas::FORCE_RTL_DIRECTIONALITY;
    else
      flags |= gfx::Canvas::FORCE_LTR_DIRECTIONALITY;
  }

  if (!is_multi_line_)
    return flags;

  flags |= gfx::Canvas::MULTI_LINE;
#if !defined(OS_WIN)
    // Don't elide multiline labels on Linux.
    // Todo(davemoore): Do we depend on eliding multiline text?
    // Pango insists on limiting the number of lines to one if text is
    // elided. You can get around this if you can pass a maximum height
    // but we don't currently have that data when we call the pango code.
    flags |= gfx::Canvas::NO_ELLIPSIS;
#endif
  if (allow_character_break_)
    flags |= gfx::Canvas::CHARACTER_BREAK;
  switch (horiz_alignment_) {
    case ALIGN_LEFT:
      flags |= gfx::Canvas::TEXT_ALIGN_LEFT;
      break;
    case ALIGN_CENTER:
      flags |= gfx::Canvas::TEXT_ALIGN_CENTER;
      break;
    case ALIGN_RIGHT:
      flags |= gfx::Canvas::TEXT_ALIGN_RIGHT;
      break;
  }

  return flags;
}

gfx::Rect Label::GetAvailableRect() const {
  gfx::Rect bounds(gfx::Point(), size());
  gfx::Insets insets(GetInsets());
  bounds.Inset(insets.left(), insets.top(), insets.right(), insets.bottom());
  return bounds;
}

void Label::CalculateDrawStringParams(string16* paint_text,
                                      gfx::Rect* text_bounds,
                                      int* flags) const {
  DCHECK(paint_text && text_bounds && flags);

  if (is_email_) {
    *paint_text = ui::ElideEmail(text_, font_, GetAvailableRect().width());
  } else if (elide_behavior_ == ELIDE_IN_MIDDLE) {
    *paint_text = ui::ElideText(text_, font_, GetAvailableRect().width(),
                                ui::ELIDE_IN_MIDDLE);
  } else if (elide_behavior_ == ELIDE_AT_END) {
    *paint_text = ui::ElideText(text_, font_, GetAvailableRect().width(),
                                ui::ELIDE_AT_END);
  } else {
    *paint_text = text_;
  }

  *text_bounds = GetTextBounds();
  *flags = ComputeDrawStringFlags();
}

}  // namespace views
