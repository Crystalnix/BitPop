// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VIEWS_CONTROLS_LABEL_H_
#define VIEWS_CONTROLS_LABEL_H_
#pragma once

#include "base/gtest_prod_util.h"
#include "googleurl/src/gurl.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/font.h"
#include "views/view.h"

namespace views {

/////////////////////////////////////////////////////////////////////////////
//
// Label class
//
// A label is a view subclass that can display a string.
//
/////////////////////////////////////////////////////////////////////////////
class Label : public View {
 public:
  enum Alignment { ALIGN_LEFT = 0,
                   ALIGN_CENTER,
                   ALIGN_RIGHT };

  // The following enum is used to indicate whether using the Chrome UI's
  // alignment as the label's alignment, or autodetecting the label's
  // alignment.
  //
  // If the label text originates from the Chrome UI, we should use the Chrome
  // UI's alignment as the label's alignment.
  //
  // If the text originates from a web page, the text's alignment is determined
  // based on the first character with strong directionality, disregarding what
  // directionality the Chrome UI is. And its alignment will not be flipped
  // around in RTL locales.
  enum RTLAlignmentMode {
    USE_UI_ALIGNMENT = 0,
    AUTO_DETECT_ALIGNMENT
  };

  // The view class name.
  static const char kViewClassName[];

  // The padding for the focus border when rendering focused text.
  static const int kFocusBorderPadding;

  Label();
  explicit Label(const std::wstring& text);
  Label(const std::wstring& text, const gfx::Font& font);
  virtual ~Label();

  // Set the font.
  virtual void SetFont(const gfx::Font& font);

  // Set the label text.
  void SetText(const std::wstring& text);

  // Return the font used by this label.
  gfx::Font font() const { return font_; }

  // Return the label text.
  const std::wstring GetText() const;

  // Set URL Value - text_ is set to spec().
  void SetURL(const GURL& url);

  // Return the label URL.
  const GURL GetURL() const;

  // Set the color
  virtual void SetColor(const SkColor& color);

  // Return a reference to the currently used color.
  virtual SkColor GetColor() const;

  // Set horizontal alignment. If the locale is RTL, and the RTL alignment
  // setting is set as USE_UI_ALIGNMENT, the alignment is flipped around.
  //
  // Caveat: for labels originating from a web page, the RTL alignment mode
  // should be reset to AUTO_DETECT_ALIGNMENT before the horizontal alignment
  // is set. Otherwise, the label's alignment specified as a parameter will be
  // flipped in RTL locales. Please see the comments in SetRTLAlignmentMode for
  // more information.
  void SetHorizontalAlignment(Alignment alignment);

  Alignment horizontal_alignment() const { return horiz_alignment_; }

  // Set the RTL alignment mode. The RTL alignment mode is initialized to
  // USE_UI_ALIGNMENT when the label is constructed. USE_UI_ALIGNMENT applies
  // to every label that originates from the Chrome UI. However, if the label
  // originates from a web page, its alignment should not be flipped around for
  // RTL locales. For such labels, we need to set the RTL alignment mode to
  // AUTO_DETECT_ALIGNMENT so that subsequent SetHorizontalAlignment() calls
  // will not flip the label's alignment around.
  void set_rtl_alignment_mode(RTLAlignmentMode mode) {
    rtl_alignment_mode_ = mode;
  }
  RTLAlignmentMode rtl_alignment_mode() const { return rtl_alignment_mode_; }

  // Set whether the label text can wrap on multiple lines.
  // Default is false.
  void SetMultiLine(bool multi_line);

  // Return whether the label text can wrap on multiple lines.
  bool is_multi_line() const { return is_multi_line_; }

  // Set whether the label text can be split on words.
  // Default is false. This only works when is_multi_line is true.
  void SetAllowCharacterBreak(bool allow_character_break);

  // Set whether the label text should be elided in the middle (if necessary).
  // The default is to elide at the end.
  // NOTE: This is not supported for multi-line strings.
  void SetElideInMiddle(bool elide_in_middle);

  // Sets the tooltip text.  Default behavior for a label (single-line) is to
  // show the full text if it is wider than its bounds.  Calling this overrides
  // the default behavior and lets you set a custom tooltip.  To revert to
  // default behavior, call this with an empty string.
  void SetTooltipText(const std::wstring& tooltip_text);

  // The background color to use when the mouse is over the label. Label
  // takes ownership of the Background.
  void SetMouseOverBackground(Background* background);
  const Background* GetMouseOverBackground() const;

  // Resizes the label so its width is set to the width of the longest line and
  // its height deduced accordingly.
  // This is only intended for multi-line labels and is useful when the label's
  // text contains several lines separated with \n.
  // |max_width| is the maximum width that will be used (longer lines will be
  // wrapped).  If 0, no maximum width is enforced.
  void SizeToFit(int max_width);

  // Gets/sets the flag to determine whether the label should be collapsed when
  // it's hidden (not visible). If this flag is true, the label will return a
  // preferred size of (0, 0) when it's not visible.
  void set_collapse_when_hidden(bool value) { collapse_when_hidden_ = value; }
  bool collapse_when_hidden() const { return collapse_when_hidden_; }

  // Gets/set whether or not this label is to be painted as a focused element.
  void set_paint_as_focused(bool paint_as_focused) {
    paint_as_focused_ = paint_as_focused;
  }
  bool paint_as_focused() const { return paint_as_focused_; }

  void SetHasFocusBorder(bool has_focus_border);

  // Overridden from View:
  virtual gfx::Insets GetInsets() const;
  virtual int GetBaseline() OVERRIDE;
  // Overridden to compute the size required to display this label.
  virtual gfx::Size GetPreferredSize() OVERRIDE;
  // Return the height necessary to display this label with the provided width.
  // This method is used to layout multi-line labels. It is equivalent to
  // GetPreferredSize().height() if the receiver is not multi-line.
  virtual int GetHeightForWidth(int w);
  // Sets the enabled state. Setting the enabled state resets the color.
  virtual void SetEnabled(bool enabled) OVERRIDE;
  virtual std::string GetClassName() const OVERRIDE;
  // Mouse enter/exit are overridden to render mouse over background color.
  // These invoke SetContainsMouse as necessary.
  virtual void OnMouseMoved(const MouseEvent& event) OVERRIDE;
  virtual void OnMouseEntered(const MouseEvent& event) OVERRIDE;
  virtual void OnMouseExited(const MouseEvent& event) OVERRIDE;
  // Gets the tooltip text for labels that are wider than their bounds, except
  // when the label is multiline, in which case it just returns false (no
  // tooltip).  If a custom tooltip has been specified with SetTooltipText()
  // it is returned instead.
  virtual bool GetTooltipText(const gfx::Point& p, std::wstring* tooltip);
  virtual void GetAccessibleState(ui::AccessibleViewState* state) OVERRIDE;

 protected:
  // Called by Paint to paint the text.  Override this to change how
  // text is painted.
  virtual void PaintText(gfx::Canvas* canvas,
                         const std::wstring& text,
                         const gfx::Rect& text_bounds,
                         int flags);

  void invalidate_text_size() { text_size_valid_ = false; }

  virtual gfx::Size GetTextSize() const;

  // Overridden from View:
  // Overridden to dirty our text bounds if we're multi-line.
  virtual void OnBoundsChanged(const gfx::Rect& previous_bounds) OVERRIDE;
  virtual void OnPaint(gfx::Canvas* canvas) OVERRIDE;
  // If the mouse is over the label, and a mouse over background has been
  // specified, its used. Otherwise super's implementation is invoked.
  virtual void OnPaintBackground(gfx::Canvas* canvas) OVERRIDE;

 private:
  // These tests call CalculateDrawStringParams in order to verify the
  // calculations done for drawing text.
  FRIEND_TEST_ALL_PREFIXES(LabelTest, DrawSingleLineString);
  FRIEND_TEST_ALL_PREFIXES(LabelTest, DrawMultiLineString);
  FRIEND_TEST_ALL_PREFIXES(LabelTest, DrawSingleLineStringInRTL);
  FRIEND_TEST_ALL_PREFIXES(LabelTest, DrawMultiLineStringInRTL);

  static gfx::Font GetDefaultFont();

  void Init(const std::wstring& text, const gfx::Font& font);

  // If the mouse is over the text, SetContainsMouse(true) is invoked, otherwise
  // SetContainsMouse(false) is invoked.
  void UpdateContainsMouse(const MouseEvent& event);

  // Updates whether the mouse is contained in the Label. If the new value
  // differs from the current value, and a mouse over background is specified,
  // SchedulePaint is invoked.
  void SetContainsMouse(bool contains_mouse);

  // Returns where the text is drawn, in the receivers coordinate system.
  gfx::Rect GetTextBounds() const;

  int ComputeMultiLineFlags() const;

  gfx::Rect GetAvailableRect() const;

  // Returns parameters to be used for the DrawString call.
  void CalculateDrawStringParams(std::wstring* paint_text,
                                 gfx::Rect* text_bounds,
                                 int* flags) const;

  // The colors to use for enabled and disabled labels.
  static SkColor kEnabledColor, kDisabledColor;

  string16 text_;
  GURL url_;
  gfx::Font font_;
  SkColor color_;
  mutable gfx::Size text_size_;
  mutable bool text_size_valid_;
  bool is_multi_line_;
  bool allow_character_break_;
  bool elide_in_middle_;
  bool url_set_;
  Alignment horiz_alignment_;
  string16 tooltip_text_;
  // Whether the mouse is over this label.
  bool contains_mouse_;
  scoped_ptr<Background> mouse_over_background_;
  // Whether to collapse the label when it's not visible.
  bool collapse_when_hidden_;
  // The following member variable is used to control whether the alignment
  // needs to be flipped around for RTL locales. Please refer to the definition
  // of RTLAlignmentMode for more information.
  RTLAlignmentMode rtl_alignment_mode_;
  // When embedded in a larger control that is focusable, setting this flag
  // allows this view to be painted as focused even when it is itself not.
  bool paint_as_focused_;
  // When embedded in a larger control that is focusable, setting this flag
  // allows this view to reserve space for a focus border that it otherwise
  // might not have because it is not itself focusable.
  bool has_focus_border_;

  DISALLOW_COPY_AND_ASSIGN(Label);
};

}  // namespace views

#endif  // VIEWS_CONTROLS_LABEL_H_
