// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/autocomplete/autocomplete_popup_contents_view.h"

#include "base/compiler_specific.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/autocomplete/autocomplete_popup_model.h"
#include "chrome/browser/instant/instant_confirm_dialog.h"
#include "chrome/browser/instant/promo_counter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/omnibox/omnibox_view.h"
#include "chrome/browser/ui/views/autocomplete/autocomplete_result_view.h"
#include "chrome/browser/ui/views/bubble/bubble_border.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "third_party/skia/include/core/SkShader.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/theme_provider.h"
#include "ui/gfx/canvas_skia.h"
#include "ui/gfx/insets.h"
#include "ui/gfx/path.h"
#include "unicode/ubidi.h"
#include "views/controls/button/text_button.h"
#include "views/controls/label.h"
#include "views/layout/grid_layout.h"
#include "views/layout/layout_constants.h"
#include "views/painter.h"
#include "views/widget/widget.h"
#include "views/window/window.h"

#if defined(OS_WIN)
#include <commctrl.h>
#include <dwmapi.h>
#include <objidl.h>

#include "base/win/scoped_gdi_object.h"
#include "views/widget/native_widget_win.h"
#endif

#if defined(TOOLKIT_USES_GTK)
#include "ui/gfx/skia_utils_gtk.h"
#endif

namespace {

const SkAlpha kGlassPopupAlpha = 240;
const SkAlpha kOpaquePopupAlpha = 255;
// The size delta between the font used for the edit and the result rows. Passed
// to gfx::Font::DeriveFont.
#if defined(OS_CHROMEOS)
// Don't adjust the size on Chrome OS (http://crbug.com/61433).
const int kEditFontAdjust = 0;
#else
const int kEditFontAdjust = -1;
#endif

// Horizontal padding between the buttons on the opt in promo.
const int kOptInButtonPadding = 2;

// Padding around the opt in view.
const int kOptInLeftPadding = 12;
const int kOptInRightPadding = 10;
const int kOptInTopPadding = 6;
const int kOptInBottomPadding = 5;

// Horizontal/Vertical inset of the promo background.
const int kOptInBackgroundHInset = 6;
const int kOptInBackgroundVInset = 2;

// Border for instant opt-in buttons. Consists of two 9 patch painters: one for
// the normal state, the other for the pressed state.
class OptInButtonBorder : public views::Border {
 public:
  OptInButtonBorder() {
    border_painter_.reset(CreatePainter(IDR_OPT_IN_BUTTON));
    border_pushed_painter_.reset(CreatePainter(IDR_OPT_IN_BUTTON_P));
  }

  virtual void Paint(const views::View& view, gfx::Canvas* canvas) const {
    views::Painter* painter;
    if (static_cast<const views::CustomButton&>(view).state() ==
        views::CustomButton::BS_PUSHED) {
      painter = border_pushed_painter_.get();
    } else {
      painter = border_painter_.get();
    }
    painter->Paint(view.width(), view.height(), canvas);
  }

  virtual void GetInsets(gfx::Insets* insets) const {
    insets->Set(3, 8, 3, 8);
  }

 private:
  // Creates 9 patch painter from the image with the id |image_id|.
  views::Painter* CreatePainter(int image_id) {
    SkBitmap* image =
        ResourceBundle::GetSharedInstance().GetBitmapNamed(image_id);
    int w = image->width() / 2;
    if (image->width() % 2 == 0)
      w--;
    int h = image->height() / 2;
    if (image->height() % 2 == 0)
      h--;
    gfx::Insets insets(h, w, h, w);
    return views::Painter::CreateImagePainter(*image, insets, true);
  }

  scoped_ptr<views::Painter> border_painter_;
  scoped_ptr<views::Painter> border_pushed_painter_;

  DISALLOW_COPY_AND_ASSIGN(OptInButtonBorder);
};

gfx::NativeView GetRelativeWindowForPopup(gfx::NativeView edit_native_view) {
#if defined(OS_WIN)
  // When an IME is attached to the rich-edit control, retrieve its window
  // handle and show this popup window under the IME windows.
  // Otherwise, show this popup window under top-most windows.
  // TODO(hbono): http://b/1111369 if we exclude this popup window from the
  // display area of IME windows, this workaround becomes unnecessary.
  HWND ime_window = ImmGetDefaultIMEWnd(edit_native_view);
  return ime_window ? ime_window : HWND_NOTOPMOST;
#elif defined(TOOLKIT_USES_GTK)
  GtkWidget* toplevel = gtk_widget_get_toplevel(edit_native_view);
  DCHECK(GTK_WIDGET_TOPLEVEL(toplevel));
  return toplevel;
#endif
}

}  // namespace

class AutocompletePopupContentsView::AutocompletePopupWidget
    : public views::Widget,
      public base::SupportsWeakPtr<AutocompletePopupWidget> {
 public:
  AutocompletePopupWidget() {}
  virtual ~AutocompletePopupWidget() {}

 private:
   DISALLOW_COPY_AND_ASSIGN(AutocompletePopupWidget);
};

class AutocompletePopupContentsView::InstantOptInView
    : public views::View,
      public views::ButtonListener {
 public:
  InstantOptInView(AutocompletePopupContentsView* contents_view,
                   const gfx::Font& label_font,
                   const gfx::Font& button_font)
      : contents_view_(contents_view),
        bg_painter_(views::Painter::CreateVerticalGradient(
                        SkColorSetRGB(255, 242, 183),
                        SkColorSetRGB(250, 230, 145))) {
    views::Label* label = new views::Label(
        UTF16ToWide(l10n_util::GetStringUTF16(IDS_INSTANT_OPT_IN_LABEL)));
    label->SetFont(label_font);

    views::GridLayout* layout = new views::GridLayout(this);
    layout->SetInsets(kOptInTopPadding, kOptInLeftPadding,
                      kOptInBottomPadding, kOptInRightPadding);
    SetLayoutManager(layout);

    const int first_column_set = 1;
    views::GridLayout::Alignment v_align = views::GridLayout::CENTER;
    views::ColumnSet* column_set = layout->AddColumnSet(first_column_set);
    column_set->AddColumn(views::GridLayout::TRAILING, v_align, 1,
                          views::GridLayout::USE_PREF, 0, 0);
    column_set->AddPaddingColumn(0, views::kRelatedControlHorizontalSpacing);
    column_set->AddColumn(views::GridLayout::CENTER, v_align, 0,
                          views::GridLayout::USE_PREF, 0, 0);
    column_set->AddPaddingColumn(0, kOptInButtonPadding);
    column_set->AddColumn(views::GridLayout::CENTER, v_align, 0,
                          views::GridLayout::USE_PREF, 0, 0);
    column_set->LinkColumnSizes(2, 4, -1);
    layout->StartRow(0, first_column_set);
    layout->AddView(label);
    layout->AddView(CreateButton(IDS_INSTANT_OPT_IN_ENABLE, button_font));
    layout->AddView(CreateButton(IDS_INSTANT_OPT_IN_NO_THANKS, button_font));
  }

  virtual void ButtonPressed(views::Button* sender, const views::Event& event) {
    contents_view_->UserPressedOptIn(
        sender->tag() == IDS_INSTANT_OPT_IN_ENABLE);
    // WARNING: we've been deleted.
  }

  virtual void OnPaint(gfx::Canvas* canvas) {
    canvas->Save();
    canvas->TranslateInt(kOptInBackgroundHInset, kOptInBackgroundVInset);
    bg_painter_->Paint(width() - kOptInBackgroundHInset * 2,
                       height() - kOptInBackgroundVInset * 2, canvas);
    canvas->DrawRectInt(ResourceBundle::toolbar_separator_color, 0, 0,
                        width() - kOptInBackgroundHInset * 2,
                        height() - kOptInBackgroundVInset * 2);
    canvas->Restore();
  }

 private:
  // Creates and returns a button configured for the opt-in promo.
  views::View* CreateButton(int id, const gfx::Font& font) {
    // NOTE: we can't use NativeButton as the popup is a layered window and
    // native buttons don't draw  in layered windows.
    // TODO: these buttons look crap. Figure out the right border/background to
    // use.
    views::TextButton* button =
        new views::TextButton(this, UTF16ToWide(l10n_util::GetStringUTF16(id)));
    button->set_border(new OptInButtonBorder());
    button->SetNormalHasBorder(true);
    button->set_tag(id);
    button->SetFont(font);
    button->set_animate_on_state_change(false);
    return button;
  }

  AutocompletePopupContentsView* contents_view_;
  scoped_ptr<views::Painter> bg_painter_;

  DISALLOW_COPY_AND_ASSIGN(InstantOptInView);
};

////////////////////////////////////////////////////////////////////////////////
// AutocompletePopupContentsView, public:

AutocompletePopupContentsView::AutocompletePopupContentsView(
    const gfx::Font& font,
    OmniboxView* omnibox_view,
    AutocompleteEditModel* edit_model,
    Profile* profile,
    const views::View* location_bar)
    : model_(new AutocompletePopupModel(this, edit_model, profile)),
      opt_in_view_(NULL),
      omnibox_view_(omnibox_view),
      location_bar_(location_bar),
      result_font_(font.DeriveFont(kEditFontAdjust)),
      result_bold_font_(result_font_.DeriveFont(0, gfx::Font::BOLD)),
      ignore_mouse_drag_(false),
      ALLOW_THIS_IN_INITIALIZER_LIST(size_animation_(this)) {
  // The following little dance is required because set_border() requires a
  // pointer to a non-const object.
  BubbleBorder* bubble_border = new BubbleBorder(BubbleBorder::NONE);
  bubble_border_ = bubble_border;
  set_border(bubble_border);
  // The contents is owned by the LocationBarView.
  set_parent_owned(false);
}

AutocompletePopupContentsView::~AutocompletePopupContentsView() {
  // We don't need to do anything with |popup_| here.  The OS either has already
  // closed the window, in which case it's been deleted, or it will soon, in
  // which case there's nothing we need to do.
}

gfx::Rect AutocompletePopupContentsView::GetPopupBounds() const {
  if (!size_animation_.is_animating())
    return target_bounds_;

  gfx::Rect current_frame_bounds = start_bounds_;
  int total_height_delta = target_bounds_.height() - start_bounds_.height();
  // Round |current_height_delta| instead of truncating so we won't leave single
  // white pixels at the bottom of the popup as long when animating very small
  // height differences.
  int current_height_delta = static_cast<int>(
      size_animation_.GetCurrentValue() * total_height_delta - 0.5);
  current_frame_bounds.set_height(
      current_frame_bounds.height() + current_height_delta);
  return current_frame_bounds;
}

void AutocompletePopupContentsView::LayoutChildren() {
  gfx::Rect contents_rect = GetContentsBounds();
  int top = contents_rect.y();
  for (int i = 0; i < child_count(); ++i) {
    View* v = GetChildViewAt(i);
    if (v->IsVisible()) {
      v->SetBounds(contents_rect.x(), top, contents_rect.width(),
                   v->GetPreferredSize().height());
      top = v->bounds().bottom();
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
// AutocompletePopupContentsView, AutocompletePopupView overrides:

bool AutocompletePopupContentsView::IsOpen() const {
  return (popup_ != NULL);
}

void AutocompletePopupContentsView::InvalidateLine(size_t line) {
  GetChildViewAt(static_cast<int>(line))->SchedulePaint();
}

void AutocompletePopupContentsView::UpdatePopupAppearance() {
  if (model_->result().empty()) {
    // No matches, close any existing popup.
    if (popup_ != NULL) {
      size_animation_.Stop();
      // NOTE: Do NOT use CloseNow() here, as we may be deep in a callstack
      // triggered by the popup receiving a message (e.g. LBUTTONUP), and
      // destroying the popup would cause us to read garbage when we unwind back
      // to that level.
      popup_->Close();  // This will eventually delete the popup.
      popup_.reset();
    }
    return;
  }

  // Update the match cached by each row, in the process of doing so make sure
  // we have enough row views.
  size_t child_rv_count = child_count();
  if (opt_in_view_) {
    DCHECK(child_rv_count > 0);
    child_rv_count--;
  }
  for (size_t i = 0; i < model_->result().size(); ++i) {
    AutocompleteResultView* result_view;
    if (i >= child_rv_count) {
      result_view =
          CreateResultView(this, i, result_font_, result_bold_font_);
      AddChildViewAt(result_view, static_cast<int>(i));
    } else {
      result_view = static_cast<AutocompleteResultView*>(GetChildViewAt(i));
      result_view->SetVisible(true);
    }
    result_view->SetMatch(GetMatchAtIndex(i));
  }
  for (size_t i = model_->result().size(); i < child_rv_count; ++i)
    GetChildViewAt(i)->SetVisible(false);

  PromoCounter* counter = model_->profile()->GetInstantPromoCounter();
  if (!opt_in_view_ && counter && counter->ShouldShow(base::Time::Now())) {
    opt_in_view_ = new InstantOptInView(this, result_bold_font_, result_font_);
    AddChildView(opt_in_view_);
  } else if (opt_in_view_ && (!counter ||
                              !counter->ShouldShow(base::Time::Now()))) {
    delete opt_in_view_;
    opt_in_view_ = NULL;
  }

  gfx::Rect new_target_bounds = CalculateTargetBounds(CalculatePopupHeight());

  // If we're animating and our target height changes, reset the animation.
  // NOTE: If we just reset blindly on _every_ update, then when the user types
  // rapidly we could get "stuck" trying repeatedly to animate shrinking by the
  // last few pixels to get to one visible result.
  if (new_target_bounds.height() != target_bounds_.height())
    size_animation_.Reset();
  target_bounds_ = new_target_bounds;

  if (popup_ == NULL) {
    // If the popup is currently closed, we need to create it.
    popup_ = (new AutocompletePopupWidget)->AsWeakPtr();
    views::Widget::InitParams params(views::Widget::InitParams::TYPE_POPUP);
    params.can_activate = false;
    params.transparent = true;
    params.parent = location_bar_->GetWidget()->GetNativeView();
    params.bounds = GetPopupBounds();
    popup_->Init(params);
    popup_->SetContentsView(this);
    popup_->MoveAbove(
        GetRelativeWindowForPopup(omnibox_view_->GetNativeView()));
    popup_->Show();
  } else {
    // Animate the popup shrinking, but don't animate growing larger since that
    // would make the popup feel less responsive.
    start_bounds_ = GetWidget()->GetWindowScreenBounds();
    if (target_bounds_.height() < start_bounds_.height())
      size_animation_.Show();
    else
      start_bounds_ = target_bounds_;
    popup_->SetBounds(GetPopupBounds());
  }

  SchedulePaint();
}

gfx::Rect AutocompletePopupContentsView::GetTargetBounds() {
  return target_bounds_;
}

void AutocompletePopupContentsView::PaintUpdatesNow() {
  // TODO(beng): remove this from the interface.
}

void AutocompletePopupContentsView::OnDragCanceled() {
  ignore_mouse_drag_ = true;
}

////////////////////////////////////////////////////////////////////////////////
// AutocompletePopupContentsView, AutocompleteResultViewModel implementation:

bool AutocompletePopupContentsView::IsSelectedIndex(size_t index) const {
  return HasMatchAt(index) ? index == model_->selected_line() : false;
}

bool AutocompletePopupContentsView::IsHoveredIndex(size_t index) const {
  return HasMatchAt(index) ? index == model_->hovered_line() : false;
}

const SkBitmap* AutocompletePopupContentsView::GetIconIfExtensionMatch(
    size_t index) const {
  if (!HasMatchAt(index))
    return NULL;
  return model_->GetIconIfExtensionMatch(GetMatchAtIndex(index));
}

////////////////////////////////////////////////////////////////////////////////
// AutocompletePopupContentsView, AnimationDelegate implementation:

void AutocompletePopupContentsView::AnimationProgressed(
    const ui::Animation* animation) {
  // We should only be running the animation when the popup is already visible.
  DCHECK(popup_ != NULL);
  popup_->SetBounds(GetPopupBounds());
}

////////////////////////////////////////////////////////////////////////////////
// AutocompletePopupContentsView, views::View overrides:

void AutocompletePopupContentsView::Layout() {
  UpdateBlurRegion();

  // Size our children to the available content area.
  LayoutChildren();

  // We need to manually schedule a paint here since we are a layered window and
  // won't implicitly require painting until we ask for one.
  SchedulePaint();
}

views::View* AutocompletePopupContentsView::GetEventHandlerForPoint(
    const gfx::Point& point) {
  // If there is no opt in view, then we want all mouse events. Otherwise let
  // any descendants of the opt-in view get mouse events.
  if (!opt_in_view_)
    return this;

  views::View* child = views::View::GetEventHandlerForPoint(point);
  views::View* ancestor = child;
  while (ancestor && ancestor != opt_in_view_)
    ancestor = ancestor->parent();
  return ancestor ? child : this;
}

bool AutocompletePopupContentsView::OnMousePressed(
    const views::MouseEvent& event) {
  ignore_mouse_drag_ = false;  // See comment on |ignore_mouse_drag_| in header.
  if (event.IsLeftMouseButton() || event.IsMiddleMouseButton()) {
    size_t index = GetIndexForPoint(event.location());
    model_->SetHoveredLine(index);
    if (HasMatchAt(index) && event.IsLeftMouseButton())
      model_->SetSelectedLine(index, false, false);
  }
  return true;
}

bool AutocompletePopupContentsView::OnMouseDragged(
    const views::MouseEvent& event) {
  if (event.IsLeftMouseButton() || event.IsMiddleMouseButton()) {
    size_t index = GetIndexForPoint(event.location());
    model_->SetHoveredLine(index);
    if (!ignore_mouse_drag_ && HasMatchAt(index) && event.IsLeftMouseButton())
      model_->SetSelectedLine(index, false, false);
  }
  return true;
}

void AutocompletePopupContentsView::OnMouseReleased(
    const views::MouseEvent& event) {
  if (ignore_mouse_drag_) {
    OnMouseCaptureLost();
    return;
  }

  size_t index = GetIndexForPoint(event.location());
  if (event.IsOnlyMiddleMouseButton())
    OpenIndex(index, NEW_BACKGROUND_TAB);
  else if (event.IsOnlyLeftMouseButton())
    OpenIndex(index, CURRENT_TAB);
}

void AutocompletePopupContentsView::OnMouseCaptureLost() {
  ignore_mouse_drag_ = false;
}

void AutocompletePopupContentsView::OnMouseMoved(
    const views::MouseEvent& event) {
  model_->SetHoveredLine(GetIndexForPoint(event.location()));
}

void AutocompletePopupContentsView::OnMouseEntered(
    const views::MouseEvent& event) {
  model_->SetHoveredLine(GetIndexForPoint(event.location()));
}

void AutocompletePopupContentsView::OnMouseExited(
    const views::MouseEvent& event) {
  model_->SetHoveredLine(AutocompletePopupModel::kNoMatch);
}

////////////////////////////////////////////////////////////////////////////////
// AutocompletePopupContentsView, protected:

void AutocompletePopupContentsView::PaintResultViews(gfx::CanvasSkia* canvas) {
  canvas->drawColor(AutocompleteResultView::GetColor(
      AutocompleteResultView::NORMAL, AutocompleteResultView::BACKGROUND));
  View::PaintChildren(canvas);
}

int AutocompletePopupContentsView::CalculatePopupHeight() {
  DCHECK_GE(static_cast<size_t>(child_count()), model_->result().size());
  int popup_height = 0;
  for (size_t i = 0; i < model_->result().size(); ++i)
    popup_height += GetChildViewAt(i)->GetPreferredSize().height();
  return popup_height +
      (opt_in_view_ ? opt_in_view_->GetPreferredSize().height() : 0);
}

AutocompleteResultView* AutocompletePopupContentsView::CreateResultView(
    AutocompleteResultViewModel* model,
    int model_index,
    const gfx::Font& font,
    const gfx::Font& bold_font) {
  return new AutocompleteResultView(model, model_index, font, bold_font);
}

////////////////////////////////////////////////////////////////////////////////
// AutocompletePopupContentsView, views::View overrides, protected:

void AutocompletePopupContentsView::OnPaint(gfx::Canvas* canvas) {
  // We paint our children in an unconventional way.
  //
  // Because the border of this view creates an anti-aliased round-rect region
  // for the contents, we need to render our rectangular result child views into
  // this round rect region. We can't use a simple clip because clipping is
  // 1-bit and we get nasty jagged edges.
  //
  // Instead, we paint all our children into a second canvas and use that as a
  // shader to fill a path representing the round-rect clipping region. This
  // yields a nice anti-aliased edge.
  gfx::CanvasSkia contents_canvas(width(), height(), true);
  PaintResultViews(&contents_canvas);

  // We want the contents background to be slightly transparent so we can see
  // the blurry glass effect on DWM systems behind. We do this _after_ we paint
  // the children since they paint text, and GDI will reset this alpha data if
  // we paint text after this call.
  MakeCanvasTransparent(&contents_canvas);

  // Now paint the contents of the contents canvas into the actual canvas.
  SkPaint paint;
  paint.setAntiAlias(true);

  SkShader* shader = SkShader::CreateBitmapShader(
      contents_canvas.getDevice()->accessBitmap(false),
      SkShader::kClamp_TileMode,
      SkShader::kClamp_TileMode);
  paint.setShader(shader);
  shader->unref();

  gfx::Path path;
  MakeContentsPath(&path, GetContentsBounds());
  canvas->AsCanvasSkia()->drawPath(path, paint);

  // Now we paint the border, so it will be alpha-blended atop the contents.
  // This looks slightly better in the corners than drawing the contents atop
  // the border.
  OnPaintBorder(canvas);
}

void AutocompletePopupContentsView::PaintChildren(gfx::Canvas* canvas) {
  // We paint our children inside OnPaint().
}

////////////////////////////////////////////////////////////////////////////////
// AutocompletePopupContentsView, private:

bool AutocompletePopupContentsView::HasMatchAt(size_t index) const {
  return index < model_->result().size();
}

const AutocompleteMatch& AutocompletePopupContentsView::GetMatchAtIndex(
    size_t index) const {
  return model_->result().match_at(index);
}

void AutocompletePopupContentsView::MakeContentsPath(
    gfx::Path* path,
    const gfx::Rect& bounding_rect) {
  SkRect rect;
  rect.set(SkIntToScalar(bounding_rect.x()),
           SkIntToScalar(bounding_rect.y()),
           SkIntToScalar(bounding_rect.right()),
           SkIntToScalar(bounding_rect.bottom()));

  SkScalar radius = SkIntToScalar(BubbleBorder::GetCornerRadius());
  path->addRoundRect(rect, radius, radius);
}

void AutocompletePopupContentsView::UpdateBlurRegion() {
#if defined(OS_WIN)
  // We only support background blurring on Vista with Aero-Glass enabled.
  if (!views::NativeWidgetWin::IsAeroGlassEnabled() || !GetWidget())
    return;

  // Provide a blurred background effect within the contents region of the
  // popup.
  DWM_BLURBEHIND bb = {0};
  bb.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
  bb.fEnable = true;

  // Translate the contents rect into widget coordinates, since that's what
  // DwmEnableBlurBehindWindow expects a region in.
  gfx::Rect contents_rect = GetContentsBounds();
  gfx::Point origin(contents_rect.origin());
  views::View::ConvertPointToWidget(this, &origin);
  contents_rect.set_origin(origin);

  gfx::Path contents_path;
  MakeContentsPath(&contents_path, contents_rect);
  base::win::ScopedGDIObject<HRGN> popup_region;
  popup_region.Set(contents_path.CreateNativeRegion());
  bb.hRgnBlur = popup_region.Get();
  DwmEnableBlurBehindWindow(GetWidget()->GetNativeView(), &bb);
#endif
}

void AutocompletePopupContentsView::MakeCanvasTransparent(
    gfx::Canvas* canvas) {
  // Allow the window blur effect to show through the popup background.
  SkAlpha alpha = GetThemeProvider()->ShouldUseNativeFrame() ?
      kGlassPopupAlpha : kOpaquePopupAlpha;
  canvas->AsCanvasSkia()->drawColor(SkColorSetA(
      AutocompleteResultView::GetColor(AutocompleteResultView::NORMAL,
      AutocompleteResultView::BACKGROUND), alpha), SkXfermode::kDstIn_Mode);
}

void AutocompletePopupContentsView::OpenIndex(
    size_t index,
    WindowOpenDisposition disposition) {
  if (!HasMatchAt(index))
    return;

  // OpenMatch() may close the popup, which will clear the result set and, by
  // extension, |match| and its contents.  So copy the relevant match out to
  // make sure it stays alive until the call completes.
  AutocompleteMatch match = model_->result().match_at(index);
  string16 keyword;
  const bool is_keyword_hint = model_->GetKeywordForMatch(match, &keyword);
  omnibox_view_->OpenMatch(match, disposition, GURL(), index,
                         is_keyword_hint ? string16() : keyword);
}

size_t AutocompletePopupContentsView::GetIndexForPoint(
    const gfx::Point& point) {
  if (!HitTest(point))
    return AutocompletePopupModel::kNoMatch;

  int nb_match = model_->result().size();
  DCHECK(nb_match <= child_count());
  for (int i = 0; i < nb_match; ++i) {
    views::View* child = GetChildViewAt(i);
    gfx::Point point_in_child_coords(point);
    View::ConvertPointToView(this, child, &point_in_child_coords);
    if (child->HitTest(point_in_child_coords))
      return i;
  }
  return AutocompletePopupModel::kNoMatch;
}

gfx::Rect AutocompletePopupContentsView::CalculateTargetBounds(int h) {
  gfx::Rect location_bar_bounds(location_bar_->GetContentsBounds());
  const views::Border* border = location_bar_->border();
  if (border) {
    // Adjust for the border so that the bubble and location bar borders are
    // aligned.
    gfx::Insets insets;
    border->GetInsets(&insets);
    location_bar_bounds.Inset(insets.left(), 0, insets.right(), 0);
  } else {
    // The normal location bar is drawn using a background graphic that includes
    // the border, so we inset by enough to make the edges line up, and the
    // bubble appear at the same height as the Star bubble.
    location_bar_bounds.Inset(LocationBarView::kNormalHorizontalEdgeThickness,
                              0);
  }
  gfx::Point location_bar_origin(location_bar_bounds.origin());
  views::View::ConvertPointToScreen(location_bar_, &location_bar_origin);
  location_bar_bounds.set_origin(location_bar_origin);
  return bubble_border_->GetBounds(
      location_bar_bounds, gfx::Size(location_bar_bounds.width(), h));
}

void AutocompletePopupContentsView::UserPressedOptIn(bool opt_in) {
  delete opt_in_view_;
  opt_in_view_ = NULL;
  PromoCounter* counter = model_->profile()->GetInstantPromoCounter();
  DCHECK(counter);
  counter->Hide();
  if (opt_in) {
    browser::ShowInstantConfirmDialogIfNecessary(
        location_bar_->GetWindow()->GetNativeWindow(), model_->profile());
  }
  UpdatePopupAppearance();
}
