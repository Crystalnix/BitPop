// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_GFX_NATIVE_THEME_H_
#define UI_GFX_NATIVE_THEME_H_
#pragma once

#include "skia/ext/platform_canvas.h"
#include "ui/gfx/native_widget_types.h"

namespace gfx {

class Rect;
class Size;

// This class supports drawing UI controls (like buttons, text fields, lists,
// comboboxes, etc) that look like the native UI controls of the underlying
// platform, such as Windows or Linux.
//
// The supported control types are listed in the Part enum.  These parts can be
// in any state given by the State enum, where the actual definititon of the
// state is part-specific.
//
// Some parts require more information than simply the state in order to be
// drawn correctly, and this information is given to the Paint() method via the
// ExtraParams union.  Each part that requires more information has its own
// field in the union.
//
// NativeTheme also supports getting the default size of a given part with
// the GetPartSize() method.
class NativeTheme {
 public:
  // The part to be painted / sized.
  enum Part {
    kScrollbarDownArrow,
    kScrollbarLeftArrow,
    kScrollbarRightArrow,
    kScrollbarUpArrow,
    kScrollbarHorizontalThumb,
    kScrollbarVerticalThumb,
    kScrollbarHorizontalTrack,
    kScrollbarVerticalTrack,
    kCheckbox,
    kRadio,
    kPushButton,
    kTextField,
    kMenuList,
    kMenuCheck,
    kMenuCheckBackground,
    kMenuPopupArrow,
    kMenuPopupBackground,
    kMenuPopupGutter,
    kMenuPopupSeparator,
    kMenuItemBackground,
    kSliderTrack,
    kSliderThumb,
    kInnerSpinButton,
    kProgressBar,
    kMaxPart,
  };

  // The state of the part.
  enum State {
    kDisabled,
    kHovered,
    kNormal,
    kPressed,
    kMaxState,
  };

  // Each structure below hold extra information needed when painting a given
  // part.

  struct ScrollbarTrackExtraParams {
    int track_x;
    int track_y;
    int track_width;
    int track_height;
  };

  struct ButtonExtraParams {
    bool checked;
    bool indeterminate;  // Whether the button state is indeterminate.
    bool is_default;  // Whether the button is default button.
    bool has_border;
    int classic_state;  // Used on Windows when uxtheme is not available.
    SkColor background_color;
  };

  struct TextFieldExtraParams {
    bool is_text_area;
    bool is_listbox;
    SkColor background_color;
  };

  struct MenuArrowExtraParams {
    bool pointing_right;
  };

  struct MenuCheckExtraParams {
    bool is_radio;
  };

  struct MenuItemExtraParams {
    bool is_selected;
  };

  struct MenuListExtraParams {
    bool has_border;
    bool has_border_radius;
    int arrow_x;
    int arrow_y;
    SkColor background_color;
  };

  struct MenuSeparatorExtraParams {
    bool has_gutter;
  };

  struct SliderExtraParams {
    bool vertical;
    bool in_drag;
  };

  struct InnerSpinButtonExtraParams {
    bool spin_up;
    bool read_only;
  };

  struct ProgressBarExtraParams {
    bool determinate;
    int value_rect_x;
    int value_rect_y;
    int value_rect_width;
    int value_rect_height;
  };

  union ExtraParams {
    ScrollbarTrackExtraParams scrollbar_track;
    ButtonExtraParams button;
    MenuArrowExtraParams menu_arrow;
    MenuCheckExtraParams menu_check;
    MenuItemExtraParams menu_item;
    MenuListExtraParams menu_list;
    MenuSeparatorExtraParams menu_separator;
    SliderExtraParams slider;
    TextFieldExtraParams text_field;
    InnerSpinButtonExtraParams inner_spin;
    ProgressBarExtraParams progress_bar;
  };

  // Return the size of the part.
  virtual Size GetPartSize(Part part) const = 0;

  // Paint the part to the canvas.
  virtual void Paint(SkCanvas* canvas,
                     Part part,
                     State state,
                     const gfx::Rect& rect,
                     const ExtraParams& extra) const = 0;

  // Supports theme specific colors.
  void SetScrollbarColors(unsigned inactive_color,
                          unsigned active_color,
                          unsigned track_color) const;

  // Returns a shared instance of the native theme.
  // The retuned object should not be deleted by the caller.  This function
  // is not thread safe and should only be called from the UI thread.
  static const NativeTheme* instance();

 protected:
  NativeTheme() {}
  virtual ~NativeTheme() {}

  static unsigned int thumb_inactive_color_;
  static unsigned int thumb_active_color_;
  static unsigned int track_color_;

  DISALLOW_COPY_AND_ASSIGN(NativeTheme);
};

}  // namespace gfx

#endif  // UI_GFX_NATIVE_THEME_H_
