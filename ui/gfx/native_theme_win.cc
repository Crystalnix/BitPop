// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gfx/native_theme_win.h"

#include <windows.h>
#include <uxtheme.h>
#include <vsstyle.h>
#include <vssym32.h>

#include "base/logging.h"
#include "base/memory/scoped_handle.h"
#include "base/win/scoped_gdi_object.h"
#include "base/win/scoped_hdc.h"
#include "base/win/windows_version.h"
#include "skia/ext/skia_utils_win.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkShader.h"
#include "ui/gfx/gdi_util.h"
#include "ui/gfx/rect.h"

namespace {

void SetCheckerboardShader(SkPaint* paint, const RECT& align_rect) {
  // Create a 2x2 checkerboard pattern using the 3D face and highlight colors.
  SkColor face = skia::COLORREFToSkColor(GetSysColor(COLOR_3DFACE));
  SkColor highlight = skia::COLORREFToSkColor(GetSysColor(COLOR_3DHILIGHT));
  SkColor buffer[] = { face, highlight, highlight, face };
  // Confusing bit: we first create a temporary bitmap with our desired pattern,
  // then copy it to another bitmap.  The temporary bitmap doesn't take
  // ownership of the pixel data, and so will point to garbage when this
  // function returns.  The copy will copy the pixel data into a place owned by
  // the bitmap, which is in turn owned by the shader, etc., so it will live
  // until we're done using it.
  SkBitmap temp_bitmap;
  temp_bitmap.setConfig(SkBitmap::kARGB_8888_Config, 2, 2);
  temp_bitmap.setPixels(buffer);
  SkBitmap bitmap;
  temp_bitmap.copyTo(&bitmap, temp_bitmap.config());
  SkShader* shader = SkShader::CreateBitmapShader(bitmap,
                                                  SkShader::kRepeat_TileMode,
                                                  SkShader::kRepeat_TileMode);

  // Align the pattern with the upper corner of |align_rect|.
  SkMatrix matrix;
  matrix.setTranslate(SkIntToScalar(align_rect.left),
                      SkIntToScalar(align_rect.top));
  shader->setLocalMatrix(matrix);
  SkSafeUnref(paint->setShader(shader));
}

}  // namespace

namespace gfx {

// static
const NativeTheme* NativeTheme::instance() {
  return NativeThemeWin::instance();
}

// static
const NativeThemeWin* NativeThemeWin::instance() {
  // The global NativeThemeWin instance.
  static const NativeThemeWin s_native_theme;
  return &s_native_theme;
}

NativeThemeWin::NativeThemeWin()
    : theme_dll_(LoadLibrary(L"uxtheme.dll")),
      draw_theme_(NULL),
      draw_theme_ex_(NULL),
      get_theme_color_(NULL),
      get_theme_content_rect_(NULL),
      get_theme_part_size_(NULL),
      open_theme_(NULL),
      close_theme_(NULL),
      set_theme_properties_(NULL),
      is_theme_active_(NULL),
      get_theme_int_(NULL) {
  if (theme_dll_) {
    draw_theme_ = reinterpret_cast<DrawThemeBackgroundPtr>(
        GetProcAddress(theme_dll_, "DrawThemeBackground"));
    draw_theme_ex_ = reinterpret_cast<DrawThemeBackgroundExPtr>(
        GetProcAddress(theme_dll_, "DrawThemeBackgroundEx"));
    get_theme_color_ = reinterpret_cast<GetThemeColorPtr>(
        GetProcAddress(theme_dll_, "GetThemeColor"));
    get_theme_content_rect_ = reinterpret_cast<GetThemeContentRectPtr>(
        GetProcAddress(theme_dll_, "GetThemeBackgroundContentRect"));
    get_theme_part_size_ = reinterpret_cast<GetThemePartSizePtr>(
        GetProcAddress(theme_dll_, "GetThemePartSize"));
    open_theme_ = reinterpret_cast<OpenThemeDataPtr>(
        GetProcAddress(theme_dll_, "OpenThemeData"));
    close_theme_ = reinterpret_cast<CloseThemeDataPtr>(
        GetProcAddress(theme_dll_, "CloseThemeData"));
    set_theme_properties_ = reinterpret_cast<SetThemeAppPropertiesPtr>(
        GetProcAddress(theme_dll_, "SetThemeAppProperties"));
    is_theme_active_ = reinterpret_cast<IsThemeActivePtr>(
        GetProcAddress(theme_dll_, "IsThemeActive"));
    get_theme_int_ = reinterpret_cast<GetThemeIntPtr>(
        GetProcAddress(theme_dll_, "GetThemeInt"));
  }
  memset(theme_handles_, 0, sizeof(theme_handles_));
}

NativeThemeWin::~NativeThemeWin() {
  if (theme_dll_) {
    // todo (cpu): fix this soon.  Making a call to CloseHandles() here breaks
    // certain tests and the reliability bots.
    //CloseHandles();
    FreeLibrary(theme_dll_);
  }
}

gfx::Size NativeThemeWin::GetPartSize(Part part,
                                      State state,
                                      const ExtraParams& extra) const {
  SIZE size;
  int part_id = GetWindowsPart(part, state, extra);
  int state_id = GetWindowsState(part, state, extra);

  HDC hdc = GetDC(NULL);
  HRESULT hr = GetThemePartSize(GetThemeName(part), hdc, part_id, state_id,
                                NULL, TS_TRUE, &size);
  ReleaseDC(NULL, hdc);

  return SUCCEEDED(hr) ? Size(size.cx, size.cy) : Size();
}

void NativeThemeWin::Paint(SkCanvas* canvas,
                           Part part,
                           State state,
                           const gfx::Rect& rect,
                           const ExtraParams& extra) const {
  if (!skia::SupportsPlatformPaint(canvas)) {
    // TODO(alokp): Implement this path.
    // This block will only get hit with --enable-accelerated-drawing flag.
    DLOG(INFO) << "Could not paint native UI control";
    return;
  }

  skia::ScopedPlatformPaint scoped_platform_paint(canvas);
  HDC hdc = scoped_platform_paint.GetPlatformSurface();

  switch (part) {
    case kCheckbox:
      PaintCheckbox(hdc, part, state, rect, extra.button);
      break;
    case kRadio:
      PaintRadioButton(hdc, part, state, rect, extra.button);
      break;
    case kPushButton:
      PaintPushButton(hdc, part, state, rect, extra.button);
      break;
    case kMenuPopupArrow:
      PaintMenuArrow(hdc, state, rect, extra.menu_arrow);
      break;
    case kMenuPopupGutter:
      PaintMenuGutter(hdc, rect);
      break;
    case kMenuPopupSeparator:
      PaintMenuSeparator(hdc, rect, extra.menu_separator);
      break;
    case kMenuPopupBackground:
      PaintMenuBackground(hdc, rect);
      break;
    case kMenuCheck:
      PaintMenuCheck(hdc, state, rect, extra.menu_check);
      break;
    case kMenuCheckBackground:
      PaintMenuCheckBackground(hdc, state, rect);
      break;
    case kMenuItemBackground:
      PaintMenuItemBackground(hdc, state, rect, extra.menu_item);
      break;
    case kMenuList:
      PaintMenuList(hdc, state, rect, extra.menu_list);
      break;
    case kScrollbarDownArrow:
    case kScrollbarUpArrow:
    case kScrollbarLeftArrow:
    case kScrollbarRightArrow:
      PaintScrollbarArrow(hdc, part, state, rect, extra.scrollbar_arrow);
      break;
    case kScrollbarHorizontalTrack:
    case kScrollbarVerticalTrack:
      PaintScrollbarTrack(canvas, hdc, part, state, rect,
                          extra.scrollbar_track);
      break;
    case kScrollbarHorizontalThumb:
    case kScrollbarVerticalThumb:
    case kScrollbarHorizontalGripper:
    case kScrollbarVerticalGripper:
      PaintScrollbarThumb(hdc, part, state, rect, extra.scrollbar_thumb);
      break;
    case kInnerSpinButton:
      PaintSpinButton(hdc, part, state, rect, extra.inner_spin);
      break;
    case kTrackbarThumb:
    case kTrackbarTrack:
      PaintTrackbar(canvas, hdc, part, state, rect, extra.trackbar);
      break;
    case kProgressBar:
      PaintProgressBar(hdc, rect, extra.progress_bar);
      break;
    case kWindowResizeGripper:
      PaintWindowResizeGripper(hdc, rect);
      break;
    case kTabPanelBackground:
      PaintTabPanelBackground(hdc, rect);
      break;
    case kTextField:
      PaintTextField(hdc, part, state, rect, extra.text_field);
      break;

    case kSliderTrack:
    case kSliderThumb:
    default:
      // While transitioning NativeThemeWin to the single Paint() entry point,
      // unsupported parts will DCHECK here.
      DCHECK(false);
  }
}

HRESULT NativeThemeWin::PaintScrollbarArrow(
    HDC hdc,
    Part part,
    State state,
    const gfx::Rect& rect,
    const ScrollbarArrowExtraParams& extra) const {
  static int state_id_matrix[4][kMaxState] = {
      ABS_DOWNDISABLED, ABS_DOWNHOT, ABS_DOWNNORMAL, ABS_DOWNPRESSED,
      ABS_LEFTDISABLED, ABS_LEFTHOT, ABS_LEFTNORMAL, ABS_LEFTPRESSED,
      ABS_RIGHTDISABLED, ABS_RIGHTHOT, ABS_RIGHTNORMAL, ABS_RIGHTPRESSED,
      ABS_UPDISABLED, ABS_UPHOT, ABS_UPNORMAL, ABS_UPPRESSED
  };
  HANDLE handle = GetThemeHandle(SCROLLBAR);
  RECT rect_win = rect.ToRECT();
  if (handle && draw_theme_) {
    int index = part - kScrollbarDownArrow;
    DCHECK(index >=0 && index < 4);
    int state_id = state_id_matrix[index][state];

    // Hovering means that the cursor is over the scroolbar, but not over the
    // specific arrow itself.  We don't want to show it "hot" mode, but only
    // in "hover" mode.
    if (state == kHovered && extra.is_hovering) {
      switch(part) {
        case kScrollbarDownArrow:
          state_id = ABS_DOWNHOVER;
          break;
        case kScrollbarLeftArrow:
          state_id = ABS_LEFTHOVER;
          break;
        case kScrollbarRightArrow:
          state_id = ABS_RIGHTHOVER;
          break;
        case kScrollbarUpArrow:
          state_id = ABS_UPHOVER;
          break;
        default:
          NOTREACHED() << "Invalid part: " << part;
          break;
      }
    }

    return draw_theme_(handle, hdc, SBP_ARROWBTN, state_id, &rect_win, NULL);
  }

  int classic_state = DFCS_SCROLLDOWN;
  switch(part) {
    case kScrollbarDownArrow:
      classic_state = DFCS_SCROLLDOWN;
      break;
    case kScrollbarLeftArrow:
      classic_state = DFCS_SCROLLLEFT;
      break;
    case kScrollbarRightArrow:
      classic_state = DFCS_SCROLLRIGHT;
      break;
    case kScrollbarUpArrow:
      classic_state = DFCS_SCROLLUP;
      break;
    default:
      NOTREACHED() << "Invalid part: " << part;
      break;
  }

  DrawFrameControl(hdc, &rect_win, DFC_SCROLL, classic_state);
  return S_OK;
}

HRESULT NativeThemeWin::PaintScrollbarTrack(
    SkCanvas* canvas,
    HDC hdc,
    Part part,
    State state,
    const gfx::Rect& rect,
    const ScrollbarTrackExtraParams& extra) const {
  HANDLE handle = GetThemeHandle(SCROLLBAR);
  RECT rect_win = rect.ToRECT();
  int part_id;
  int state_id;

  switch(part) {
    case gfx::NativeTheme::kScrollbarHorizontalTrack:
      part_id = extra.is_upper ? SBP_UPPERTRACKHORZ : SBP_LOWERTRACKHORZ;
      break;
    case gfx::NativeTheme::kScrollbarVerticalTrack:
      part_id = extra.is_upper ? SBP_UPPERTRACKVERT : SBP_LOWERTRACKVERT;
      break;
    default:
      NOTREACHED() << "Invalid part: " << part;
      break;
  }

  switch(state) {
    case kDisabled:
      state_id = SCRBS_DISABLED;
      break;
    case kHovered:
      state_id = SCRBS_HOVER;
      break;
    case kNormal:
      state_id = SCRBS_NORMAL;
      break;
    case kPressed:
      state_id = SCRBS_PRESSED;
      break;
    default:
      NOTREACHED() << "Invalid state: " << state;
      break;
  }

  if (handle && draw_theme_)
    return draw_theme_(handle, hdc, part_id, state_id, &rect_win, NULL);

  // Draw it manually.
  const DWORD colorScrollbar = GetSysColor(COLOR_SCROLLBAR);
  const DWORD color3DFace = GetSysColor(COLOR_3DFACE);
  if ((colorScrollbar != color3DFace) &&
      (colorScrollbar != GetSysColor(COLOR_WINDOW))) {
    FillRect(hdc, &rect_win, reinterpret_cast<HBRUSH>(COLOR_SCROLLBAR + 1));
  } else {
    SkPaint paint;
    RECT align_rect = gfx::Rect(extra.track_x, extra.track_y, extra.track_width,
                                extra.track_height).ToRECT();
    SetCheckerboardShader(&paint, align_rect);
    canvas->drawIRect(skia::RECTToSkIRect(rect_win), paint);
  }
  if (extra.classic_state & DFCS_PUSHED)
    InvertRect(hdc, &rect_win);
  return S_OK;
}

HRESULT NativeThemeWin::PaintScrollbarThumb(
    HDC hdc,
    Part part,
    State state,
    const gfx::Rect& rect,
    const ScrollbarThumbExtraParams& extra) const {
  HANDLE handle = GetThemeHandle(SCROLLBAR);
  RECT rect_win = rect.ToRECT();
  int part_id;
  int state_id;

  switch(part) {
    case gfx::NativeTheme::kScrollbarHorizontalThumb:
      part_id = SBP_THUMBBTNHORZ;
      break;
    case gfx::NativeTheme::kScrollbarVerticalThumb:
      part_id = SBP_THUMBBTNVERT;
      break;
    case gfx::NativeTheme::kScrollbarHorizontalGripper:
      part_id = SBP_GRIPPERHORZ;
      break;
    case gfx::NativeTheme::kScrollbarVerticalGripper:
      part_id = SBP_GRIPPERVERT;
      break;
    default:
      NOTREACHED() << "Invalid part: " << part;
      break;
  }

  switch(state) {
    case kDisabled:
      state_id = SCRBS_DISABLED;
      break;
    case kHovered:
      state_id = extra.is_hovering ? SCRBS_HOVER : SCRBS_HOT;
      break;
    case kNormal:
      state_id = SCRBS_NORMAL;
      break;
    case kPressed:
      state_id = SCRBS_PRESSED;
      break;
    default:
      NOTREACHED() << "Invalid state: " << state;
      break;
  }

  if (handle && draw_theme_)
    return draw_theme_(handle, hdc, part_id, state_id, &rect_win, NULL);

  // Draw it manually.
  if ((part_id == SBP_THUMBBTNHORZ) || (part_id == SBP_THUMBBTNVERT))
    DrawEdge(hdc, &rect_win, EDGE_RAISED, BF_RECT | BF_MIDDLE);
  // Classic mode doesn't have a gripper.
  return S_OK;
}

HRESULT NativeThemeWin::PaintPushButton(HDC hdc,
                                        Part part,
                                        State state,
                                        const gfx::Rect& rect,
                                        const ButtonExtraParams& extra) const {
  int state_id;
  switch(state) {
    case kDisabled:
      state_id = PBS_DISABLED;
      break;
    case kHovered:
      state_id = PBS_HOT;
      break;
    case kNormal:
      state_id = extra.is_default ? PBS_DEFAULTED : PBS_NORMAL;
      break;
    case kPressed:
      state_id = PBS_PRESSED;
      break;
    default:
      NOTREACHED() << "Invalid state: " << state;
      break;
  }

  RECT rect_win = rect.ToRECT();
  return PaintButton(hdc, BP_PUSHBUTTON, state_id, extra.classic_state,
                     &rect_win);
}

HRESULT NativeThemeWin::PaintRadioButton(HDC hdc,
                                         Part part,
                                         State state,
                                         const gfx::Rect& rect,
                                         const ButtonExtraParams& extra) const {
  int state_id;
  switch(state) {
    case kDisabled:
      state_id = extra.checked ? RBS_CHECKEDDISABLED : RBS_UNCHECKEDDISABLED;
      break;
    case kHovered:
      state_id = extra.checked ? RBS_CHECKEDHOT : RBS_UNCHECKEDHOT;
      break;
    case kNormal:
      state_id = extra.checked ? RBS_CHECKEDNORMAL : RBS_UNCHECKEDNORMAL;
      break;
    case kPressed:
      state_id = extra.checked ? RBS_CHECKEDPRESSED : RBS_UNCHECKEDPRESSED;
      break;
    default:
      NOTREACHED() << "Invalid state: " << state;
      break;
  }

  RECT rect_win = rect.ToRECT();
  return PaintButton(hdc, BP_RADIOBUTTON, state_id, extra.classic_state,
                     &rect_win);
}

HRESULT NativeThemeWin::PaintCheckbox(HDC hdc,
                                      Part part,
                                      State state,
                                      const gfx::Rect& rect,
                                      const ButtonExtraParams& extra) const {
  int state_id;
  switch(state) {
    case kDisabled:
      state_id = extra.checked ? CBS_CHECKEDDISABLED :
          extra.indeterminate ? CBS_MIXEDDISABLED :
              CBS_UNCHECKEDDISABLED;
      break;
    case kHovered:
      state_id = extra.checked ? CBS_CHECKEDHOT :
          extra.indeterminate ? CBS_MIXEDHOT :
              CBS_UNCHECKEDHOT;
      break;
    case kNormal:
      state_id = extra.checked ? CBS_CHECKEDNORMAL :
          extra.indeterminate ? CBS_MIXEDNORMAL :
              CBS_UNCHECKEDNORMAL;
      break;
    case kPressed:
      state_id = extra.checked ? CBS_CHECKEDPRESSED :
          extra.indeterminate ? CBS_MIXEDPRESSED :
              CBS_UNCHECKEDPRESSED;
      break;
    default:
      NOTREACHED() << "Invalid state: " << state;
      break;
  }

  RECT rect_win = rect.ToRECT();
  return PaintButton(hdc, BP_CHECKBOX, state_id, extra.classic_state,
                     &rect_win);
}

HRESULT NativeThemeWin::PaintButton(HDC hdc,
                                    int part_id,
                                    int state_id,
                                    int classic_state,
                                    RECT* rect) const {
  HANDLE handle = GetThemeHandle(BUTTON);
  if (handle && draw_theme_)
    return draw_theme_(handle, hdc, part_id, state_id, rect, NULL);

  // Draw it manually.
  // All pressed states have both low bits set, and no other states do.
  const bool focused = ((state_id & ETS_FOCUSED) == ETS_FOCUSED);
  const bool pressed = ((state_id & PBS_PRESSED) == PBS_PRESSED);
  if ((BP_PUSHBUTTON == part_id) && (pressed || focused)) {
    // BP_PUSHBUTTON has a focus rect drawn around the outer edge, and the
    // button itself is shrunk by 1 pixel.
    HBRUSH brush = GetSysColorBrush(COLOR_3DDKSHADOW);
    if (brush) {
      FrameRect(hdc, rect, brush);
      InflateRect(rect, -1, -1);
    }
  }
  DrawFrameControl(hdc, rect, DFC_BUTTON, classic_state);

  // Draw the focus rectangle (the dotted line box) only on buttons.  For radio
  // and checkboxes, we let webkit draw the focus rectangle (orange glow).
  if ((BP_PUSHBUTTON == part_id) && focused) {
    // The focus rect is inside the button.  The exact number of pixels depends
    // on whether we're in classic mode or using uxtheme.
    if (handle && get_theme_content_rect_) {
      get_theme_content_rect_(handle, hdc, part_id, state_id, rect, rect);
    } else {
      InflateRect(rect, -GetSystemMetrics(SM_CXEDGE),
                  -GetSystemMetrics(SM_CYEDGE));
    }
    DrawFocusRect(hdc, rect);
  }

  return S_OK;
}

HRESULT NativeThemeWin::PaintMenuArrow(HDC hdc,
                                       State state,
                                       const gfx::Rect& rect,
                                       const MenuArrowExtraParams& extra)
    const {
  int state_id = MSM_NORMAL;
  if (state == kDisabled)
    state_id = MSM_DISABLED;

  HANDLE handle = GetThemeHandle(MENU);
  RECT rect_win = rect.ToRECT();
  if (handle && draw_theme_) {
    if (extra.pointing_right) {
      return draw_theme_(handle, hdc, MENU_POPUPSUBMENU, state_id, &rect_win,
                         NULL);
    } else {
      // There is no way to tell the uxtheme API to draw a left pointing arrow;
      // it doesn't have a flag equivalent to DFCS_MENUARROWRIGHT.  But they
      // are needed for RTL locales on Vista.  So use a memory DC and mirror
      // the region with GDI's StretchBlt.
      Rect r(rect);
      base::win::ScopedHDC mem_dc(CreateCompatibleDC(hdc));
      base::win::ScopedBitmap mem_bitmap(CreateCompatibleBitmap(hdc, r.width(),
                                                                r.height()));
      HGDIOBJ old_bitmap = SelectObject(mem_dc, mem_bitmap);
      // Copy and horizontally mirror the background from hdc into mem_dc. Use
      // a negative-width source rect, starting at the rightmost pixel.
      StretchBlt(mem_dc, 0, 0, r.width(), r.height(),
                 hdc, r.right()-1, r.y(), -r.width(), r.height(), SRCCOPY);
      // Draw the arrow.
      RECT theme_rect = {0, 0, r.width(), r.height()};
      HRESULT result = draw_theme_(handle, mem_dc, MENU_POPUPSUBMENU,
                                   state_id, &theme_rect, NULL);
      // Copy and mirror the result back into mem_dc.
      StretchBlt(hdc, r.x(), r.y(), r.width(), r.height(),
                 mem_dc, r.width()-1, 0, -r.width(), r.height(), SRCCOPY);
      SelectObject(mem_dc, old_bitmap);
      return result;
    }
  }

  // For some reason, Windows uses the name DFCS_MENUARROWRIGHT to indicate a
  // left pointing arrow. This makes the following 'if' statement slightly
  // counterintuitive.
  UINT pfc_state;
  if (extra.pointing_right)
    pfc_state = DFCS_MENUARROW;
  else
    pfc_state = DFCS_MENUARROWRIGHT;
  return PaintFrameControl(hdc, rect, DFC_MENU, pfc_state, extra.is_selected,
                           state);
}

HRESULT NativeThemeWin::PaintMenuBackground(HDC hdc,
                                            const gfx::Rect& rect) const {
  HANDLE handle = GetThemeHandle(MENU);
  RECT rect_win = rect.ToRECT();
  if (handle && draw_theme_) {
    HRESULT result = draw_theme_(handle, hdc, MENU_POPUPBACKGROUND, 0,
                                 &rect_win, NULL);
    FrameRect(hdc, &rect_win, GetSysColorBrush(COLOR_3DSHADOW));
    return result;
  }

  FillRect(hdc, &rect_win, GetSysColorBrush(COLOR_MENU));
  DrawEdge(hdc, &rect_win, EDGE_RAISED, BF_RECT);
  return S_OK;
}

HRESULT NativeThemeWin::PaintMenuCheckBackground(HDC hdc,
                                                 State state,
                                                 const gfx::Rect& rect) const {
  HANDLE handle = GetThemeHandle(MENU);
  int state_id = state == kDisabled ? MCB_DISABLED : MCB_NORMAL;
  RECT rect_win = rect.ToRECT();
  if (handle && draw_theme_)
    return draw_theme_(handle, hdc, MENU_POPUPCHECKBACKGROUND, state_id,
                       &rect_win, NULL);
  // Nothing to do for background.
  return S_OK;
}

HRESULT NativeThemeWin::PaintMenuCheck(
    HDC hdc,
    State state,
    const gfx::Rect& rect,
    const MenuCheckExtraParams& extra) const {
  HANDLE handle = GetThemeHandle(MENU);
  int state_id;
  if (extra.is_radio) {
    state_id = state == kDisabled ? MC_BULLETDISABLED : MC_BULLETNORMAL;
  } else {
    state_id = state == kDisabled ? MC_CHECKMARKDISABLED : MC_CHECKMARKNORMAL;
  }

  RECT rect_win = rect.ToRECT();
  if (handle && draw_theme_)
    return draw_theme_(handle, hdc, MENU_POPUPCHECK, state_id, &rect_win, NULL);

  return PaintFrameControl(hdc, rect, DFC_MENU, DFCS_MENUCHECK,
                           extra.is_selected, state);
}

HRESULT NativeThemeWin::PaintMenuGutter(HDC hdc,
                                        const gfx::Rect& rect) const {
  RECT rect_win = rect.ToRECT();
  HANDLE handle = GetThemeHandle(MENU);
  if (handle && draw_theme_)
    return draw_theme_(handle, hdc, MENU_POPUPGUTTER, MPI_NORMAL, &rect_win,
                       NULL);
  return E_NOTIMPL;
}

HRESULT NativeThemeWin::PaintMenuItemBackground(
    HDC hdc,
    State state,
    const gfx::Rect& rect,
    const MenuItemExtraParams& extra) const {
  HANDLE handle = GetThemeHandle(MENU);
  RECT rect_win = rect.ToRECT();
  int state_id;
  switch(state) {
    case kNormal:
      state_id = MPI_NORMAL;
      break;
    case kDisabled:
      state_id = extra.is_selected ? MPI_DISABLEDHOT : MPI_DISABLED;
      break;
    case kHovered:
      state_id = MPI_HOT;
      break;
    default:
      NOTREACHED() << "Invalid state " << state;
      break;
  }

  if (handle && draw_theme_)
    return draw_theme_(handle, hdc, MENU_POPUPITEM, state_id, &rect_win, NULL);

  if (extra.is_selected)
    FillRect(hdc, &rect_win, GetSysColorBrush(COLOR_HIGHLIGHT));
  return S_OK;
}

HRESULT NativeThemeWin::PaintMenuList(HDC hdc,
                                      State state,
                                      const gfx::Rect& rect,
                                      const MenuListExtraParams& extra) const {
  HANDLE handle = GetThemeHandle(MENULIST);
  RECT rect_win = rect.ToRECT();
  int state_id;
  switch(state) {
    case kNormal:
      state_id = CBXS_NORMAL;
      break;
    case kDisabled:
      state_id = CBXS_DISABLED;
      break;
    case kHovered:
      state_id = CBXS_HOT;
      break;
    case kPressed:
      state_id = CBXS_PRESSED;
      break;
    default:
      NOTREACHED() << "Invalid state " << state;
      break;
  }

  if (handle && draw_theme_)
    return draw_theme_(handle, hdc, CP_DROPDOWNBUTTON, state_id, &rect_win,
                       NULL);

  // Draw it manually.
  DrawFrameControl(hdc, &rect_win, DFC_SCROLL,
                   DFCS_SCROLLCOMBOBOX | extra.classic_state);
  return S_OK;
}

HRESULT NativeThemeWin::PaintMenuSeparator(
    HDC hdc,
    const gfx::Rect& rect,
    const MenuSeparatorExtraParams& extra) const {
  RECT rect_win = rect.ToRECT();
  if (!extra.has_gutter)
    rect_win.top = rect.y() + rect.height() / 3 + 1;

  HANDLE handle = GetThemeHandle(MENU);
  if (handle && draw_theme_) {
    // Delta is needed for non-classic to move separator up slightly.
    --rect_win.top;
    --rect_win.bottom;
    return draw_theme_(handle, hdc, MENU_POPUPSEPARATOR, MPI_NORMAL, &rect_win,
                       NULL);
  }

  DrawEdge(hdc, &rect_win, EDGE_ETCHED, BF_TOP);
  return S_OK;
}

HRESULT NativeThemeWin::PaintSpinButton(
    HDC hdc,
    Part part,
    State state,
    const gfx::Rect& rect,
    const InnerSpinButtonExtraParams& extra) const {
  HANDLE handle = GetThemeHandle(SPIN);
  RECT rect_win = rect.ToRECT();
  int part_id = extra.spin_up ? SPNP_UP : SPNP_DOWN;
  int state_id;
  switch(state) {
    case kDisabled:
      state_id = extra.spin_up ? UPS_DISABLED : DNS_DISABLED;
      break;
    case kHovered:
      state_id = extra.spin_up ? UPS_HOT : DNS_HOT;
      break;
    case kNormal:
      state_id = extra.spin_up ? UPS_NORMAL : DNS_NORMAL;
      break;
    case kPressed:
      state_id = extra.spin_up ? UPS_PRESSED : DNS_PRESSED;
      break;
    default:
      NOTREACHED() << "Invalid state " << state;
      break;
  }

  if (handle && draw_theme_)
    return draw_theme_(handle, hdc, part_id, state_id, &rect_win, NULL);
  DrawFrameControl(hdc, &rect_win, DFC_SCROLL, extra.classic_state);
  return S_OK;
}

HRESULT NativeThemeWin::PaintWindowResizeGripper(HDC hdc,
                                                 const gfx::Rect& rect) const {
  HANDLE handle = GetThemeHandle(STATUS);
  RECT rect_win = rect.ToRECT();
  if (handle && draw_theme_) {
    // Paint the status bar gripper.  There doesn't seem to be a
    // standard gripper in Windows for the space between
    // scrollbars.  This is pretty close, but it's supposed to be
    // painted over a status bar.
    return draw_theme_(handle, hdc, SP_GRIPPER, 0, &rect_win, NULL);
  }

  // Draw a windows classic scrollbar gripper.
  DrawFrameControl(hdc, &rect_win, DFC_SCROLL, DFCS_SCROLLSIZEGRIP);
  return S_OK;
}

HRESULT NativeThemeWin::PaintTabPanelBackground(HDC hdc,
                                                const gfx::Rect& rect) const {
  HANDLE handle = GetThemeHandle(TAB);
  RECT rect_win = rect.ToRECT();
  if (handle && draw_theme_)
    return draw_theme_(handle, hdc, TABP_BODY, 0, &rect_win, NULL);

  // Classic just renders a flat color background.
  FillRect(hdc, &rect_win, reinterpret_cast<HBRUSH>(COLOR_3DFACE + 1));
  return S_OK;
}

HRESULT NativeThemeWin::PaintTrackbar(
    SkCanvas* canvas,
    HDC hdc,
    Part part,
    State state,
    const gfx::Rect& rect,
    const TrackbarExtraParams& extra) const {
  int part_id = part == kTrackbarTrack ? TKP_TRACK : TKP_THUMBBOTTOM;
  if (extra.vertical)
    part_id = part == kTrackbarTrack ? TKP_TRACKVERT : TKP_THUMBVERT;

  int state_id = 0;
  switch(state) {
    case kDisabled:
      state_id = TUS_DISABLED;
      break;
    case kHovered:
      state_id = TUS_HOT;
      break;
    case kNormal:
      state_id = TUS_NORMAL;
      break;
    case kPressed:
      state_id = TUS_PRESSED;
      break;
    default:
      NOTREACHED() << "Invalid state " << state;
      break;
  }

  // Make the channel be 4 px thick in the center of the supplied rect.  (4 px
  // matches what XP does in various menus; GetThemePartSize() doesn't seem to
  // return good values here.)
  RECT rect_win = rect.ToRECT();
  RECT channel_rect = rect.ToRECT();
  const int channel_thickness = 4;
  if (part_id == TKP_TRACK) {
    channel_rect.top +=
        ((channel_rect.bottom - channel_rect.top - channel_thickness) / 2);
    channel_rect.bottom = channel_rect.top + channel_thickness;
  } else if (part_id == TKP_TRACKVERT) {
    channel_rect.left +=
        ((channel_rect.right - channel_rect.left - channel_thickness) / 2);
    channel_rect.right = channel_rect.left + channel_thickness;
  }  // else this isn't actually a channel, so |channel_rect| == |rect|.

  HANDLE handle = GetThemeHandle(TRACKBAR);
  if (handle && draw_theme_)
    return draw_theme_(handle, hdc, part_id, state_id, &channel_rect, NULL);

  // Classic mode, draw it manually.
  if ((part_id == TKP_TRACK) || (part_id == TKP_TRACKVERT)) {
    DrawEdge(hdc, &channel_rect, EDGE_SUNKEN, BF_RECT);
  } else if (part_id == TKP_THUMBVERT) {
    DrawEdge(hdc, &rect_win, EDGE_RAISED, BF_RECT | BF_SOFT | BF_MIDDLE);
  } else {
    // Split rect into top and bottom pieces.
    RECT top_section = rect.ToRECT();
    RECT bottom_section = rect.ToRECT();
    top_section.bottom -= ((bottom_section.right - bottom_section.left) / 2);
    bottom_section.top = top_section.bottom;
    DrawEdge(hdc, &top_section, EDGE_RAISED,
             BF_LEFT | BF_TOP | BF_RIGHT | BF_SOFT | BF_MIDDLE | BF_ADJUST);

    // Split triangular piece into two diagonals.
    RECT& left_half = bottom_section;
    RECT right_half = bottom_section;
    right_half.left += ((bottom_section.right - bottom_section.left) / 2);
    left_half.right = right_half.left;
    DrawEdge(hdc, &left_half, EDGE_RAISED,
             BF_DIAGONAL_ENDTOPLEFT | BF_SOFT | BF_MIDDLE | BF_ADJUST);
    DrawEdge(hdc, &right_half, EDGE_RAISED,
             BF_DIAGONAL_ENDBOTTOMLEFT | BF_SOFT | BF_MIDDLE | BF_ADJUST);

    // If the button is pressed, draw hatching.
    if (extra.classic_state & DFCS_PUSHED) {
      SkPaint paint;
      SetCheckerboardShader(&paint, rect_win);

      // Fill all three pieces with the pattern.
      canvas->drawIRect(skia::RECTToSkIRect(top_section), paint);

      SkScalar left_triangle_top = SkIntToScalar(left_half.top);
      SkScalar left_triangle_right = SkIntToScalar(left_half.right);
      SkPath left_triangle;
      left_triangle.moveTo(SkIntToScalar(left_half.left), left_triangle_top);
      left_triangle.lineTo(left_triangle_right, left_triangle_top);
      left_triangle.lineTo(left_triangle_right,
                           SkIntToScalar(left_half.bottom));
      left_triangle.close();
      canvas->drawPath(left_triangle, paint);

      SkScalar right_triangle_left = SkIntToScalar(right_half.left);
      SkScalar right_triangle_top = SkIntToScalar(right_half.top);
      SkPath right_triangle;
      right_triangle.moveTo(right_triangle_left, right_triangle_top);
      right_triangle.lineTo(SkIntToScalar(right_half.right),
                            right_triangle_top);
      right_triangle.lineTo(right_triangle_left,
                            SkIntToScalar(right_half.bottom));
      right_triangle.close();
      canvas->drawPath(right_triangle, paint);
    }
  }
  return S_OK;
}

//    <-a->
// [  *****             ]
//  ____ |              |
//  <-a-> <------b----->
// a: object_width
// b: frame_width
// *: animating object
//
// - the animation goes from "[" to "]" repeatedly.
// - the animation offset is at first "|"
//
static int ComputeAnimationProgress(int frame_width,
                                    int object_width,
                                    int pixels_per_second,
                                    double animated_seconds) {
  int animation_width = frame_width + object_width;
  double interval = static_cast<double>(animation_width) / pixels_per_second;
  double ratio = fmod(animated_seconds, interval) / interval;
  return static_cast<int>(animation_width * ratio) - object_width;
}

static RECT InsetRect(const RECT* rect, int size) {
  gfx::Rect result(*rect);
  result.Inset(size, size);
  return result.ToRECT();
}

HRESULT NativeThemeWin::PaintProgressBar(
    HDC hdc,
    const gfx::Rect& rect,
    const ProgressBarExtraParams& extra) const {
  // There is no documentation about the animation speed, frame-rate, nor
  // size of moving overlay of the indeterminate progress bar.
  // So we just observed real-world programs and guessed following parameters.
  const int kDeteminateOverlayPixelsPerSecond = 300;
  const int kDeteminateOverlayWidth = 120;
  const int kIndeterminateOverlayPixelsPerSecond =  175;
  const int kVistaIndeterminateOverlayWidth = 120;
  const int kXPIndeterminateOverlayWidth = 55;
  // The thickness of the bar frame inside |value_rect|
  const int kXPBarPadding = 3;

  RECT bar_rect = rect.ToRECT();
  RECT value_rect = gfx::Rect(extra.value_rect_x,
                              extra.value_rect_y,
                              extra.value_rect_width,
                              extra.value_rect_height).ToRECT();

  bool pre_vista = base::win::GetVersion() < base::win::VERSION_VISTA;
  HANDLE handle = GetThemeHandle(PROGRESS);
  if (handle && draw_theme_ && draw_theme_ex_) {
    draw_theme_(handle, hdc, PP_BAR, 0, &bar_rect, NULL);

    int bar_width = bar_rect.right - bar_rect.left;
    if (extra.determinate) {
      // TODO(morrita): this RTL guess can be wrong.
      // We should pass the direction from WebKit side.
      bool is_rtl = (bar_rect.right == value_rect.right &&
                     bar_rect.left != value_rect.left);
      // We should care the direction here because PP_CNUNK painting
      // is asymmetric.
      DTBGOPTS value_draw_options;
      value_draw_options.dwSize = sizeof(DTBGOPTS);
      value_draw_options.dwFlags = is_rtl ? DTBG_MIRRORDC : 0;
      value_draw_options.rcClip = bar_rect;

      if (pre_vista) {
        // On XP, progress bar is chunk-style and has no glossy effect.
        // We need to shrink destination rect to fit the part inside the bar
        // with an appropriate margin.
        RECT shrunk_value_rect = InsetRect(&value_rect, kXPBarPadding);
        draw_theme_ex_(handle, hdc, PP_CHUNK, 0,
                       &shrunk_value_rect, &value_draw_options);
      } else  {
        // On Vista or later, the progress bar part has a
        // single-block value part. It also has glossy effect.
        // And the value part has exactly same height as the bar part
        // so we don't need to shrink the rect.
        draw_theme_ex_(handle, hdc, PP_FILL, 0,
                       &value_rect, &value_draw_options);

        int dx = ComputeAnimationProgress(bar_width,
                                          kDeteminateOverlayWidth,
                                          kDeteminateOverlayPixelsPerSecond,
                                          extra.animated_seconds);
        RECT overlay_rect = value_rect;
        overlay_rect.left += dx;
        overlay_rect.right = overlay_rect.left + kDeteminateOverlayWidth;
        draw_theme_(handle, hdc, PP_MOVEOVERLAY, 0, &overlay_rect, &value_rect);
      }
    } else {
      // A glossy overlay for indeterminate progress bar has small pause
      // after each animation. We emulate this by adding an invisible margin
      // the animation has to traverse.
      int width_with_margin = bar_width + kIndeterminateOverlayPixelsPerSecond;
      int overlay_width = pre_vista ?
          kXPIndeterminateOverlayWidth : kVistaIndeterminateOverlayWidth;
      int dx = ComputeAnimationProgress(width_with_margin,
                                        overlay_width,
                                        kIndeterminateOverlayPixelsPerSecond,
                                        extra.animated_seconds);
      RECT overlay_rect = bar_rect;
      overlay_rect.left += dx;
      overlay_rect.right = overlay_rect.left + overlay_width;
      if (pre_vista) {
        RECT shrunk_rect = InsetRect(&overlay_rect, kXPBarPadding);
        RECT shrunk_bar_rect = InsetRect(&bar_rect, kXPBarPadding);
        draw_theme_(handle, hdc, PP_CHUNK, 0, &shrunk_rect, &shrunk_bar_rect);
      } else {
        draw_theme_(handle, hdc, PP_MOVEOVERLAY, 0, &overlay_rect, &bar_rect);
      }
    }

    return S_OK;
  }

  HBRUSH bg_brush = GetSysColorBrush(COLOR_BTNFACE);
  HBRUSH fg_brush = GetSysColorBrush(COLOR_BTNSHADOW);
  FillRect(hdc, &bar_rect, bg_brush);
  FillRect(hdc, &value_rect, fg_brush);
  DrawEdge(hdc, &bar_rect, EDGE_SUNKEN, BF_RECT | BF_ADJUST);
  return S_OK;
}

HRESULT NativeThemeWin::PaintTextField(
    HDC hdc,
    Part part,
    State state,
    const gfx::Rect& rect,
    const TextFieldExtraParams& extra) const {
  int part_id = EP_EDITTEXT;
  int state_id = ETS_NORMAL;
  switch(state) {
    case kNormal:
      if (extra.is_read_only) {
        state_id = ETS_READONLY;
      } else if (extra.is_focused) {
        state_id = ETS_FOCUSED;
      } else {
        state_id = ETS_NORMAL;
      }
      break;
    case kHovered:
      state_id = ETS_HOT;
      break;
    case kPressed:
      state_id = ETS_SELECTED;
      break;
    case kDisabled:
      state_id = ETS_DISABLED;
      break;
    default:
      NOTREACHED() << "Invalid state: " << state;
      break;
  }

  RECT rect_win = rect.ToRECT();
  return PaintTextField(hdc, part_id, state_id, extra.classic_state,
                        &rect_win,
                        skia::SkColorToCOLORREF(extra.background_color),
                        extra.fill_content_area, extra.draw_edges);
}

HRESULT NativeThemeWin::PaintTextField(HDC hdc,
                                       int part_id,
                                       int state_id,
                                       int classic_state,
                                       RECT* rect,
                                       COLORREF color,
                                       bool fill_content_area,
                                       bool draw_edges) const {
  // TODO(ojan): http://b/1210017 Figure out how to give the ability to
  // exclude individual edges from being drawn.

  HANDLE handle = GetThemeHandle(TEXTFIELD);
  // TODO(mpcomplete): can we detect if the color is specified by the user,
  // and if not, just use the system color?
  // CreateSolidBrush() accepts a RGB value but alpha must be 0.
  HBRUSH bg_brush = CreateSolidBrush(color);
  HRESULT hr;
  // DrawThemeBackgroundEx was introduced in XP SP2, so that it's possible
  // draw_theme_ex_ is NULL and draw_theme_ is non-null.
  if (handle && (draw_theme_ex_ || (draw_theme_ && draw_edges))) {
    if (draw_theme_ex_) {
      static DTBGOPTS omit_border_options = {
        sizeof(DTBGOPTS),
        DTBG_OMITBORDER,
        {0,0,0,0}
      };
      DTBGOPTS* draw_opts = draw_edges ? NULL : &omit_border_options;
      hr = draw_theme_ex_(handle, hdc, part_id, state_id, rect, draw_opts);
    } else {
      hr = draw_theme_(handle, hdc, part_id, state_id, rect, NULL);
    }

    // TODO(maruel): Need to be fixed if get_theme_content_rect_ is NULL.
    if (fill_content_area && get_theme_content_rect_) {
      RECT content_rect;
      hr = get_theme_content_rect_(handle, hdc, part_id, state_id, rect,
                                   &content_rect);
      FillRect(hdc, &content_rect, bg_brush);
    }
  } else {
    // Draw it manually.
    if (draw_edges)
      DrawEdge(hdc, rect, EDGE_SUNKEN, BF_RECT | BF_ADJUST);

    if (fill_content_area) {
      FillRect(hdc, rect, (classic_state & DFCS_INACTIVE) ?
                   reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1) : bg_brush);
    }
    hr = S_OK;
  }
  DeleteObject(bg_brush);
  return hr;
}

bool NativeThemeWin::IsThemingActive() const {
  if (is_theme_active_)
    return !!is_theme_active_();
  return false;
}

HRESULT NativeThemeWin::GetThemePartSize(ThemeName theme_name,
                                         HDC hdc,
                                         int part_id,
                                         int state_id,
                                         RECT* rect,
                                         int ts,
                                         SIZE* size) const {
  HANDLE handle = GetThemeHandle(theme_name);
  if (handle && get_theme_part_size_)
    return get_theme_part_size_(handle, hdc, part_id, state_id, rect, ts, size);

  return E_NOTIMPL;
}

HRESULT NativeThemeWin::GetThemeColor(ThemeName theme,
                                      int part_id,
                                      int state_id,
                                      int prop_id,
                                      SkColor* color) const {
  HANDLE handle = GetThemeHandle(theme);
  if (handle && get_theme_color_) {
    COLORREF color_ref;
    if (get_theme_color_(handle, part_id, state_id, prop_id, &color_ref) ==
        S_OK) {
      *color = skia::COLORREFToSkColor(color_ref);
      return S_OK;
    }
  }
  return E_NOTIMPL;
}

SkColor NativeThemeWin::GetThemeColorWithDefault(ThemeName theme,
                                                 int part_id,
                                                 int state_id,
                                                 int prop_id,
                                                 int default_sys_color) const {
  SkColor color;
  if (GetThemeColor(theme, part_id, state_id, prop_id, &color) != S_OK)
    color = skia::COLORREFToSkColor(GetSysColor(default_sys_color));
  return color;
}

HRESULT NativeThemeWin::GetThemeInt(ThemeName theme,
                                    int part_id,
                                    int state_id,
                                    int prop_id,
                                    int *value) const {
  HANDLE handle = GetThemeHandle(theme);
  if (handle && get_theme_int_)
    return get_theme_int_(handle, part_id, state_id, prop_id, value);
  return E_NOTIMPL;
}

Size NativeThemeWin::GetThemeBorderSize(ThemeName theme) const {
  // For simplicity use the wildcard state==0, part==0, since it works
  // for the cases we currently depend on.
  int border;
  if (GetThemeInt(theme, 0, 0, TMT_BORDERSIZE, &border) == S_OK)
    return Size(border, border);
  else
    return Size(GetSystemMetrics(SM_CXEDGE), GetSystemMetrics(SM_CYEDGE));
}


void NativeThemeWin::DisableTheming() const {
  if (!set_theme_properties_)
    return;
  set_theme_properties_(0);
}

HRESULT NativeThemeWin::PaintFrameControl(HDC hdc,
                                          const gfx::Rect& rect,
                                          UINT type,
                                          UINT state,
                                          bool is_selected,
                                          State control_state) const {
  const int width = rect.width();
  const int height = rect.height();

  // DrawFrameControl for menu arrow/check wants a monochrome bitmap.
  base::win::ScopedBitmap mask_bitmap(CreateBitmap(width, height, 1, 1, NULL));

  if (mask_bitmap == NULL)
    return E_OUTOFMEMORY;

  base::win::ScopedHDC bitmap_dc(CreateCompatibleDC(NULL));
  HGDIOBJ org_bitmap = SelectObject(bitmap_dc, mask_bitmap);
  RECT local_rect = { 0, 0, width, height };
  DrawFrameControl(bitmap_dc, &local_rect, type, state);

  // We're going to use BitBlt with a b&w mask. This results in using the dest
  // dc's text color for the black bits in the mask, and the dest dc's
  // background color for the white bits in the mask. DrawFrameControl draws the
  // check in black, and the background in white.
  int bg_color_key;
  int text_color_key;
  switch (control_state) {
    case gfx::NativeTheme::kHovered:
      bg_color_key = COLOR_HIGHLIGHT;
      text_color_key = COLOR_HIGHLIGHTTEXT;
      break;
    case gfx::NativeTheme::kNormal:
      bg_color_key = COLOR_MENU;
      text_color_key = COLOR_MENUTEXT;
      break;
    case gfx::NativeTheme::kDisabled:
      bg_color_key = is_selected ? COLOR_HIGHLIGHT : COLOR_MENU;
      text_color_key = COLOR_GRAYTEXT;
      break;
    default:
      NOTREACHED();
      bg_color_key = COLOR_MENU;
      text_color_key = COLOR_MENUTEXT;
      break;
  }
  COLORREF old_bg_color = SetBkColor(hdc, GetSysColor(bg_color_key));
  COLORREF old_text_color = SetTextColor(hdc, GetSysColor(text_color_key));
  BitBlt(hdc, rect.x(), rect.y(), width, height, bitmap_dc, 0, 0, SRCCOPY);
  SetBkColor(hdc, old_bg_color);
  SetTextColor(hdc, old_text_color);

  SelectObject(bitmap_dc, org_bitmap);

  return S_OK;
}

void NativeThemeWin::CloseHandles() const {
  if (!close_theme_)
    return;

  for (int i = 0; i < LAST; ++i) {
    if (theme_handles_[i]) {
      close_theme_(theme_handles_[i]);
      theme_handles_[i] = NULL;
    }
  }
}

bool NativeThemeWin::IsClassicTheme(ThemeName name) const {
  if (!theme_dll_)
    return true;

  return !GetThemeHandle(name);
}

HANDLE NativeThemeWin::GetThemeHandle(ThemeName theme_name) const {
  if (!open_theme_ || theme_name < 0 || theme_name >= LAST)
    return 0;

  if (theme_handles_[theme_name])
    return theme_handles_[theme_name];

  // Not found, try to load it.
  HANDLE handle = 0;
  switch (theme_name) {
  case BUTTON:
    handle = open_theme_(NULL, L"Button");
    break;
  case LIST:
    handle = open_theme_(NULL, L"Listview");
    break;
  case MENU:
    handle = open_theme_(NULL, L"Menu");
    break;
  case MENULIST:
    handle = open_theme_(NULL, L"Combobox");
    break;
  case SCROLLBAR:
    handle = open_theme_(NULL, L"Scrollbar");
    break;
  case STATUS:
    handle = open_theme_(NULL, L"Status");
    break;
  case TAB:
    handle = open_theme_(NULL, L"Tab");
    break;
  case TEXTFIELD:
    handle = open_theme_(NULL, L"Edit");
    break;
  case TRACKBAR:
    handle = open_theme_(NULL, L"Trackbar");
    break;
  case WINDOW:
    handle = open_theme_(NULL, L"Window");
    break;
  case PROGRESS:
    handle = open_theme_(NULL, L"Progress");
    break;
  case SPIN:
    handle = open_theme_(NULL, L"Spin");
    break;
  default:
    NOTREACHED();
  }
  theme_handles_[theme_name] = handle;
  return handle;
}

// static
NativeThemeWin::ThemeName NativeThemeWin::GetThemeName(Part part) {
  ThemeName name;
  switch(part) {
    case kCheckbox:
    case kRadio:
    case kPushButton:
      name = BUTTON;
      break;
    case kInnerSpinButton:
      name = SPIN;
      break;
    case kMenuCheck:
    case kMenuPopupGutter:
    case kMenuList:
    case kMenuPopupArrow:
    case kMenuPopupSeparator:
      name = MENU;
      break;
    case kProgressBar:
      name = PROGRESS;
      break;
    case kScrollbarDownArrow:
    case kScrollbarLeftArrow:
    case kScrollbarRightArrow:
    case kScrollbarUpArrow:
    case kScrollbarHorizontalThumb:
    case kScrollbarVerticalThumb:
    case kScrollbarHorizontalTrack:
    case kScrollbarVerticalTrack:
      name = SCROLLBAR;
      break;
    case kSliderTrack:
    case kSliderThumb:
      name = TRACKBAR;
      break;
    case kTextField:
      name = TEXTFIELD;
      break;
    case kWindowResizeGripper:
      name = STATUS;
      break;
    default:
      NOTREACHED() << "Invalid part: " << part;
      break;
  }
  return name;
}

// static
int NativeThemeWin::GetWindowsPart(Part part,
                                   State state,
                                   const ExtraParams& extra) {
  int part_id;
  switch(part) {
    case kCheckbox:
      part_id = BP_CHECKBOX;
      break;
    case kMenuCheck:
      part_id = MENU_POPUPCHECK;
      break;
    case kMenuPopupArrow:
      part_id = MENU_POPUPSUBMENU;
      break;
    case kMenuPopupGutter:
      part_id = MENU_POPUPGUTTER;
      break;
    case kMenuPopupSeparator:
      part_id = MENU_POPUPSEPARATOR;
      break;
    case kPushButton:
      part_id = BP_PUSHBUTTON;
      break;
    case kRadio:
      part_id = BP_RADIOBUTTON;
      break;
    case kWindowResizeGripper:
      part_id = SP_GRIPPER;
      break;
    default:
      NOTREACHED() << "Invalid part: " << part;
      break;
  }
  return part_id;
}

int NativeThemeWin::GetWindowsState(Part part,
                                    State state,
                                    const ExtraParams& extra) {
  int state_id;
  switch(part) {
    case kCheckbox:
      switch(state) {
        case kNormal:
          state_id = CBS_UNCHECKEDNORMAL;
          break;
        case kHovered:
          state_id = CBS_UNCHECKEDHOT;
          break;
        case kPressed:
          state_id = CBS_UNCHECKEDPRESSED;
          break;
        case kDisabled:
          state_id = CBS_UNCHECKEDDISABLED;
          break;
        default:
          NOTREACHED() << "Invalid state: " << state;
          break;
      }
      break;
    case kMenuCheck:
      switch(state) {
        case kNormal:
        case kHovered:
        case kPressed:
          state_id = extra.menu_check.is_radio ? MC_BULLETNORMAL
                                               : MC_CHECKMARKNORMAL;
          break;
        case kDisabled:
          state_id = extra.menu_check.is_radio ? MC_BULLETDISABLED
                                               : MC_CHECKMARKDISABLED;
          break;
        default:
          NOTREACHED() << "Invalid state: " << state;
          break;
      }
      break;
    case kMenuPopupArrow:
    case kMenuPopupGutter:
    case kMenuPopupSeparator:
      switch(state) {
        case kNormal:
          state_id = MBI_NORMAL;
          break;
        case kHovered:
          state_id = MBI_HOT;
          break;
        case kPressed:
          state_id = MBI_PUSHED;
          break;
        case kDisabled:
          state_id = MBI_DISABLED;
          break;
        default:
          NOTREACHED() << "Invalid state: " << state;
          break;
      }
      break;
    case kPushButton:
      switch(state) {
        case kNormal:
          state_id = PBS_NORMAL;
          break;
        case kHovered:
          state_id = PBS_HOT;
          break;
        case kPressed:
          state_id = PBS_PRESSED;
          break;
        case kDisabled:
          state_id = PBS_DISABLED;
          break;
        default:
          NOTREACHED() << "Invalid state: " << state;
          break;
      }
      break;
    case kRadio:
      switch(state) {
        case kNormal:
          state_id = RBS_UNCHECKEDNORMAL;
          break;
        case kHovered:
          state_id = RBS_UNCHECKEDHOT;
          break;
        case kPressed:
          state_id = RBS_UNCHECKEDPRESSED;
          break;
        case kDisabled:
          state_id = RBS_UNCHECKEDDISABLED;
          break;
        default:
          NOTREACHED() << "Invalid state: " << state;
          break;
      }
      break;
    case kWindowResizeGripper:
      switch(state) {
        case kNormal:
        case kHovered:
        case kPressed:
        case kDisabled:
          state_id = 1;  // gripper has no windows state
          break;
        default:
          NOTREACHED() << "Invalid state: " << state;
          break;
      }
      break;
    default:
      NOTREACHED() << "Invalid part: " << part;
      break;
  }
  return state_id;
}

}  // namespace gfx
