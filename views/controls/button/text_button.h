// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VIEWS_CONTROLS_BUTTON_TEXT_BUTTON_H_
#define VIEWS_CONTROLS_BUTTON_TEXT_BUTTON_H_
#pragma once

#include <string>

// TODO(avi): remove when not needed
#include "base/utf_string_conversions.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/font.h"
#include "views/border.h"
#include "views/controls/button/custom_button.h"
#include "views/native_theme_delegate.h"

namespace views {

////////////////////////////////////////////////////////////////////////////////
//
// TextButtonBorder
//
//  A Border subclass that paints a TextButton's background layer -
//  basically the button frame in the hot/pushed states.
//
// Note that this type of button is not focusable by default and will not be
// part of the focus chain.  Call SetFocusable(true) to make it part of the
// focus chain.
//
////////////////////////////////////////////////////////////////////////////////
class TextButtonBorder : public Border {
 public:
  TextButtonBorder();
  virtual ~TextButtonBorder();

  // Implementation of Border:
  virtual void Paint(const View& view, gfx::Canvas* canvas) const;
  virtual void GetInsets(gfx::Insets* insets) const;

 protected:
  // Images
  struct MBBImageSet {
    SkBitmap* top_left;
    SkBitmap* top;
    SkBitmap* top_right;
    SkBitmap* left;
    SkBitmap* center;
    SkBitmap* right;
    SkBitmap* bottom_left;
    SkBitmap* bottom;
    SkBitmap* bottom_right;
  };
  MBBImageSet hot_set_;
  MBBImageSet pushed_set_;

  virtual void Paint(const View& view, gfx::Canvas* canvas,
      const MBBImageSet& set) const;

 private:
  DISALLOW_COPY_AND_ASSIGN(TextButtonBorder);
};


////////////////////////////////////////////////////////////////////////////////
//
// TextButtonNativeThemeBorder
//
//  A Border subclass that paints a TextButton's background layer using the
//  platform's native theme look.  This handles normal/disabled/hot/pressed
//  states, with possible animation between states.
//
////////////////////////////////////////////////////////////////////////////////
class TextButtonNativeThemeBorder : public Border {
 public:
   TextButtonNativeThemeBorder(NativeThemeDelegate* delegate);
  virtual ~TextButtonNativeThemeBorder();

  // Implementation of Border:
  virtual void Paint(const View& view, gfx::Canvas* canvas) const;
  virtual void GetInsets(gfx::Insets* insets) const;

 private:
  // The delegate the controls the appearance of this border.
  NativeThemeDelegate* delegate_;

  DISALLOW_COPY_AND_ASSIGN(TextButtonNativeThemeBorder);
};


////////////////////////////////////////////////////////////////////////////////
//
// TextButtonBase
//
//  A base ckass for different types of buttons, like push buttons, radio
//  buttons, and checkboxes, that do not depende on native components for
//  look and feel. TextButton reserves space for the largest string
//  passed to SetText. To reset the cached max size invoke ClearMaxTextSize.
//
////////////////////////////////////////////////////////////////////////////////
class TextButtonBase : public CustomButton, public NativeThemeDelegate {
 public:
  // The menu button's class name.
  static const char kViewClassName[];

  // Enumeration of how the prefix ('&') character is processed. The default
  // is |PREFIX_NONE|.
  enum PrefixType {
    // No special processing is done.
    PREFIX_NONE,

    // The character following the prefix character is not rendered specially.
    PREFIX_HIDE,

    // The character following the prefix character is underlined.
    PREFIX_SHOW
  };

  virtual ~TextButtonBase();

  // Call SetText once per string in your set of possible values at button
  // creation time, so that it can contain the largest of them and avoid
  // resizing the button when the text changes.
  virtual void SetText(const std::wstring& text);
  std::wstring text() const { return UTF16ToWideHack(text_); }

  enum TextAlignment {
    ALIGN_LEFT,
    ALIGN_CENTER,
    ALIGN_RIGHT
  };

  void set_alignment(TextAlignment alignment) { alignment_ = alignment; }

  void set_prefix_type(PrefixType type) { prefix_type_ = type; }

  const ui::Animation* GetAnimation() const;

  void SetIsDefault(bool is_default);
  bool is_default() const { return is_default_; }

  // Set whether the button text can wrap on multiple lines.
  // Default is false.
  void SetMultiLine(bool multi_line);

  // Return whether the button text can wrap on multiple lines.
  bool multi_line() const { return multi_line_; }

  // TextButton remembers the maximum display size of the text passed to
  // SetText. This method resets the cached maximum display size to the
  // current size.
  void ClearMaxTextSize();

  void set_max_width(int max_width) { max_width_ = max_width; }
  void SetFont(const gfx::Font& font);
  // Return the font used by this button.
  gfx::Font font() const { return font_; }

  void SetEnabledColor(SkColor color);
  void SetDisabledColor(SkColor color);
  void SetHighlightColor(SkColor color);
  void SetHoverColor(SkColor color);
  void SetTextHaloColor(SkColor color);
  // The shadow color used is determined by whether the widget is active or
  // inactive. Both possible colors are set in this method, and the
  // appropriate one is chosen during Paint.
  void SetTextShadowColors(SkColor active_color, SkColor inactive_color);
  void SetTextShadowOffset(int x, int y);

  bool normal_has_border() const { return normal_has_border_; }
  void SetNormalHasBorder(bool normal_has_border);

  // Sets whether or not to show the hot and pushed states for the button icon
  // (if present) in addition to the normal state.  Defaults to true.
  bool show_multiple_icon_states() const { return show_multiple_icon_states_; }
  void SetShowMultipleIconStates(bool show_multiple_icon_states);

  // Clears halo and shadow settings.
  void ClearEmbellishing();

  // Paint the button into the specified canvas. If |mode| is |PB_FOR_DRAG|, the
  // function paints a drag image representation into the canvas.
  enum PaintButtonMode { PB_NORMAL, PB_FOR_DRAG };
  virtual void PaintButton(gfx::Canvas* canvas, PaintButtonMode mode);

  // Overridden from View:
  virtual gfx::Size GetPreferredSize() OVERRIDE;
  virtual gfx::Size GetMinimumSize() OVERRIDE;
  virtual void OnEnabledChanged() OVERRIDE;
  virtual void OnPaint(gfx::Canvas* canvas) OVERRIDE;

  // Text colors.
  static const SkColor kEnabledColor;
  static const SkColor kHighlightColor;
  static const SkColor kDisabledColor;
  static const SkColor kHoverColor;

  // Returns views/TextButton.
  virtual std::string GetClassName() const;

 protected:
  TextButtonBase(ButtonListener* listener, const std::wstring& text);

  // Called when enabled or disabled state changes, or the colors for those
  // states change.
  virtual void UpdateColor();

  // Updates text_size_ and max_text_size_ from the current text/font. This is
  // invoked when the font or text changes.
  void UpdateTextSize();

  // Overridden from NativeThemeDelegate:
  virtual gfx::Rect GetThemePaintRect() const OVERRIDE;
  virtual gfx::NativeTheme::State GetThemeState(
      gfx::NativeTheme::ExtraParams* params) const OVERRIDE;
  virtual const ui::Animation* GetThemeAnimation() const OVERRIDE;
  virtual gfx::NativeTheme::State GetBackgroundThemeState(
      gfx::NativeTheme::ExtraParams* params) const OVERRIDE;
  virtual gfx::NativeTheme::State GetForegroundThemeState(
      gfx::NativeTheme::ExtraParams* params) const OVERRIDE;

  virtual void GetExtraParams(gfx::NativeTheme::ExtraParams* params) const;

  virtual gfx::Rect GetTextBounds() const;

  int ComputeCanvasStringFlags() const;

  // Calculate the bounds of the content of this button, including any extra
  // width needed on top of the text width.
  gfx::Rect GetContentBounds(int extra_width) const;

  // The text string that is displayed in the button.
  string16 text_;

  // The size of the text string.
  gfx::Size text_size_;

  // Track the size of the largest text string seen so far, so that
  // changing text_ will not resize the button boundary.
  gfx::Size max_text_size_;

  // The alignment of the text string within the button.
  TextAlignment alignment_;

  // The font used to paint the text.
  gfx::Font font_;

  // Text color.
  SkColor color_;

  // State colors.
  SkColor color_enabled_;
  SkColor color_disabled_;
  SkColor color_highlight_;
  SkColor color_hover_;

  // An optional halo around text.
  SkColor text_halo_color_;
  bool has_text_halo_;

  // Optional shadow text colors for active and inactive widget states.
  SkColor active_text_shadow_color_;
  SkColor inactive_text_shadow_color_;
  bool has_shadow_;
  // Space between text and shadow. Defaults to (1,1).
  gfx::Point shadow_offset_;

  // The width of the button will never be larger than this value. A value <= 0
  // indicates the width is not constrained.
  int max_width_;

  // This is true if normal state has a border frame; default is false.
  bool normal_has_border_;

  // Whether or not to show the hot and pushed icon states.
  bool show_multiple_icon_states_;

  // Whether or not the button appears and behaves as the default button in its
  // current context.
  bool is_default_;

  // Whether the text button should handle its text string as multi-line.
  bool multi_line_;

  PrefixType prefix_type_;

  DISALLOW_COPY_AND_ASSIGN(TextButtonBase);
};

////////////////////////////////////////////////////////////////////////////////
//
// TextButton
//
//  A button which displays text and/or and icon that can be changed in
//  response to actions. TextButton reserves space for the largest string
//  passed to SetText. To reset the cached max size invoke ClearMaxTextSize.
//
////////////////////////////////////////////////////////////////////////////////
class TextButton : public TextButtonBase {
 public:
  // The button's class name.
  static const char kViewClassName[];

  TextButton(ButtonListener* listener, const std::wstring& text);
  virtual ~TextButton();

  void set_icon_text_spacing(int icon_text_spacing) {
    icon_text_spacing_ = icon_text_spacing;
  }

  // Sets the icon.
  void SetIcon(const SkBitmap& icon);
  void SetHoverIcon(const SkBitmap& icon);
  void SetPushedIcon(const SkBitmap& icon);

  bool HasIcon() const { return !icon_.empty(); }

  // Meanings are reversed for right-to-left layouts.
  enum IconPlacement {
    ICON_ON_LEFT,
    ICON_ON_RIGHT
  };

  IconPlacement icon_placement() { return icon_placement_; }
  void set_icon_placement(IconPlacement icon_placement) {
    icon_placement_ = icon_placement;
  }

  // Overridden from View:
  virtual gfx::Size GetPreferredSize() OVERRIDE;
  virtual std::string GetClassName() const OVERRIDE;

  // Overridden from TextButtonBase:
  virtual void PaintButton(gfx::Canvas* canvas, PaintButtonMode mode) OVERRIDE;

 protected:
  SkBitmap icon() const { return icon_; }

  // Overridden from NativeThemeDelegate:
  virtual gfx::NativeTheme::Part GetThemePart() const OVERRIDE;

  // Overridden from TextButtonBase:
  virtual void GetExtraParams(
      gfx::NativeTheme::ExtraParams* params) const OVERRIDE;
  virtual gfx::Rect GetTextBounds() const OVERRIDE;

 private:
  // The position of the icon.
  IconPlacement icon_placement_;

  // An icon displayed with the text.
  SkBitmap icon_;

  // An optional different version of the icon for hover state.
  SkBitmap icon_hover_;
  bool has_hover_icon_;

  // An optional different version of the icon for pushed state.
  SkBitmap icon_pushed_;
  bool has_pushed_icon_;

  // Space between icon and text.
  int icon_text_spacing_;

  DISALLOW_COPY_AND_ASSIGN(TextButton);
};

}  // namespace views

#endif  // VIEWS_CONTROLS_BUTTON_TEXT_BUTTON_H_
