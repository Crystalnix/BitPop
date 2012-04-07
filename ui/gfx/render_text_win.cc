// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gfx/render_text_win.h"

#include <algorithm>

#include "base/i18n/break_iterator.h"
#include "base/logging.h"
#include "base/stl_util.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "base/win/scoped_hdc.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/canvas_skia.h"
#include "ui/gfx/platform_font.h"

namespace {

// The maximum supported number of Uniscribe runs; a SCRIPT_ITEM is 8 bytes.
// TODO(msw): Review memory use/failure? Max string length? Alternate approach?
const int kGuessItems = 100;
const int kMaxItems = 10000;

// The maximum supported number of Uniscribe glyphs; a glyph is 1 word.
// TODO(msw): Review memory use/failure? Max string length? Alternate approach?
const int kMaxGlyphs = 100000;

// Callback to |EnumEnhMetaFile()| to intercept font creation.
int CALLBACK MetaFileEnumProc(HDC hdc,
                              HANDLETABLE* table,
                              CONST ENHMETARECORD* record,
                              int table_entries,
                              LPARAM log_font) {
  if (record->iType == EMR_EXTCREATEFONTINDIRECTW) {
    const EMREXTCREATEFONTINDIRECTW* create_font_record =
        reinterpret_cast<const EMREXTCREATEFONTINDIRECTW*>(record);
    *reinterpret_cast<LOGFONT*>(log_font) = create_font_record->elfw.elfLogFont;
  }
  return 1;
}

// Finds a fallback font to use to render the specified |text| with respect to
// an initial |font|. Returns the resulting font via out param |result|. Returns
// |true| if a fallback font was found.
// Adapted from WebKit's |FontCache::GetFontDataForCharacters()|.
bool ChooseFallbackFont(HDC hdc,
                        const gfx::Font& font,
                        const wchar_t* text,
                        int text_length,
                        gfx::Font* result) {
  // Use a meta file to intercept the fallback font chosen by Uniscribe.
  HDC meta_file_dc = CreateEnhMetaFile(hdc, NULL, NULL, NULL);
  if (!meta_file_dc)
    return false;

  SelectObject(meta_file_dc, font.GetNativeFont());

  SCRIPT_STRING_ANALYSIS script_analysis;
  HRESULT hresult =
      ScriptStringAnalyse(meta_file_dc, text, text_length, 0, -1,
                          SSA_METAFILE | SSA_FALLBACK | SSA_GLYPHS | SSA_LINK,
                          0, NULL, NULL, NULL, NULL, NULL, &script_analysis);

  if (SUCCEEDED(hresult)) {
    hresult = ScriptStringOut(script_analysis, 0, 0, 0, NULL, 0, 0, FALSE);
    ScriptStringFree(&script_analysis);
  }

  bool found_fallback = false;
  HENHMETAFILE meta_file = CloseEnhMetaFile(meta_file_dc);
  if (SUCCEEDED(hresult)) {
    LOGFONT log_font;
    log_font.lfFaceName[0] = 0;
    EnumEnhMetaFile(0, meta_file, MetaFileEnumProc, &log_font, NULL);
    if (log_font.lfFaceName[0]) {
      int font_style = font.GetStyle();
      *result = gfx::Font(UTF16ToUTF8(log_font.lfFaceName), font.GetFontSize());
      if (result->GetStyle() != font_style)
        *result = result->DeriveFont(0, font_style);
      found_fallback = true;
    }
  }
  DeleteEnhMetaFile(meta_file);

  return found_fallback;
}

}  // namespace

namespace gfx {

namespace internal {

TextRun::TextRun()
  : strike(false),
    underline(false),
    width(0),
    preceding_run_widths(0),
    glyph_count(0),
    script_cache(NULL) {
  memset(&script_analysis, 0, sizeof(script_analysis));
  memset(&abc_widths, 0, sizeof(abc_widths));
}

TextRun::~TextRun() {
  ScriptFreeCache(&script_cache);
}

}  // namespace internal

RenderTextWin::RenderTextWin()
    : RenderText(),
      script_control_(),
      script_state_(),
      string_width_(0),
      needs_layout_(false) {
  // Omitting default constructors for script_* would leave POD uninitialized.
  HRESULT hr = 0;

  // TODO(msw): Call ScriptRecordDigitSubstitution on WM_SETTINGCHANGE message.
  // TODO(msw): Use Chrome/profile locale/language settings?
  hr = ScriptRecordDigitSubstitution(LOCALE_USER_DEFAULT, &digit_substitute_);
  DCHECK(SUCCEEDED(hr));

  hr = ScriptApplyDigitSubstitution(&digit_substitute_,
                                    &script_control_,
                                    &script_state_);
  DCHECK(SUCCEEDED(hr));
  script_control_.fMergeNeutralItems = true;

  MoveCursorTo(EdgeSelectionModel(CURSOR_LEFT));
}

RenderTextWin::~RenderTextWin() {
  STLDeleteContainerPointers(runs_.begin(), runs_.end());
}

base::i18n::TextDirection RenderTextWin::GetTextDirection() {
  // TODO(benrg): Code moved from RenderText::GetTextDirection. Needs to be
  // replaced by a correct Windows implementation.
  if (base::i18n::IsRTL())
    return base::i18n::RIGHT_TO_LEFT;
  return base::i18n::LEFT_TO_RIGHT;
}

int RenderTextWin::GetStringWidth() {
  EnsureLayout();
  return string_width_;
}

SelectionModel RenderTextWin::FindCursorPosition(const Point& point) {
  if (text().empty())
    return SelectionModel();

  EnsureLayout();
  // Find the run that contains the point and adjust the argument location.
  Point p(ToTextPoint(point));
  size_t run_index = GetRunContainingPoint(p);
  if (run_index == runs_.size())
    return EdgeSelectionModel((p.x() < 0) ? CURSOR_LEFT : CURSOR_RIGHT);
  internal::TextRun* run = runs_[run_index];

  int position = 0, trailing = 0;
  HRESULT hr = ScriptXtoCP(p.x() - run->preceding_run_widths,
                           run->range.length(),
                           run->glyph_count,
                           run->logical_clusters.get(),
                           run->visible_attributes.get(),
                           run->advance_widths.get(),
                           &(run->script_analysis),
                           &position,
                           &trailing);
  DCHECK(SUCCEEDED(hr));
  position += run->range.start();

  size_t cursor = position + trailing;
  DCHECK_GE(cursor, 0U);
  DCHECK_LE(cursor, text().length());
  return SelectionModel(cursor, position,
      (trailing > 0) ? SelectionModel::TRAILING : SelectionModel::LEADING);
}

Rect RenderTextWin::GetCursorBounds(const SelectionModel& selection,
                                    bool insert_mode) {
  EnsureLayout();

  // Highlight the logical cursor (selection end) when not in insert mode.
  size_t pos = insert_mode ? selection.caret_pos() : selection.selection_end();
  size_t run_index = GetRunContainingPosition(pos);
  internal::TextRun* run = run_index == runs_.size() ? NULL : runs_[run_index];

  int start_x = 0, end_x = 0;
  if (run) {
    HRESULT hr = 0;
    hr = ScriptCPtoX(pos - run->range.start(),
                     false,
                     run->range.length(),
                     run->glyph_count,
                     run->logical_clusters.get(),
                     run->visible_attributes.get(),
                     run->advance_widths.get(),
                     &(run->script_analysis),
                     &start_x);
    DCHECK(SUCCEEDED(hr));
    hr = ScriptCPtoX(pos - run->range.start(),
                     true,
                     run->range.length(),
                     run->glyph_count,
                     run->logical_clusters.get(),
                     run->visible_attributes.get(),
                     run->advance_widths.get(),
                     &(run->script_analysis),
                     &end_x);
    DCHECK(SUCCEEDED(hr));
  }
  // TODO(msw): Use the last visual run's font instead of the default font?
  int height = run ? run->font.GetHeight() : GetFont().GetHeight();
  Rect rect(std::min(start_x, end_x), 0, std::abs(end_x - start_x), height);
  // Offset to the run start or the right/left end for an out of bounds index.
  // Also center the rect vertically in the display area.
  int text_end_offset = base::i18n::IsRTL() ? 0 : GetStringWidth();
  rect.Offset((run ? run->preceding_run_widths : text_end_offset),
              (display_rect().height() - rect.height()) / 2);
  // Adjust for leading/trailing in insert mode.
  if (insert_mode && run) {
    bool leading = selection.caret_placement() == SelectionModel::LEADING;
    // Adjust the x value for right-side placement.
    if (run->script_analysis.fRTL == leading)
      rect.set_x(rect.right());
    rect.set_width(0);
  }
  rect.set_origin(ToViewPoint(rect.origin()));
  return rect;
}

SelectionModel RenderTextWin::AdjacentCharSelectionModel(
    const SelectionModel& selection, VisualCursorDirection direction) {
  DCHECK(!needs_layout_);
  size_t caret = selection.caret_pos();
  SelectionModel::CaretPlacement caret_placement = selection.caret_placement();
  size_t run_index = GetRunContainingPosition(caret);
  DCHECK(run_index < runs_.size());
  internal::TextRun* run = runs_[run_index];

  bool forward_motion = run->script_analysis.fRTL == (direction == CURSOR_LEFT);
  if (forward_motion) {
    if (caret_placement == SelectionModel::LEADING) {
      size_t cursor = IndexOfAdjacentGrapheme(caret, CURSOR_FORWARD);
      return SelectionModel(cursor, caret, SelectionModel::TRAILING);
    } else if (selection.selection_end() < run->range.end()) {
      caret = IndexOfAdjacentGrapheme(caret, CURSOR_FORWARD);
      size_t cursor = IndexOfAdjacentGrapheme(caret, CURSOR_FORWARD);
      return SelectionModel(cursor, caret, SelectionModel::TRAILING);
    }
  } else {
    if (caret_placement == SelectionModel::TRAILING)
      return SelectionModel(caret, caret, SelectionModel::LEADING);
    else if (caret > run->range.start()) {
      caret = IndexOfAdjacentGrapheme(caret, CURSOR_BACKWARD);
      return SelectionModel(caret, caret, SelectionModel::LEADING);
    }
  }

  // The character is at the beginning/end of its run; go to the previous/next
  // visual run.
  size_t visual_index = logical_to_visual_[run_index];
  if (visual_index == (direction == CURSOR_LEFT ? 0 : runs_.size() - 1))
    return EdgeSelectionModel(direction);
  internal::TextRun* adjacent = runs_[visual_to_logical_[
      direction == CURSOR_LEFT ? visual_index - 1 : visual_index + 1]];
  forward_motion = adjacent->script_analysis.fRTL == (direction == CURSOR_LEFT);
  return forward_motion ? FirstSelectionModelInsideRun(adjacent) :
                          LastSelectionModelInsideRun(adjacent);
}

// TODO(msw): Implement word breaking for Windows.
SelectionModel RenderTextWin::AdjacentWordSelectionModel(
    const SelectionModel& selection,
    VisualCursorDirection direction) {
  base::i18n::BreakIterator iter(text(), base::i18n::BreakIterator::BREAK_WORD);
  bool success = iter.Init();
  DCHECK(success);
  if (!success)
    return selection;

  size_t pos;
  if (direction == CURSOR_RIGHT) {
    pos = std::min(selection.selection_end() + 1, text().length());
    while (iter.Advance()) {
      pos = iter.pos();
      if (iter.IsWord() && pos > selection.selection_end())
        break;
    }
  } else {  // direction == CURSOR_LEFT
    // Notes: We always iterate words from the beginning.
    // This is probably fast enough for our usage, but we may
    // want to modify WordIterator so that it can start from the
    // middle of string and advance backwards.
    pos = std::max<int>(selection.selection_end() - 1, 0);
    while (iter.Advance()) {
      if (iter.IsWord()) {
        size_t begin = iter.pos() - iter.GetString().length();
        if (begin == selection.selection_end()) {
          // The cursor is at the beginning of a word.
          // Move to previous word.
          break;
        } else if (iter.pos() >= selection.selection_end()) {
          // The cursor is in the middle or at the end of a word.
          // Move to the top of current word.
          pos = begin;
          break;
        } else {
          pos = iter.pos() - iter.GetString().length();
        }
      }
    }
  }
  return SelectionModel(pos, pos, SelectionModel::LEADING);
}

SelectionModel RenderTextWin::EdgeSelectionModel(
    VisualCursorDirection direction) {
  if (text().empty())
    return SelectionModel(0, 0, SelectionModel::LEADING);

  EnsureLayout();
  size_t cursor = (direction == GetVisualDirectionOfLogicalEnd()) ?
      text().length() : 0;
  internal::TextRun* run = runs_[
      visual_to_logical_[direction == CURSOR_RIGHT ? runs_.size() - 1 : 0]];
  size_t caret;
  SelectionModel::CaretPlacement placement;
  if (run->script_analysis.fRTL == (direction == CURSOR_RIGHT)) {
    caret = run->range.start();
    placement = SelectionModel::LEADING;
  } else {
    caret = IndexOfAdjacentGrapheme(run->range.end(), CURSOR_BACKWARD);
    placement = SelectionModel::TRAILING;
  }
  return SelectionModel(cursor, caret, placement);
}

std::vector<Rect> RenderTextWin::GetSubstringBounds(size_t from, size_t to) {
  DCHECK(!needs_layout_);
  ui::Range range(from, to);
  DCHECK(ui::Range(0, text().length()).Contains(range));
  Point display_offset(GetUpdatedDisplayOffset());
  HRESULT hr = 0;

  std::vector<Rect> bounds;
  if (from == to)
    return bounds;

  // Add a Rect for each run/selection intersection.
  // TODO(msw): The bounds should probably not always be leading the range ends.
  for (size_t i = 0; i < runs_.size(); ++i) {
    internal::TextRun* run = runs_[visual_to_logical_[i]];
    ui::Range intersection = run->range.Intersect(range);
    if (intersection.IsValid()) {
      DCHECK(!intersection.is_reversed());
      int start_offset = 0;
      hr = ScriptCPtoX(intersection.start() - run->range.start(),
                       false,
                       run->range.length(),
                       run->glyph_count,
                       run->logical_clusters.get(),
                       run->visible_attributes.get(),
                       run->advance_widths.get(),
                       &(run->script_analysis),
                       &start_offset);
      DCHECK(SUCCEEDED(hr));
      int end_offset = 0;
      hr = ScriptCPtoX(intersection.end() - run->range.start(),
                       false,
                       run->range.length(),
                       run->glyph_count,
                       run->logical_clusters.get(),
                       run->visible_attributes.get(),
                       run->advance_widths.get(),
                       &(run->script_analysis),
                       &end_offset);
      DCHECK(SUCCEEDED(hr));
      if (start_offset > end_offset)
        std::swap(start_offset, end_offset);
      Rect rect(run->preceding_run_widths + start_offset, 0,
                end_offset - start_offset, run->font.GetHeight());
      // Center the rect vertically in the display area.
      rect.Offset(0, (display_rect().height() - rect.height()) / 2);
      rect.set_origin(ToViewPoint(rect.origin()));
      // Union this with the last rect if they're adjacent.
      if (!bounds.empty() && rect.SharesEdgeWith(bounds.back())) {
        rect = rect.Union(bounds.back());
        bounds.pop_back();
      }
      bounds.push_back(rect);
    }
  }
  return bounds;
}

void RenderTextWin::SetSelectionModel(const SelectionModel& model) {
  RenderText::SetSelectionModel(model);
  // TODO(xji): The styles are applied to text inside ItemizeLogicalText(). So,
  // we need to update layout here in order for the styles, such as selection
  // foreground, to be picked up. Eventually, we should separate styles from
  // layout by applying foreground, strike, and underline styles during
  // DrawVisualText as what RenderTextLinux does.
  UpdateLayout();
}

bool RenderTextWin::IsCursorablePosition(size_t position) {
  if (position == 0 || position == text().length())
    return true;

  EnsureLayout();
  size_t run_index = GetRunContainingPosition(position);
  if (run_index >= runs_.size())
    return false;

  internal::TextRun* run = runs_[run_index];
  size_t start = run->range.start();
  if (position == start)
    return true;
  return run->logical_clusters[position - start] !=
         run->logical_clusters[position - start - 1];
}

void RenderTextWin::UpdateLayout() {
  // Layout is performed lazily as needed for drawing/metrics.
  needs_layout_ = true;
}

void RenderTextWin::EnsureLayout() {
  if (!needs_layout_)
    return;
  // TODO(msw): Skip complex processing if ScriptIsComplex returns false.
  ItemizeLogicalText();
  if (!runs_.empty())
    LayoutVisualText();
  needs_layout_ = false;
}

void RenderTextWin::DrawVisualText(Canvas* canvas) {
  DCHECK(!needs_layout_);

  Point offset(GetOriginForSkiaDrawing());
  SkScalar x = SkIntToScalar(offset.x());
  SkScalar y = SkIntToScalar(offset.y());

  std::vector<SkPoint> pos;

  internal::SkiaTextRenderer renderer(canvas);
  ApplyFadeEffects(&renderer);

  for (size_t i = 0; i < runs_.size(); ++i) {
    // Get the run specified by the visual-to-logical map.
    internal::TextRun* run = runs_[visual_to_logical_[i]];

    // Based on WebCore::skiaDrawText.
    pos.resize(run->glyph_count);
    SkScalar glyph_x = x;
    for (int glyph = 0; glyph < run->glyph_count; glyph++) {
      pos[glyph].set(glyph_x + run->offsets[glyph].du,
                     y + run->offsets[glyph].dv);
      glyph_x += SkIntToScalar(run->advance_widths[glyph]);
    }

    renderer.SetFont(run->font);
    renderer.SetForegroundColor(run->foreground);
    renderer.DrawPosText(&pos[0], run->glyphs.get(), run->glyph_count);
    // TODO(oshima|msw): Consider refactoring StyleRange into Style
    // class and StyleRange containing Style, and use Style class in
    // TextRun class.  This may conflict with msw's comment in
    // TextRun, so please consult with msw when refactoring.
    StyleRange style;
    style.strike = run->strike;
    style.diagonal_strike = run->diagonal_strike;
    style.underline = run->underline;
    renderer.DrawDecorations(x, y, run->width, style);

    x = glyph_x;
  }
}

size_t RenderTextWin::IndexOfAdjacentGrapheme(
    size_t index,
    LogicalCursorDirection direction) {
  EnsureLayout();

  if (text().empty())
    return 0;

  if (index >= text().length()) {
    if (direction == CURSOR_FORWARD || index > text().length()) {
      return text().length();
    } else {
      // The requested |index| is at the end of the text. Use the index of the
      // last character to find the grapheme.
      index = text().length() - 1;
      if (IsCursorablePosition(index))
        return index;
    }
  }

  size_t run_index = GetRunContainingPosition(index);
  DCHECK(run_index < runs_.size());
  internal::TextRun* run = runs_[run_index];
  size_t start = run->range.start();
  size_t ch = index - start;

  if (direction == CURSOR_BACKWARD) {
    // If |ch| is the start of the run, use the preceding run, if any.
    if (ch == 0) {
      if (run_index == 0)
        return 0;
      run = runs_[run_index - 1];
      start = run->range.start();
      ch = run->range.length();
    }

    // Loop to find the start of the grapheme.
    WORD cluster = run->logical_clusters[ch - 1];
    do {
      ch--;
    } while (ch > 0 && run->logical_clusters[ch - 1] == cluster);
  } else {  // direction == CURSOR_FORWARD
    WORD cluster = run->logical_clusters[ch];
    while (ch < run->range.length() && run->logical_clusters[ch] == cluster)
      ch++;
  }

  return start + ch;
}

void RenderTextWin::ItemizeLogicalText() {
  STLDeleteContainerPointers(runs_.begin(), runs_.end());
  runs_.clear();
  string_width_ = 0;
  if (text().empty())
    return;

  const wchar_t* raw_text = text().c_str();
  const int text_length = text().length();

  HRESULT hr = E_OUTOFMEMORY;
  int script_items_count = 0;
  std::vector<SCRIPT_ITEM> script_items;
  for (size_t n = kGuessItems; hr == E_OUTOFMEMORY && n < kMaxItems; n *= 2) {
    // Derive the array of Uniscribe script items from the logical text.
    // ScriptItemize always adds a terminal array item so that the length of the
    // last item can be derived from the terminal SCRIPT_ITEM::iCharPos.
    script_items.resize(n);
    hr = ScriptItemize(raw_text,
                       text_length,
                       n - 1,
                       &script_control_,
                       &script_state_,
                       &script_items[0],
                       &script_items_count);
  }
  DCHECK(SUCCEEDED(hr));

  if (script_items_count <= 0)
    return;

  // Build the list of runs, merge font/underline styles.
  // TODO(msw): Only break for font changes, not color etc. See TextRun comment.
  StyleRanges styles(style_ranges());
  ApplyCompositionAndSelectionStyles(&styles);
  StyleRanges::const_iterator style = styles.begin();
  SCRIPT_ITEM* script_item = &script_items[0];
  for (int run_break = 0; run_break < text_length;) {
    internal::TextRun* run = new internal::TextRun();
    run->range.set_start(run_break);
    run->font = GetFont().DeriveFont(0, style->font_style);
    run->foreground = style->foreground;
    run->strike = style->strike;
    run->diagonal_strike = style->diagonal_strike;
    run->underline = style->underline;
    run->script_analysis = script_item->a;

    // Find the range end and advance the structures as needed.
    int script_item_end = (script_item + 1)->iCharPos;
    int style_range_end = style->range.end();
    run_break = std::min(script_item_end, style_range_end);
    if (script_item_end <= style_range_end)
      script_item++;
    if (script_item_end >= style_range_end)
      style++;
    run->range.set_end(run_break);
    runs_.push_back(run);
  }
}

void RenderTextWin::LayoutVisualText() {
  HRESULT hr = E_FAIL;
  base::win::ScopedCreateDC hdc(CreateCompatibleDC(NULL));
  std::vector<internal::TextRun*>::const_iterator run_iter;
  for (run_iter = runs_.begin(); run_iter < runs_.end(); ++run_iter) {
    internal::TextRun* run = *run_iter;
    size_t run_length = run->range.length();
    const wchar_t* run_text = &(text()[run->range.start()]);
    bool tried_fallback = false;

    // Select the font desired for glyph generation.
    SelectObject(hdc, run->font.GetNativeFont());

    run->logical_clusters.reset(new WORD[run_length]);
    run->glyph_count = 0;
    // Max glyph guess: http://msdn.microsoft.com/en-us/library/dd368564.aspx
    size_t max_glyphs = static_cast<size_t>(1.5 * run_length + 16);
    while (max_glyphs < kMaxGlyphs) {
      run->glyphs.reset(new WORD[max_glyphs]);
      run->visible_attributes.reset(new SCRIPT_VISATTR[max_glyphs]);
      hr = ScriptShape(hdc,
                       &run->script_cache,
                       run_text,
                       run_length,
                       max_glyphs,
                       &(run->script_analysis),
                       run->glyphs.get(),
                       run->logical_clusters.get(),
                       run->visible_attributes.get(),
                       &(run->glyph_count));
      if (hr == E_OUTOFMEMORY) {
        max_glyphs *= 2;
      } else if (hr == USP_E_SCRIPT_NOT_IN_FONT) {
         // Only try font fallback if it hasn't yet been attempted for this run.
         if (tried_fallback) {
           // TODO(msw): Don't use SCRIPT_UNDEFINED. Apparently Uniscribe can
           //            crash on certain surrogate pairs with SCRIPT_UNDEFINED.
           //            See https://bugzilla.mozilla.org/show_bug.cgi?id=341500
           //            And http://maxradi.us/documents/uniscribe/
           run->script_analysis.eScript = SCRIPT_UNDEFINED;
           // Reset |hr| to 0 to not trigger the DCHECK() below when a font is
           // not found that can display the text. This is expected behavior
           // under Windows XP without additional language packs installed and
           // may also happen on newer versions when trying to display text in
           // an obscure script that the system doesn't have the right font for.
           hr = 0;
           break;
         }

        // The run's font doesn't contain the required glyphs, use an alternate.
        // TODO(msw): support RenderText's font_list().
        if (ChooseFallbackFont(hdc, run->font, run_text, run_length,
                               &run->font)) {
          ScriptFreeCache(&run->script_cache);
          SelectObject(hdc, run->font.GetNativeFont());
        }

        tried_fallback = true;
      } else {
        break;
      }
    }
    DCHECK(SUCCEEDED(hr));

    if (run->glyph_count > 0) {
      run->advance_widths.reset(new int[run->glyph_count]);
      run->offsets.reset(new GOFFSET[run->glyph_count]);
      hr = ScriptPlace(hdc,
                       &run->script_cache,
                       run->glyphs.get(),
                       run->glyph_count,
                       run->visible_attributes.get(),
                       &(run->script_analysis),
                       run->advance_widths.get(),
                       run->offsets.get(),
                       &(run->abc_widths));
      DCHECK(SUCCEEDED(hr));
    }
  }

  if (runs_.size() > 0) {
    // Build the array of bidirectional embedding levels.
    scoped_array<BYTE> levels(new BYTE[runs_.size()]);
    for (size_t i = 0; i < runs_.size(); ++i)
      levels[i] = runs_[i]->script_analysis.s.uBidiLevel;

    // Get the maps between visual and logical run indices.
    visual_to_logical_.reset(new int[runs_.size()]);
    logical_to_visual_.reset(new int[runs_.size()]);
    hr = ScriptLayout(runs_.size(),
                      levels.get(),
                      visual_to_logical_.get(),
                      logical_to_visual_.get());
    DCHECK(SUCCEEDED(hr));
  }

  // Precalculate run width information.
  size_t preceding_run_widths = 0;
  for (size_t i = 0; i < runs_.size(); ++i) {
    internal::TextRun* run = runs_[visual_to_logical_[i]];
    run->preceding_run_widths = preceding_run_widths;
    const ABC& abc = run->abc_widths;
    run->width = abc.abcA + abc.abcB + abc.abcC;
    preceding_run_widths += run->width;
  }
  string_width_ = preceding_run_widths;
}

size_t RenderTextWin::GetRunContainingPosition(size_t position) const {
  DCHECK(!needs_layout_);
  // Find the text run containing the argument position.
  size_t run = 0;
  for (; run < runs_.size(); ++run)
    if (runs_[run]->range.start() <= position &&
        runs_[run]->range.end() > position)
      break;
  return run;
}

size_t RenderTextWin::GetRunContainingPoint(const Point& point) const {
  DCHECK(!needs_layout_);
  // Find the text run containing the argument point (assumed already offset).
  size_t run = 0;
  for (; run < runs_.size(); ++run)
    if (runs_[run]->preceding_run_widths <= point.x() &&
        runs_[run]->preceding_run_widths + runs_[run]->width > point.x())
      break;
  return run;
}

SelectionModel RenderTextWin::FirstSelectionModelInsideRun(
    internal::TextRun* run) {
  size_t caret = run->range.start();
  size_t cursor = IndexOfAdjacentGrapheme(caret, CURSOR_FORWARD);
  return SelectionModel(cursor, caret, SelectionModel::TRAILING);
}

SelectionModel RenderTextWin::LastSelectionModelInsideRun(
    internal::TextRun* run) {
  size_t caret = IndexOfAdjacentGrapheme(run->range.end(), CURSOR_BACKWARD);
  return SelectionModel(caret, caret, SelectionModel::LEADING);
}

RenderText* RenderText::CreateRenderText() {
  return new RenderTextWin;
}

}  // namespace gfx
