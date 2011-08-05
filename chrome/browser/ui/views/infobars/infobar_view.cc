// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/infobars/infobar_view.h"

#include "base/message_loop.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/tab_contents/infobar_delegate.h"
#include "chrome/browser/ui/views/infobars/infobar_background.h"
#include "chrome/browser/ui/views/infobars/infobar_button_border.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "grit/theme_resources_standard.h"
#include "third_party/skia/include/effects/SkGradientShader.h"
#include "ui/base/accessibility/accessible_view_state.h"
#include "ui/base/animation/slide_animation.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/canvas_skia_paint.h"
#include "ui/gfx/image.h"
#include "views/controls/button/image_button.h"
#include "views/controls/button/menu_button.h"
#include "views/controls/button/text_button.h"
#include "views/controls/image_view.h"
#include "views/controls/label.h"
#include "views/controls/link.h"
#include "views/focus/external_focus_tracker.h"
#include "views/widget/widget.h"
#include "views/window/non_client_view.h"

#if defined(OS_WIN)
#include <shellapi.h>

#include "base/win/win_util.h"
#include "base/win/windows_version.h"
#include "ui/base/win/hwnd_util.h"
#include "ui/gfx/icon_util.h"
#endif

// static
const int InfoBar::kSeparatorLineHeight =
    views::NonClientFrameView::kClientEdgeThickness;
const int InfoBar::kDefaultArrowTargetHeight = 9;
const int InfoBar::kMaximumArrowTargetHeight = 24;
const int InfoBar::kDefaultArrowTargetHalfWidth = kDefaultArrowTargetHeight;
const int InfoBar::kMaximumArrowTargetHalfWidth = 14;

#ifdef TOUCH_UI
const int InfoBar::kDefaultBarTargetHeight = 75;
#else
const int InfoBar::kDefaultBarTargetHeight = 36;
#endif

const int InfoBarView::kButtonButtonSpacing = 10;
const int InfoBarView::kEndOfLabelSpacing = 16;
const int InfoBarView::kHorizontalPadding = 6;

InfoBarView::InfoBarView(TabContentsWrapper* owner, InfoBarDelegate* delegate)
    : InfoBar(owner, delegate),
      icon_(NULL),
      close_button_(NULL),
      ALLOW_THIS_IN_INITIALIZER_LIST(delete_factory_(this)),
      fill_path_(new SkPath),
      stroke_path_(new SkPath) {
  set_parent_owned(false);  // InfoBar deletes itself at the appropriate time.
  set_background(new InfoBarBackground(delegate->GetInfoBarType()));
}

InfoBarView::~InfoBarView() {
}

// static
views::Label* InfoBarView::CreateLabel(const string16& text) {
  views::Label* label = new views::Label(UTF16ToWideHack(text),
      ResourceBundle::GetSharedInstance().GetFont(ResourceBundle::MediumFont));
  label->SetColor(SK_ColorBLACK);
  label->SetHorizontalAlignment(views::Label::ALIGN_LEFT);
  return label;
}

// static
views::Link* InfoBarView::CreateLink(const string16& text,
                                     views::LinkListener* listener,
                                     const SkColor& background_color) {
  views::Link* link = new views::Link;
  link->SetText(UTF16ToWideHack(text));
  link->SetFont(
      ResourceBundle::GetSharedInstance().GetFont(ResourceBundle::MediumFont));
  link->SetHorizontalAlignment(views::Label::ALIGN_LEFT);
  link->set_listener(listener);
  link->MakeReadableOverBackgroundColor(background_color);
  return link;
}

// static
views::MenuButton* InfoBarView::CreateMenuButton(
    const string16& text,
    bool normal_has_border,
    views::ViewMenuDelegate* menu_delegate) {
  views::MenuButton* menu_button =
      new views::MenuButton(NULL, UTF16ToWideHack(text), menu_delegate, true);
  menu_button->set_border(new InfoBarButtonBorder);
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  menu_button->set_menu_marker(
      rb.GetBitmapNamed(IDR_INFOBARBUTTON_MENU_DROPARROW));
  if (normal_has_border) {
    menu_button->SetNormalHasBorder(true);
    menu_button->SetAnimationDuration(0);
  }
  menu_button->SetEnabledColor(SK_ColorBLACK);
  menu_button->SetHighlightColor(SK_ColorBLACK);
  menu_button->SetHoverColor(SK_ColorBLACK);
  menu_button->SetFont(rb.GetFont(ResourceBundle::MediumFont));
  return menu_button;
}

// static
views::TextButton* InfoBarView::CreateTextButton(
    views::ButtonListener* listener,
    const string16& text,
    bool needs_elevation) {
  views::TextButton* text_button =
      new views::TextButton(listener, UTF16ToWideHack(text));
  text_button->set_border(new InfoBarButtonBorder);
  text_button->SetNormalHasBorder(true);
  text_button->SetAnimationDuration(0);
  text_button->SetEnabledColor(SK_ColorBLACK);
  text_button->SetHighlightColor(SK_ColorBLACK);
  text_button->SetHoverColor(SK_ColorBLACK);
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  text_button->SetFont(rb.GetFont(ResourceBundle::MediumFont));
#if defined(OS_WIN)
  if (needs_elevation &&
      (base::win::GetVersion() >= base::win::VERSION_VISTA) &&
      base::win::UserAccountControlIsEnabled()) {
    SHSTOCKICONINFO icon_info = { sizeof SHSTOCKICONINFO };
    // Even with the runtime guard above, we have to use GetProcAddress() here,
    // because otherwise the loader will try to resolve the function address on
    // startup, which will break on XP.
    typedef HRESULT (STDAPICALLTYPE *GetStockIconInfo)(SHSTOCKICONID, UINT,
                                                       SHSTOCKICONINFO*);
    GetStockIconInfo func = reinterpret_cast<GetStockIconInfo>(
        GetProcAddress(GetModuleHandle(L"shell32.dll"), "SHGetStockIconInfo"));
    (*func)(SIID_SHIELD, SHGSI_ICON | SHGSI_SMALLICON, &icon_info);
    text_button->SetIcon(*IconUtil::CreateSkBitmapFromHICON(icon_info.hIcon,
        gfx::Size(GetSystemMetrics(SM_CXSMICON),
                  GetSystemMetrics(SM_CYSMICON))));
  }
#endif
  return text_button;
}

void InfoBarView::Layout() {
  // Calculate the fill and stroke paths.  We do this here, rather than in
  // PlatformSpecificRecalculateHeight(), because this is also reached when our
  // width is changed, which affects both paths.
  stroke_path_->rewind();
  fill_path_->rewind();
  const InfoBarContainer::Delegate* delegate = container_delegate();
  if (delegate) {
    static_cast<InfoBarBackground*>(background())->set_separator_color(
        delegate->GetInfoBarSeparatorColor());
    int arrow_x;
    SkScalar arrow_fill_height =
        SkIntToScalar(std::max(arrow_height() - kSeparatorLineHeight, 0));
    SkScalar arrow_fill_half_width = SkIntToScalar(arrow_half_width());
    SkScalar separator_height = SkIntToScalar(kSeparatorLineHeight);
    if (delegate->DrawInfoBarArrows(&arrow_x) && arrow_fill_height) {
      // Skia pixel centers are at the half-values, so the arrow is horizontally
      // centered at |arrow_x| + 0.5.  Vertically, the stroke path is the center
      // of the separator, while the fill path is a closed path that extends up
      // through the entire height of the separator and down to the bottom of
      // the arrow where it joins the bar.
      stroke_path_->moveTo(
          SkIntToScalar(arrow_x) + SK_ScalarHalf - arrow_fill_half_width,
          SkIntToScalar(arrow_height()) - (separator_height * SK_ScalarHalf));
      stroke_path_->rLineTo(arrow_fill_half_width, -arrow_fill_height);
      stroke_path_->rLineTo(arrow_fill_half_width, arrow_fill_height);

      *fill_path_ = *stroke_path_;
      // Move the top of the fill path up to the top of the separator and then
      // extend it down all the way through.
      fill_path_->offset(0, -separator_height * SK_ScalarHalf);
      // This 0.01 hack prevents the fill from filling more pixels on the right
      // edge of the arrow than on the left.
      const SkScalar epsilon = 0.01f;
      fill_path_->rLineTo(-epsilon, 0);
      fill_path_->rLineTo(0, separator_height);
      fill_path_->rLineTo(epsilon - (arrow_fill_half_width * 2), 0);
      fill_path_->close();
    }
  }
  if (bar_height()) {
    fill_path_->addRect(0.0, SkIntToScalar(arrow_height()),
        SkIntToScalar(width()), SkIntToScalar(height() - kSeparatorLineHeight));
  }

  int start_x = kHorizontalPadding;
  if (icon_ != NULL) {
    gfx::Size icon_size = icon_->GetPreferredSize();
    icon_->SetBounds(start_x, OffsetY(icon_size), icon_size.width(),
                     icon_size.height());
  }

  gfx::Size button_size = close_button_->GetPreferredSize();
  close_button_->SetBounds(std::max(start_x + ContentMinimumWidth(),
      width() - kHorizontalPadding - button_size.width()), OffsetY(button_size),
      button_size.width(), button_size.height());
}

void InfoBarView::ViewHierarchyChanged(bool is_add, View* parent, View* child) {
  View::ViewHierarchyChanged(is_add, parent, child);

  if (child == this) {
    if (is_add) {
#if defined(OS_WIN)
      // When we're added to a view hierarchy within a widget, we create an
      // external focus tracker to track what was focused in case we obtain
      // focus so that we can restore focus when we're removed.
      views::Widget* widget = GetWidget();
      if (widget) {
        focus_tracker_.reset(
            new views::ExternalFocusTracker(this, GetFocusManager()));
      }
#endif
      if (GetFocusManager())
        GetFocusManager()->AddFocusChangeListener(this);
      if (GetWidget()) {
        GetWidget()->NotifyAccessibilityEvent(
            this, ui::AccessibilityTypes::EVENT_ALERT, true);
      }

      if (close_button_ == NULL) {
        gfx::Image* image = delegate()->GetIcon();
        if (image) {
          icon_ = new views::ImageView;
          icon_->SetImage(*image);
          AddChildView(icon_);
        }

        close_button_ = new views::ImageButton(this);
        ResourceBundle& rb = ResourceBundle::GetSharedInstance();
        close_button_->SetImage(views::CustomButton::BS_NORMAL,
                                rb.GetBitmapNamed(IDR_CLOSE_BAR));
        close_button_->SetImage(views::CustomButton::BS_HOT,
                                rb.GetBitmapNamed(IDR_CLOSE_BAR_H));
        close_button_->SetImage(views::CustomButton::BS_PUSHED,
                                rb.GetBitmapNamed(IDR_CLOSE_BAR_P));
        close_button_->SetAccessibleName(
            l10n_util::GetStringUTF16(IDS_ACCNAME_CLOSE));
        close_button_->SetFocusable(true);
        AddChildView(close_button_);
      }
    } else {
      DestroyFocusTracker(false);
      animation()->Stop();
      // Finally, clean ourselves up when we're removed from the view hierarchy
      // since no-one refers to us now.
      MessageLoop::current()->PostTask(FROM_HERE,
          delete_factory_.NewRunnableMethod(&InfoBarView::DeleteSelf));
      if (GetFocusManager())
        GetFocusManager()->RemoveFocusChangeListener(this);
    }
  }

  // For accessibility, ensure the close button is the last child view.
  if ((close_button_ != NULL) && (parent == this) && (child != close_button_) &&
      (close_button_->parent() == this) &&
      (GetChildViewAt(child_count() - 1) != close_button_)) {
    RemoveChildView(close_button_);
    AddChildView(close_button_);
  }
}

void InfoBarView::PaintChildren(gfx::Canvas* canvas) {
  canvas->Save();

  // TODO(scr): This really should be the |fill_path_|, but the clipPath seems
  // broken on non-Windows platforms (crbug.com/75154). For now, just clip to
  // the bar bounds.
  //
  // gfx::CanvasSkia* canvas_skia = canvas->AsCanvasSkia();
  // canvas_skia->clipPath(*fill_path_);
  DCHECK_EQ(total_height(), height())
      << "Infobar piecewise heights do not match overall height";
  canvas->ClipRectInt(0, arrow_height(), width(), bar_height());
  views::View::PaintChildren(canvas);
  canvas->Restore();
}

void InfoBarView::ButtonPressed(views::Button* sender,
                                const views::Event& event) {
  if (sender == close_button_) {
    if (delegate())
      delegate()->InfoBarDismissed();
    RemoveInfoBar();
  }
}

int InfoBarView::ContentMinimumWidth() const {
  return 0;
}

int InfoBarView::StartX() const {
  // Ensure we don't return a value greater than EndX(), so children can safely
  // set something's width to "EndX() - StartX()" without risking that being
  // negative.
  return std::min(EndX(),
      ((icon_ != NULL) ? icon_->bounds().right() : 0) + kHorizontalPadding);
}

int InfoBarView::EndX() const {
  const int kCloseButtonSpacing = 12;
  return close_button_->x() - kCloseButtonSpacing;
}

const InfoBarContainer::Delegate* InfoBarView::container_delegate() const {
  const InfoBarContainer* infobar_container = container();
  return infobar_container ? infobar_container->delegate() : NULL;
}

void InfoBarView::PlatformSpecificHide(bool animate) {
  if (!animate)
    return;

  bool restore_focus = true;
#if defined(OS_WIN)
  // Do not restore focus (and active state with it) on Windows if some other
  // top-level window became active.
  if (GetWidget() &&
      !ui::DoesWindowBelongToActiveWindow(GetWidget()->GetNativeView()))
    restore_focus = false;
#endif  // defined(OS_WIN)
  DestroyFocusTracker(restore_focus);
}

void InfoBarView::PlatformSpecificOnHeightsRecalculated() {
  // Ensure that notifying our container of our size change will result in a
  // re-layout.
  InvalidateLayout();
}

void InfoBarView::GetAccessibleState(ui::AccessibleViewState* state) {
  if (delegate()) {
    state->name = l10n_util::GetStringUTF16(
        (delegate()->GetInfoBarType() == InfoBarDelegate::WARNING_TYPE) ?
        IDS_ACCNAME_INFOBAR_WARNING : IDS_ACCNAME_INFOBAR_PAGE_ACTION);
  }
  state->role = ui::AccessibilityTypes::ROLE_ALERT;
}

gfx::Size InfoBarView::GetPreferredSize() {
  return gfx::Size(0, total_height());
}

void InfoBarView::FocusWillChange(View* focused_before, View* focused_now) {
  // This will trigger some screen readers to read the entire contents of this
  // infobar.
  if (focused_before && focused_now && !this->Contains(focused_before) &&
      this->Contains(focused_now) && GetWidget()) {
    GetWidget()->NotifyAccessibilityEvent(
        this, ui::AccessibilityTypes::EVENT_ALERT, true);
  }
}

void InfoBarView::DestroyFocusTracker(bool restore_focus) {
  if (focus_tracker_ != NULL) {
    if (restore_focus)
      focus_tracker_->FocusLastFocusedExternalView();
    focus_tracker_->SetFocusManager(NULL);
    focus_tracker_.reset();
  }
}

void InfoBarView::DeleteSelf() {
  delete this;
}
