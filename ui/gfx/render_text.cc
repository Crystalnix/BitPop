// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gfx/render_text.h"

#include <algorithm>

#include "base/debug/trace_event.h"
#include "base/i18n/break_iterator.h"
#include "base/logging.h"
#include "base/stl_util.h"
#include "third_party/skia/include/core/SkTypeface.h"
#include "third_party/skia/include/effects/SkGradientShader.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/canvas_skia.h"
#include "ui/gfx/native_theme.h"
#include "unicode/uchar.h"

namespace {

// Color settings for text, backgrounds and cursor.
// These are tentative, and should be derived from theme, system
// settings and current settings.
// TODO(oshima): Change this to match the standard chrome
// before dogfooding textfield views.
const SkColor kCursorColor = SK_ColorBLACK;

#ifndef NDEBUG
// Check StyleRanges invariant conditions: sorted and non-overlapping ranges.
void CheckStyleRanges(const gfx::StyleRanges& style_ranges, size_t length) {
  if (length == 0) {
    DCHECK(style_ranges.empty()) << "Style ranges exist for empty text.";
    return;
  }
  for (gfx::StyleRanges::size_type i = 0; i < style_ranges.size() - 1; i++) {
    const ui::Range& former = style_ranges[i].range;
    const ui::Range& latter = style_ranges[i + 1].range;
    DCHECK(!former.is_empty()) << "Empty range at " << i << ":" << former;
    DCHECK(former.IsValid()) << "Invalid range at " << i << ":" << former;
    DCHECK(!former.is_reversed()) << "Reversed range at " << i << ":" << former;
    DCHECK(former.end() == latter.start()) << "Ranges gap/overlap/unsorted." <<
        "former:" << former << ", latter:" << latter;
  }
  const gfx::StyleRange& end_style = *style_ranges.rbegin();
  DCHECK(!end_style.range.is_empty()) << "Empty range at end.";
  DCHECK(end_style.range.IsValid()) << "Invalid range at end.";
  DCHECK(!end_style.range.is_reversed()) << "Reversed range at end.";
  DCHECK(end_style.range.end() == length) << "Style and text length mismatch.";
}
#endif

void ApplyStyleRangeImpl(gfx::StyleRanges* style_ranges,
                         const gfx::StyleRange& style_range) {
  const ui::Range& new_range = style_range.range;
  // Follow StyleRanges invariant conditions: sorted and non-overlapping ranges.
  gfx::StyleRanges::iterator i;
  for (i = style_ranges->begin(); i != style_ranges->end();) {
    if (i->range.end() < new_range.start()) {
      i++;
    } else if (i->range.start() == new_range.end()) {
      break;
    } else if (new_range.Contains(i->range)) {
      i = style_ranges->erase(i);
      if (i == style_ranges->end())
        break;
    } else if (i->range.start() < new_range.start() &&
               i->range.end() > new_range.end()) {
      // Split the current style into two styles.
      gfx::StyleRange split_style = gfx::StyleRange(*i);
      split_style.range.set_end(new_range.start());
      i = style_ranges->insert(i, split_style) + 1;
      i->range.set_start(new_range.end());
      break;
    } else if (i->range.start() < new_range.start()) {
      i->range.set_end(new_range.start());
      i++;
    } else if (i->range.end() > new_range.end()) {
      i->range.set_start(new_range.end());
      break;
    } else {
      NOTREACHED();
    }
  }
  // Add the new range in its sorted location.
  style_ranges->insert(i, style_range);
}

// Converts |gfx::Font::FontStyle| flags to |SkTypeface::Style| flags.
SkTypeface::Style ConvertFontStyleToSkiaTypefaceStyle(int font_style) {
  int skia_style = SkTypeface::kNormal;
  if (font_style & gfx::Font::BOLD)
    skia_style |= SkTypeface::kBold;
  if (font_style & gfx::Font::ITALIC)
    skia_style |= SkTypeface::kItalic;
  return static_cast<SkTypeface::Style>(skia_style);
}

// Given |font| and |display_width|, returns the width of the fade gradient.
int CalculateFadeGradientWidth(const gfx::Font& font, int display_width) {
  // Fade in/out about 2.5 characters of the beginning/end of the string.
  // The .5 here is helpful if one of the characters is a space.
  // Use a quarter of the display width if the display width is very short.
  const int average_character_width = font.GetAverageCharacterWidth();
  const double gradient_width = std::min(average_character_width * 2.5,
                                         display_width / 4.0);
  DCHECK_GE(gradient_width, 0.0);
  return static_cast<int>(floor(gradient_width + 0.5));
}

// Appends to |positions| and |colors| values corresponding to the fade over
// |fade_rect| from color |c0| to color |c1|.
void AddFadeEffect(const gfx::Rect& text_rect,
                   const gfx::Rect& fade_rect,
                   SkColor c0,
                   SkColor c1,
                   std::vector<SkScalar>* positions,
                   std::vector<SkColor>* colors) {
  const SkScalar left = static_cast<SkScalar>(fade_rect.x() - text_rect.x());
  const SkScalar width = static_cast<SkScalar>(fade_rect.width());
  const SkScalar p0 = left / text_rect.width();
  const SkScalar p1 = (left + width) / text_rect.width();
  // Prepend 0.0 to |positions|, as required by Skia.
  if (positions->empty() && p0 != 0.0) {
    positions->push_back(0.0);
    colors->push_back(c0);
  }
  positions->push_back(p0);
  colors->push_back(c0);
  positions->push_back(p1);
  colors->push_back(c1);
}

// Creates a SkShader to fade the text, with |left_part| specifying the left
// fade effect, if any, and |right_part| specifying the right fade effect.
SkShader* CreateFadeShader(const gfx::Rect& text_rect,
                           const gfx::Rect& left_part,
                           const gfx::Rect& right_part,
                           SkColor color) {
  // Fade alpha of 51/255 corresponds to a fade of 0.2 of the original color.
  const SkColor fade_color = SkColorSetA(color, 51);
  const SkPoint points[2] = {
    SkPoint::Make(text_rect.x(), text_rect.y()),
    SkPoint::Make(text_rect.right(), text_rect.y())
  };
  std::vector<SkScalar> positions;
  std::vector<SkColor> colors;

  if (!left_part.IsEmpty())
    AddFadeEffect(text_rect, left_part, fade_color, color,
                  &positions, &colors);
  if (!right_part.IsEmpty())
    AddFadeEffect(text_rect, right_part, color, fade_color,
                  &positions, &colors);
  DCHECK(!positions.empty());

  // Terminate |positions| with 1.0, as required by Skia.
  if (positions.back() != 1.0) {
    positions.push_back(1.0);
    colors.push_back(colors.back());
  }

  return SkGradientShader::CreateLinear(&points[0], &colors[0], &positions[0],
                                        colors.size(),
                                        SkShader::kClamp_TileMode);
}


}  // namespace

namespace gfx {

namespace internal {

SkiaTextRenderer::SkiaTextRenderer(Canvas* canvas)
    : canvas_skia_(canvas->GetSkCanvas()) {
  DCHECK(canvas_skia_);
  paint_.setTextEncoding(SkPaint::kGlyphID_TextEncoding);
  paint_.setStyle(SkPaint::kFill_Style);
  paint_.setAntiAlias(true);
  paint_.setSubpixelText(true);
  paint_.setLCDRenderText(true);
}

SkiaTextRenderer::~SkiaTextRenderer() {
}

void SkiaTextRenderer::SetTypeface(SkTypeface* typeface) {
  paint_.setTypeface(typeface);
}

void SkiaTextRenderer::SetTextSize(int size) {
  paint_.setTextSize(size);
}

void SkiaTextRenderer::SetFontStyle(int style) {
  SkTypeface::Style skia_style = ConvertFontStyleToSkiaTypefaceStyle(style);
  SkTypeface* current_typeface = paint_.getTypeface();

  if (current_typeface->style() == skia_style)
    return;

  SkAutoTUnref<SkTypeface> typeface(
      SkTypeface::CreateFromTypeface(current_typeface, skia_style));
  if (typeface.get()) {
    // |paint_| adds its own ref. So don't |release()| it from the ref ptr here.
    SetTypeface(typeface.get());
  }
}

void SkiaTextRenderer::SetFont(const gfx::Font& font) {
  SkTypeface::Style skia_style =
      ConvertFontStyleToSkiaTypefaceStyle(font.GetStyle());
  SkAutoTUnref<SkTypeface> typeface(
      SkTypeface::CreateFromName(font.GetFontName().c_str(), skia_style));
  if (typeface.get()) {
    // |paint_| adds its own ref. So don't |release()| it from the ref ptr here.
    SetTypeface(typeface.get());
  }
  SetTextSize(font.GetFontSize());
}

void SkiaTextRenderer::SetForegroundColor(SkColor foreground) {
  paint_.setColor(foreground);
}

void SkiaTextRenderer::SetShader(SkShader* shader) {
  paint_.setShader(shader);
}

void SkiaTextRenderer::DrawPosText(const SkPoint* pos,
                                   const uint16* glyphs,
                                   size_t glyph_count) {
  size_t byte_length = glyph_count * sizeof(glyphs[0]);
  canvas_skia_->drawPosText(&glyphs[0], byte_length, &pos[0], paint_);
}

// Draw underline and strike through text decorations.
// Based on |SkCanvas::DrawTextDecorations()| and constants from:
//   third_party/skia/src/core/SkTextFormatParams.h
void SkiaTextRenderer::DrawDecorations(int x, int y, int width,
                                       const StyleRange& style) {
  if (!style.underline && !style.strike && !style.diagonal_strike)
    return;

  // Fraction of the text size to lower a strike through below the baseline.
  const SkScalar kStrikeThroughOffset = (-SK_Scalar1 * 6 / 21);
  // Fraction of the text size to lower an underline below the baseline.
  const SkScalar kUnderlineOffset = (SK_Scalar1 / 9);
  // Fraction of the text size to use for a strike through or under-line.
  const SkScalar kLineThickness = (SK_Scalar1 / 18);
  // Fraction of the text size to use for a top margin of a diagonal strike.
  const SkScalar kDiagonalStrikeThroughMarginOffset = (SK_Scalar1 / 4);

  SkScalar text_size = paint_.getTextSize();
  SkScalar height = SkScalarMul(text_size, kLineThickness);
  SkRect r;

  r.fLeft = x;
  r.fRight = x + width;

  if (style.underline) {
    SkScalar offset = SkScalarMulAdd(text_size, kUnderlineOffset, y);
    r.fTop = offset;
    r.fBottom = offset + height;
    canvas_skia_->drawRect(r, paint_);
  }
  if (style.strike) {
    SkScalar offset = SkScalarMulAdd(text_size, kStrikeThroughOffset, y);
    r.fTop = offset;
    r.fBottom = offset + height;
    canvas_skia_->drawRect(r, paint_);
  }
  if (style.diagonal_strike) {
    SkScalar offset =
        SkScalarMul(text_size, kDiagonalStrikeThroughMarginOffset);
    SkPaint paint(paint_);
    paint.setAntiAlias(true);
    paint.setStyle(SkPaint::kFill_Style);
    paint.setStrokeWidth(height);
    canvas_skia_->drawLine(
        SkIntToScalar(x), SkIntToScalar(y) - text_size + offset,
        SkIntToScalar(x + width), SkIntToScalar(y),
        paint);
  }
}

}  // namespace internal


StyleRange::StyleRange()
    : foreground(SK_ColorBLACK),
      font_style(gfx::Font::NORMAL),
      strike(false),
      diagonal_strike(false),
      underline(false) {
}

RenderText::~RenderText() {
}

void RenderText::SetText(const string16& text) {
  DCHECK(!composition_range_.IsValid());
  size_t old_text_length = text_.length();
  text_ = text;

  // Update the style ranges as needed.
  if (text_.empty()) {
    style_ranges_.clear();
  } else if (style_ranges_.empty()) {
    ApplyDefaultStyle();
  } else if (text_.length() > old_text_length) {
    style_ranges_.back().range.set_end(text_.length());
  } else if (text_.length() < old_text_length) {
    StyleRanges::iterator i;
    for (i = style_ranges_.begin(); i != style_ranges_.end(); i++) {
      if (i->range.start() >= text_.length()) {
        // Style ranges are sorted and non-overlapping, so all the subsequent
        // style ranges should be out of text_.length() as well.
        style_ranges_.erase(i, style_ranges_.end());
        break;
      }
    }
    // Since style ranges are sorted and non-overlapping, if there is a style
    // range ends beyond text_.length, it must be the last one.
    style_ranges_.back().range.set_end(text_.length());
  }
#ifndef NDEBUG
  CheckStyleRanges(style_ranges_, text_.length());
#endif
  cached_bounds_and_offset_valid_ = false;

  // Reset selection model. SetText should always followed by SetSelectionModel
  // or SetCursorPosition in upper layer.
  SetSelectionModel(SelectionModel(0, 0, SelectionModel::LEADING));

  UpdateLayout();
}

void RenderText::SetHorizontalAlignment(HorizontalAlignment alignment) {
  if (horizontal_alignment_ != alignment) {
    horizontal_alignment_ = alignment;
    display_offset_ = Point();
    cached_bounds_and_offset_valid_ = false;
  }
}

void RenderText::SetFontList(const FontList& font_list) {
  font_list_ = font_list;
  cached_bounds_and_offset_valid_ = false;
  UpdateLayout();
}

void RenderText::SetFontSize(int size) {
  font_list_ = font_list_.DeriveFontListWithSize(size);
  cached_bounds_and_offset_valid_ = false;
  UpdateLayout();
}

void RenderText::SetCursorEnabled(bool cursor_enabled) {
  cursor_enabled_ = cursor_enabled;
  cached_bounds_and_offset_valid_ = false;
}

const Font& RenderText::GetFont() const {
  return font_list_.GetFonts()[0];
}

void RenderText::ToggleInsertMode() {
  insert_mode_ = !insert_mode_;
  cached_bounds_and_offset_valid_ = false;
}

void RenderText::SetDisplayRect(const Rect& r) {
  display_rect_ = r;
  cached_bounds_and_offset_valid_ = false;
  UpdateLayout();
}

size_t RenderText::GetCursorPosition() const {
  return selection_model_.selection_end();
}

void RenderText::SetCursorPosition(size_t position) {
  MoveCursorTo(position, false);
}

void RenderText::MoveCursor(BreakType break_type,
                            VisualCursorDirection direction,
                            bool select) {
  SelectionModel position(selection_model());
  position.set_selection_start(GetCursorPosition());
  // Cancelling a selection moves to the edge of the selection.
  if (break_type != LINE_BREAK && !EmptySelection() && !select) {
    SelectionModel selection_start = GetSelectionModelForSelectionStart();
    int start_x = GetCursorBounds(selection_start, true).x();
    int cursor_x = GetCursorBounds(position, true).x();
    // Use the selection start if it is left (when |direction| is CURSOR_LEFT)
    // or right (when |direction| is CURSOR_RIGHT) of the selection end.
    if (direction == CURSOR_RIGHT ? start_x > cursor_x : start_x < cursor_x)
      position = selection_start;
    // For word breaks, use the nearest word boundary in the appropriate
    // |direction|.
    if (break_type == WORD_BREAK)
      position = GetAdjacentSelectionModel(position, break_type, direction);
  } else {
    position = GetAdjacentSelectionModel(position, break_type, direction);
  }
  if (select)
    position.set_selection_start(GetSelectionStart());
  MoveCursorTo(position);
}

bool RenderText::MoveCursorTo(const SelectionModel& model) {
  SelectionModel sel(model);
  size_t text_length = text().length();
  // Enforce valid selection model components.
  if (sel.selection_start() > text_length)
    sel.set_selection_start(text_length);
  if (sel.selection_end() > text_length)
    sel.set_selection_end(text_length);
  // The current model only supports caret positions at valid character indices.
  if (text_length == 0) {
    sel.set_caret_pos(0);
    sel.set_caret_placement(SelectionModel::LEADING);
  } else if (sel.caret_pos() >= text_length) {
    SelectionModel end_selection =
        EdgeSelectionModel(GetVisualDirectionOfLogicalEnd());
    sel.set_caret_pos(end_selection.caret_pos());
    sel.set_caret_placement(end_selection.caret_placement());
  }

  if (!IsCursorablePosition(sel.selection_start()) ||
      !IsCursorablePosition(sel.selection_end()) ||
      !IsCursorablePosition(sel.caret_pos()))
    return false;

  bool changed = !sel.Equals(selection_model_);
  SetSelectionModel(sel);
  return changed;
}

bool RenderText::MoveCursorTo(const Point& point, bool select) {
  SelectionModel selection = FindCursorPosition(point);
  if (select)
    selection.set_selection_start(GetSelectionStart());
  return MoveCursorTo(selection);
}

bool RenderText::SelectRange(const ui::Range& range) {
  size_t text_length = text().length();
  size_t start = std::min(range.start(), text_length);
  size_t end = std::min(range.end(), text_length);

  if (!IsCursorablePosition(start) || !IsCursorablePosition(end))
    return false;

  size_t pos = end;
  SelectionModel::CaretPlacement placement = SelectionModel::LEADING;
  if (start < end) {
    pos = IndexOfAdjacentGrapheme(end, CURSOR_BACKWARD);
    DCHECK_LT(pos, end);
    placement = SelectionModel::TRAILING;
  } else if (end == text_length) {
    SelectionModel end_selection =
        EdgeSelectionModel(GetVisualDirectionOfLogicalEnd());
    pos = end_selection.caret_pos();
    placement = end_selection.caret_placement();
  }
  SetSelectionModel(SelectionModel(start, end, pos, placement));
  return true;
}

bool RenderText::IsPointInSelection(const Point& point) {
  if (EmptySelection())
    return false;
  // TODO(xji): should this check whether the point is inside the visual
  // selection bounds? In case of "abcFED", if "ED" is selected, |point| points
  // to the right half of 'c', is the point in selection?
  size_t pos = FindCursorPosition(point).selection_end();
  return (pos >= MinOfSelection() && pos < MaxOfSelection());
}

void RenderText::ClearSelection() {
  SelectionModel sel(selection_model());
  sel.set_selection_start(GetCursorPosition());
  SetSelectionModel(sel);
}

void RenderText::SelectAll() {
  SelectionModel sel = EdgeSelectionModel(CURSOR_RIGHT);
  sel.set_selection_start(EdgeSelectionModel(CURSOR_LEFT).selection_start());
  SetSelectionModel(sel);
}

void RenderText::SelectWord() {
  size_t cursor_position = GetCursorPosition();

  base::i18n::BreakIterator iter(text(), base::i18n::BreakIterator::BREAK_WORD);
  bool success = iter.Init();
  DCHECK(success);
  if (!success)
    return;

  size_t selection_start = cursor_position;
  for (; selection_start != 0; --selection_start) {
    if (iter.IsStartOfWord(selection_start) ||
        iter.IsEndOfWord(selection_start))
      break;
  }

  if (selection_start == cursor_position)
    ++cursor_position;

  for (; cursor_position < text().length(); ++cursor_position) {
    if (iter.IsEndOfWord(cursor_position) ||
        iter.IsStartOfWord(cursor_position))
      break;
  }

  MoveCursorTo(selection_start, false);
  MoveCursorTo(cursor_position, true);
}

const ui::Range& RenderText::GetCompositionRange() const {
  return composition_range_;
}

void RenderText::SetCompositionRange(const ui::Range& composition_range) {
  CHECK(!composition_range.IsValid() ||
        ui::Range(0, text_.length()).Contains(composition_range));
  composition_range_.set_end(composition_range.end());
  composition_range_.set_start(composition_range.start());
  UpdateLayout();
}

void RenderText::ApplyStyleRange(const StyleRange& style_range) {
  const ui::Range& new_range = style_range.range;
  if (!new_range.IsValid() || new_range.is_empty())
    return;
  CHECK(!new_range.is_reversed());
  CHECK(ui::Range(0, text_.length()).Contains(new_range));
  ApplyStyleRangeImpl(&style_ranges_, style_range);
#ifndef NDEBUG
  CheckStyleRanges(style_ranges_, text_.length());
#endif
  // TODO(xji): only invalidate if font or underline changes.
  cached_bounds_and_offset_valid_ = false;
  UpdateLayout();
}

void RenderText::ApplyDefaultStyle() {
  style_ranges_.clear();
  StyleRange style = StyleRange(default_style_);
  style.range.set_end(text_.length());
  style_ranges_.push_back(style);
  cached_bounds_and_offset_valid_ = false;
  UpdateLayout();
}

VisualCursorDirection RenderText::GetVisualDirectionOfLogicalEnd() {
  return GetTextDirection() == base::i18n::LEFT_TO_RIGHT ?
      CURSOR_RIGHT : CURSOR_LEFT;
}

void RenderText::Draw(Canvas* canvas) {
  TRACE_EVENT0("gfx", "RenderText::Draw");
  {
    TRACE_EVENT0("gfx", "RenderText::EnsureLayout");
    EnsureLayout();
  }

  canvas->Save();
  canvas->ClipRect(display_rect());

  if (!text().empty())
    DrawSelection(canvas);

  DrawCursor(canvas);

  if (!text().empty()) {
    TRACE_EVENT0("gfx", "RenderText::Draw draw text");
    DrawVisualText(canvas);
  }
  canvas->Restore();
}

const Rect& RenderText::GetUpdatedCursorBounds() {
  UpdateCachedBoundsAndOffset();
  return cursor_bounds_;
}

SelectionModel RenderText::GetSelectionModelForSelectionStart() {
  size_t selection_start = GetSelectionStart();
  size_t selection_end = GetCursorPosition();
  if (selection_start < selection_end)
    return SelectionModel(selection_start,
                          selection_start,
                          SelectionModel::LEADING);
  else if (selection_start > selection_end)
    return SelectionModel(
        selection_start,
        IndexOfAdjacentGrapheme(selection_start, CURSOR_BACKWARD),
        SelectionModel::TRAILING);
  return selection_model_;
}

RenderText::RenderText()
    : horizontal_alignment_(base::i18n::IsRTL() ? ALIGN_RIGHT : ALIGN_LEFT),
      cursor_enabled_(true),
      cursor_visible_(false),
      insert_mode_(true),
      focused_(false),
      composition_range_(ui::Range::InvalidRange()),
      fade_head_(false),
      fade_tail_(false),
      cached_bounds_and_offset_valid_(false) {
}

const Point& RenderText::GetUpdatedDisplayOffset() {
  UpdateCachedBoundsAndOffset();
  return display_offset_;
}

SelectionModel RenderText::GetAdjacentSelectionModel(
    const SelectionModel& current,
    BreakType break_type,
    VisualCursorDirection direction) {
  EnsureLayout();

  if (break_type == LINE_BREAK || text().empty())
    return EdgeSelectionModel(direction);
  if (break_type == CHARACTER_BREAK)
    return AdjacentCharSelectionModel(current, direction);
  DCHECK(break_type == WORD_BREAK);
  return AdjacentWordSelectionModel(current, direction);
}

void RenderText::SetSelectionModel(const SelectionModel& model) {
  DCHECK_LE(model.selection_start(), text().length());
  selection_model_.set_selection_start(model.selection_start());
  DCHECK_LE(model.selection_end(), text().length());
  selection_model_.set_selection_end(model.selection_end());
  DCHECK_LT(model.caret_pos(), std::max<size_t>(text().length(), 1));
  selection_model_.set_caret_pos(model.caret_pos());
  selection_model_.set_caret_placement(model.caret_placement());

  cached_bounds_and_offset_valid_ = false;
}

void RenderText::ApplyCompositionAndSelectionStyles(
    StyleRanges* style_ranges) {
  // TODO(msw): This pattern ought to be reconsidered; what about composition
  //            and selection overlaps, retain existing local style features?
  // Apply a composition style override to a copy of the style ranges.
  if (composition_range_.IsValid() && !composition_range_.is_empty()) {
    StyleRange composition_style(default_style_);
    composition_style.underline = true;
    composition_style.range.set_start(composition_range_.start());
    composition_style.range.set_end(composition_range_.end());
    ApplyStyleRangeImpl(style_ranges, composition_style);
  }
  // Apply a selection style override to a copy of the style ranges.
  if (!EmptySelection()) {
    StyleRange selection_style(default_style_);
    selection_style.foreground = NativeTheme::instance()->GetSystemColor(
        NativeTheme::kColorId_TextfieldSelectionColor);
    selection_style.range.set_start(MinOfSelection());
    selection_style.range.set_end(MaxOfSelection());
    ApplyStyleRangeImpl(style_ranges, selection_style);
  }
  // Apply replacement-mode style override to a copy of the style ranges.
  //
  // TODO(xji): NEED TO FIX FOR WINDOWS ASAP. Windows call this function (to
  // apply styles) in ItemizeLogicalText(). In order for the cursor's underline
  // character to be drawn correctly, we will need to re-layout the text. It's
  // not practical to do layout on every cursor blink. We need to fix Windows
  // port to apply styles during drawing phase like Linux port does.
  // http://crbug.com/110109
  if (!insert_mode_ && cursor_visible() && focused()) {
    StyleRange replacement_mode_style(default_style_);
    replacement_mode_style.foreground = NativeTheme::instance()->GetSystemColor(
        NativeTheme::kColorId_TextfieldSelectionColor);
    size_t cursor = GetCursorPosition();
    replacement_mode_style.range.set_start(cursor);
    replacement_mode_style.range.set_end(
        IndexOfAdjacentGrapheme(cursor, CURSOR_FORWARD));
    ApplyStyleRangeImpl(style_ranges, replacement_mode_style);
  }
}

Point RenderText::GetTextOrigin() {
  Point origin = display_rect().origin();
  origin = origin.Add(GetUpdatedDisplayOffset());
  origin = origin.Add(GetAlignmentOffset());
  return origin;
}

Point RenderText::ToTextPoint(const Point& point) {
  return point.Subtract(GetTextOrigin());
}

Point RenderText::ToViewPoint(const Point& point) {
  return point.Add(GetTextOrigin());
}

int RenderText::GetContentWidth() {
  return GetStringWidth() + (cursor_enabled_ ? 1 : 0);
}

Point RenderText::GetAlignmentOffset() {
  if (horizontal_alignment() != ALIGN_LEFT) {
    int x_offset = display_rect().width() - GetContentWidth();
    if (horizontal_alignment() == ALIGN_CENTER)
      x_offset /= 2;
    return Point(x_offset, 0);
  }
  return Point();
}

Point RenderText::GetOriginForSkiaDrawing() {
  Point origin(GetTextOrigin());
  // TODO(msw): Establish a vertical baseline for strings of mixed font heights.
  const Font& font = GetFont();
  int height = font.GetHeight();
  DCHECK_LE(height, display_rect().height());
  // Center the text vertically in the display area.
  origin.Offset(0, (display_rect().height() - height) / 2);
  // Offset by the font size to account for Skia expecting y to be the bottom.
  origin.Offset(0, font.GetFontSize());
  return origin;
}

void RenderText::ApplyFadeEffects(internal::SkiaTextRenderer* renderer) {
  if (!fade_head() && !fade_tail())
    return;

  const int text_width = GetStringWidth();
  const int display_width = display_rect().width();

  // If the text fits as-is, no need to fade.
  if (text_width <= display_width)
    return;

  int gradient_width = CalculateFadeGradientWidth(GetFont(), display_width);
  if (gradient_width == 0)
    return;

  bool fade_left = fade_head();
  bool fade_right = fade_tail();
  // Under RTL, |fade_right| == |fade_head|.
  if (GetTextDirection() == base::i18n::RIGHT_TO_LEFT)
    std::swap(fade_left, fade_right);

  gfx::Rect solid_part = display_rect();
  gfx::Rect left_part;
  gfx::Rect right_part;
  if (fade_left) {
    left_part = solid_part;
    left_part.Inset(0, 0, solid_part.width() - gradient_width, 0);
    solid_part.Inset(gradient_width, 0, 0, 0);
  }
  if (fade_right) {
    right_part = solid_part;
    right_part.Inset(solid_part.width() - gradient_width, 0, 0, 0);
    solid_part.Inset(0, 0, gradient_width, 0);
  }

  gfx::Rect text_rect = display_rect();
  text_rect.Inset(GetAlignmentOffset().x(), 0, 0, 0);

  const SkColor color = default_style().foreground;
  SkAutoTUnref<SkShader> shader(
      CreateFadeShader(text_rect, left_part, right_part, color));
  if (shader.get()) {
    // |renderer| adds its own ref. So don't |release()| it from the ref ptr.
    renderer->SetShader(shader.get());
  }
}

void RenderText::MoveCursorTo(size_t position, bool select) {
  size_t cursor = std::min(position, text().length());
  size_t caret_pos = IndexOfAdjacentGrapheme(cursor, CURSOR_BACKWARD);
  SelectionModel::CaretPlacement placement = (caret_pos == cursor) ?
      SelectionModel::LEADING : SelectionModel::TRAILING;
  size_t selection_start = select ? GetSelectionStart() : cursor;
  if (IsCursorablePosition(cursor)) {
    SelectionModel sel(selection_start, cursor, caret_pos, placement);
    SetSelectionModel(sel);
  }
}

void RenderText::UpdateCachedBoundsAndOffset() {
  if (cached_bounds_and_offset_valid_)
    return;

  // First, set the valid flag true to calculate the current cursor bounds using
  // the stale |display_offset_|. Applying |delta_offset| at the end of this
  // function will set |cursor_bounds_| and |display_offset_| to correct values.
  cached_bounds_and_offset_valid_ = true;
  cursor_bounds_ = GetCursorBounds(selection_model_, insert_mode_);

  // Update |display_offset_| to ensure the current cursor is visible.
  const int display_width = display_rect_.width();
  const int content_width = GetContentWidth();

  int delta_offset = 0;
  if (content_width <= display_width || !cursor_enabled()) {
    // Don't pan if the text fits in the display width or when the cursor is
    // disabled.
    delta_offset = -display_offset_.x();
  } else if (cursor_bounds_.right() >= display_rect_.right()) {
    // TODO(xji): when the character overflow is a RTL character, currently, if
    // we pan cursor at the rightmost position, the entered RTL character is not
    // displayed. Should pan cursor to show the last logical characters.
    //
    // Pan to show the cursor when it overflows to the right,
    delta_offset = display_rect_.right() - cursor_bounds_.right() - 1;
  } else if (cursor_bounds_.x() < display_rect_.x()) {
    // TODO(xji): have similar problem as above when overflow character is a
    // LTR character.
    //
    // Pan to show the cursor when it overflows to the left.
    delta_offset = display_rect_.x() - cursor_bounds_.x();
  } else if (display_offset_.x() != 0) {
    // Reduce the pan offset to show additional overflow text when the display
    // width increases.
    const int negate_rtl = horizontal_alignment_ == ALIGN_RIGHT ? -1 : 1;
    const int offset = negate_rtl * display_offset_.x();
    if (display_width > (content_width + offset))
      delta_offset = negate_rtl * (display_width - (content_width + offset));
  }

  display_offset_.Offset(delta_offset, 0);
  cursor_bounds_.Offset(delta_offset, 0);
}

void RenderText::DrawSelection(Canvas* canvas) {
  std::vector<Rect> sel = GetSubstringBounds(
      GetSelectionStart(), GetCursorPosition());
  NativeTheme::ColorId color_id = focused() ?
      NativeTheme::kColorId_TextfieldSelectionBackgroundFocused :
      NativeTheme::kColorId_TextfieldSelectionBackgroundUnfocused;
  SkColor color = NativeTheme::instance()->GetSystemColor(color_id);
  for (std::vector<Rect>::const_iterator i = sel.begin(); i < sel.end(); ++i)
    canvas->FillRect(color, *i);
}

void RenderText::DrawCursor(Canvas* canvas) {
  // Paint cursor. Replace cursor is drawn as rectangle for now.
  // TODO(msw): Draw a better cursor with a better indication of association.
  if (cursor_enabled() && cursor_visible() && focused()) {
    const Rect& bounds = GetUpdatedCursorBounds();
    if (bounds.width() != 0)
      canvas->FillRect(kCursorColor, bounds);
    else
      canvas->DrawRect(bounds, kCursorColor);
  }
}

}  // namespace gfx
