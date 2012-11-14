// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/facebook_chat/bubble/bubble_delegate.h"

#include "chrome/browser/ui/views/facebook_chat/bubble/bubble_frame_view.h"
#include "ui/base/animation/slide_animation.h"
#include "ui/gfx/color_utils.h"
#include "ui/views/widget/widget.h"

// The duration of the fade animation in milliseconds.
static const int kHideFadeDurationMS = 200;

// The defaut margin between the content and the inside border, in pixels.
static const int kDefaultMargin = 6;

namespace {

// Create a widget to host the bubble.
views::Widget* CreateBubbleWidget(BitpopBubbleDelegateView* bubble, views::Widget* parent) {
  views::Widget* bubble_widget = new views::Widget();
  views::Widget::InitParams bubble_params(views::Widget::InitParams::TYPE_BUBBLE);
  bubble_params.delegate = bubble;
  bubble_params.transparent = true;
  bubble_params.parent_widget = parent;
  if (bubble->use_focusless())
    bubble_params.can_activate = false;
#if defined(OS_WIN) && !defined(USE_AURA)
  bubble_params.type = views::Widget::InitParams::TYPE_WINDOW_FRAMELESS;
  bubble_params.transparent = false;
#endif
  bubble_widget->Init(bubble_params);
  return bubble_widget;
}

#if defined(OS_WIN) && !defined(USE_AURA)
// The border widget's delegate, needed for transparent Windows native controls.
// TODO(msw): Remove this when Windows native controls are no longer needed.
class BitpopBubbleBorderDelegateView : public views::WidgetDelegateView {
 public:
  explicit BitpopBubbleBorderDelegateView(BitpopBubbleDelegateView* bubble)
      : bubble_(bubble) {}
  virtual ~BitpopBubbleBorderDelegateView() {}

  // views::WidgetDelegateView overrides:
  virtual bool CanActivate() const OVERRIDE;
  virtual views::NonClientFrameView* CreateNonClientFrameView() OVERRIDE;

 private:
  BitpopBubbleDelegateView* bubble_;

  DISALLOW_COPY_AND_ASSIGN(BitpopBubbleBorderDelegateView);
};

bool BitpopBubbleBorderDelegateView::CanActivate() const { return false; }

views::NonClientFrameView* BitpopBubbleBorderDelegateView::CreateNonClientFrameView() {
  return bubble_->CreateNonClientFrameView();
}

// Create a widget to host the bubble's border.
views::Widget* CreateBorderWidget(BitpopBubbleDelegateView* bubble, views::Widget* parent) {
  views::Widget* border_widget = new views::Widget();
  views::Widget::InitParams border_params(views::Widget::InitParams::TYPE_BUBBLE);
  border_params.delegate = new BitpopBubbleBorderDelegateView(bubble);
  border_params.transparent = true;
  border_params.parent_widget = parent;
  if (!border_params.parent_widget)
    border_params.ownership = views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  border_widget->Init(border_params);
  return border_widget;
}
#endif

}  // namespace

#if defined(OS_WIN) && !defined(USE_AURA)
const SkColor BitpopBubbleDelegateView::kBackgroundColor =
    color_utils::GetSysSkColor(COLOR_WINDOW);
#else
// TODO(beng): source from theme provider.
const SkColor BitpopBubbleDelegateView::kBackgroundColor = SK_ColorWHITE;
#endif

BitpopBubbleDelegateView::BitpopBubbleDelegateView()
    : close_on_esc_(true),
      close_on_deactivate_(true),
      anchor_view_(NULL),
      arrow_location_(BitpopBubbleBorder::TOP_LEFT),
      color_(kBackgroundColor),
      margin_(kDefaultMargin),
      original_opacity_(255),
      border_widget_(NULL),
      use_focusless_(false) {
  set_background(views::Background::CreateSolidBackground(color_));
  AddAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, 0));
}

BitpopBubbleDelegateView::BitpopBubbleDelegateView(
    views::View* anchor_view,
    BitpopBubbleBorder::ArrowLocation arrow_location)
    : close_on_esc_(true),
      close_on_deactivate_(true),
      anchor_view_(anchor_view),
      arrow_location_(arrow_location),
      color_(kBackgroundColor),
      margin_(kDefaultMargin),
      original_opacity_(255),
      border_widget_(NULL),
      use_focusless_(false) {
  set_background(views::Background::CreateSolidBackground(color_));
  AddAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, 0));
}

BitpopBubbleDelegateView::~BitpopBubbleDelegateView() {}

// static
views::Widget* BitpopBubbleDelegateView::CreateBubble(BitpopBubbleDelegateView* bubble_delegate) {
  bubble_delegate->Init();
  views::Widget* parent = bubble_delegate->anchor_view() ?
      bubble_delegate->anchor_view()->GetWidget() : NULL;
  views::Widget* bubble_widget = CreateBubbleWidget(bubble_delegate, parent);

#if defined(OS_WIN) && !defined(USE_AURA)
  // First set the contents view to initialize view bounds for widget sizing.
  bubble_widget->SetContentsView(bubble_delegate->GetContentsView());
  bubble_delegate->border_widget_ = CreateBorderWidget(bubble_delegate, parent);
#endif

  bubble_delegate->SizeToContents();
  bubble_widget->AddObserver(bubble_delegate);
  return bubble_widget;
}

views::View* BitpopBubbleDelegateView::GetInitiallyFocusedView() {
  return this;
}

views::BubbleDelegateView* BitpopBubbleDelegateView::AsBubbleDelegate() {
  return NULL;
}

views::View* BitpopBubbleDelegateView::GetContentsView() {
  return this;
}

views::NonClientFrameView* BitpopBubbleDelegateView::CreateNonClientFrameView() {
  return new BitpopBubbleFrameView(arrow_location(), color(), margin());
}

void BitpopBubbleDelegateView::OnWidgetClosing(views::Widget* widget) {
  if (widget == GetWidget()) {
    widget->RemoveObserver(this);
    if (border_widget_) {
      border_widget_->Close();
      border_widget_ = NULL;
    }
  }
}

void BitpopBubbleDelegateView::OnWidgetVisibilityChanged(views::Widget* widget,
                                                   bool visible) {
  if (widget == GetWidget()) {
    if (visible) {
      if (border_widget_)
        border_widget_->Show();
      GetFocusManager()->SetFocusedView(GetInitiallyFocusedView());
      views::Widget* anchor_widget = anchor_view() ? anchor_view()->GetWidget() : NULL;
      if (anchor_widget && anchor_widget->GetTopLevelWidget())
        anchor_widget->GetTopLevelWidget()->DisableInactiveRendering();
    } else if (border_widget_) {
      border_widget_->Hide();
    }
  }
}

void BitpopBubbleDelegateView::OnWidgetActivationChanged(views::Widget* widget,
                                                   bool active) {
  if (close_on_deactivate() && widget == GetWidget() && !active)
    GetWidget()->Close();
}

gfx::Rect BitpopBubbleDelegateView::GetAnchorRect() {
  return anchor_view() ? anchor_view()->GetScreenBounds() : gfx::Rect();
}

void BitpopBubbleDelegateView::Show() {
  GetWidget()->Show();
}

void BitpopBubbleDelegateView::StartFade(bool fade_in) {
  fade_animation_.reset(new ui::SlideAnimation(this));
  fade_animation_->SetSlideDuration(kHideFadeDurationMS);
  fade_animation_->Reset(fade_in ? 0.0 : 1.0);
  if (fade_in) {
    original_opacity_ = 0;
    if (border_widget_)
      border_widget_->SetOpacity(original_opacity_);
    GetWidget()->SetOpacity(original_opacity_);
    Show();
    fade_animation_->Show();
  } else {
    original_opacity_ = 255;
    fade_animation_->Hide();
  }
}

void BitpopBubbleDelegateView::ResetFade() {
  fade_animation_.reset();
  if (border_widget_)
    border_widget_->SetOpacity(original_opacity_);
  GetWidget()->SetOpacity(original_opacity_);
}

void BitpopBubbleDelegateView::SetAlignment(BitpopBubbleBorder::BubbleAlignment alignment) {
  GetBubbleFrameView()->bubble_border()->set_alignment(alignment);
  SizeToContents();
}

bool BitpopBubbleDelegateView::AcceleratorPressed(
    const ui::Accelerator& accelerator) {
  if (!close_on_esc() || accelerator.key_code() != ui::VKEY_ESCAPE)
    return false;
  if (fade_animation_.get())
    fade_animation_->Reset();
  GetWidget()->Close();
  return true;
}

void BitpopBubbleDelegateView::AnimationEnded(const ui::Animation* animation) {
  if (animation != fade_animation_.get())
    return;
  bool closed = fade_animation_->GetCurrentValue() == 0;
  fade_animation_->Reset();
  if (closed)
    GetWidget()->Close();
}

void BitpopBubbleDelegateView::AnimationProgressed(const ui::Animation* animation) {
  if (animation != fade_animation_.get())
    return;
  DCHECK(fade_animation_->is_animating());
  unsigned char opacity = fade_animation_->GetCurrentValue() * 255;
#if defined(OS_WIN) && !defined(USE_AURA)
  // Explicitly set the content views::Widget's layered style and set transparency via
  // SetLayeredWindowAttributes. This is done because initializing the views::Widget as
  // transparent and setting opacity via UpdateLayeredWindow doesn't support
  // hosting child native Windows controls.
  const HWND hwnd = GetWidget()->GetNativeView();
  const DWORD style = GetWindowLong(hwnd, GWL_EXSTYLE);
  if ((opacity == 255) == !!(style & WS_EX_LAYERED))
    SetWindowLong(hwnd, GWL_EXSTYLE, style ^ WS_EX_LAYERED);
  SetLayeredWindowAttributes(hwnd, 0, opacity, LWA_ALPHA);
  // Update the border widget's opacity.
  border_widget_->SetOpacity(opacity);
  border_widget_->non_client_view()->SchedulePaint();
#endif
  GetWidget()->SetOpacity(opacity);
  SchedulePaint();
}

void BitpopBubbleDelegateView::Init() {}

void BitpopBubbleDelegateView::SizeToContents() {
#if defined(OS_WIN) && !defined(USE_AURA)
  border_widget_->SetBounds(GetBubbleBounds());
  GetWidget()->SetBounds(GetBubbleClientBounds());
#else
  GetWidget()->SetBounds(GetBubbleBounds());
#endif
}

BitpopBubbleFrameView* BitpopBubbleDelegateView::GetBubbleFrameView() const {
  const views::Widget* widget = border_widget_ ? border_widget_ : GetWidget();
  const views::NonClientView* view = widget ? widget->non_client_view() : NULL;
  return view ? static_cast<BitpopBubbleFrameView*>(view->frame_view()) : NULL;
}

gfx::Rect BitpopBubbleDelegateView::GetBubbleBounds() {
  // The argument rect has its origin at the bubble's arrow anchor point;
  // its size is the preferred size of the bubble's client view (this view).
  return GetBubbleFrameView()->GetUpdatedWindowBounds(GetAnchorRect(),
      GetPreferredSize(), true /*try_mirroring_arrow*/);
}

#if defined(OS_WIN) && !defined(USE_AURA)
gfx::Rect BitpopBubbleDelegateView::GetBubbleClientBounds() const {
  gfx::Rect client_bounds(GetBubbleFrameView()->GetBoundsForClientView());
  client_bounds.Offset(border_widget_->GetWindowScreenBounds().origin());
  return client_bounds;
}
#endif
