// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/facebook_chat/bubble/bubble_delegate.h"

#include "ui/base/animation/slide_animation.h"
#include "ui/gfx/color_utils.h"
#include "chrome/browser/ui/views/facebook_chat/bubble/bubble_frame_view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

// The duration of the fade animation in milliseconds.
static const int kHideFadeDurationMS = 200;

// The defaut margin between the content and the inside border, in pixels.
static const int kDefaultMargin = 6;

namespace views {

namespace {

// Create a widget to host the bubble.
Widget* CreateBubbleWidget(BitpopBubbleDelegateView* bubble) {
  Widget* bubble_widget = new Widget();
  Widget::InitParams bubble_params(Widget::InitParams::TYPE_BUBBLE);
  bubble_params.delegate = bubble;
  bubble_params.transparent = true;
  if (bubble->parent_window())
    bubble_params.parent = bubble->parent_window();
  else
    bubble_params.parent_widget = bubble->anchor_widget();
  if (bubble->use_focusless())
    bubble_params.can_activate = false;
#if defined(OS_WIN) && !defined(USE_AURA)
  bubble_params.type = Widget::InitParams::TYPE_WINDOW_FRAMELESS;
  bubble_params.transparent = false;
#endif
  bubble_widget->Init(bubble_params);
  return bubble_widget;
}

#if defined(OS_WIN) && !defined(USE_AURA)
// Windows uses two widgets and some extra complexity to host partially
// transparent native controls and use per-pixel HWND alpha on the border.
// TODO(msw): Clean these up when Windows native controls are no longer needed.
class BitpopBubbleBorderDelegate : public WidgetDelegate,
                             public WidgetObserver {
 public:
  BitpopBubbleBorderDelegate(BitpopBubbleDelegateView* bubble, Widget* widget)
      : bubble_(bubble),
        widget_(widget) {
    bubble_->GetWidget()->AddObserver(this);
  }

  virtual ~BitpopBubbleBorderDelegate() {
    if (bubble_ && bubble_->GetWidget())
      bubble_->GetWidget()->RemoveObserver(this);
  }

  // WidgetDelegate overrides:
  virtual bool CanActivate() const OVERRIDE { return false; }
  virtual void DeleteDelegate() OVERRIDE { delete this; }
  virtual Widget* GetWidget() OVERRIDE { return widget_; }
  virtual const Widget* GetWidget() const OVERRIDE { return widget_; }
  virtual NonClientFrameView* CreateNonClientFrameView(
      Widget* widget) OVERRIDE {
    return bubble_->CreateNonClientFrameView(widget);
  }

  // WidgetObserver overrides:
  virtual void OnWidgetClosing(Widget* widget) OVERRIDE {
    bubble_ = NULL;
    widget_->Close();
  }

 private:
  BitpopBubbleDelegateView* bubble_;
  Widget* widget_;

  DISALLOW_COPY_AND_ASSIGN(BitpopBubbleBorderDelegate);
};

// Create a widget to host the bubble's border.
Widget* CreateBorderWidget(BitpopBubbleDelegateView* bubble) {
  Widget* border_widget = new Widget();
  Widget::InitParams border_params(Widget::InitParams::TYPE_BUBBLE);
  border_params.delegate = new BitpopBubbleBorderDelegate(bubble, border_widget);
  border_params.transparent = true;
  border_params.parent_widget = bubble->anchor_widget();
  border_params.can_activate = false;
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
      anchor_widget_(NULL),
      move_with_anchor_(false),
      arrow_location_(BitpopBubbleBorder::TOP_LEFT),
      color_(kBackgroundColor),
      margins_(kDefaultMargin, kDefaultMargin, kDefaultMargin, kDefaultMargin),
      original_opacity_(255),
      border_widget_(NULL),
      use_focusless_(false),
      try_mirroring_arrow_(true),
      parent_window_(NULL) {
  set_background(Background::CreateSolidBackground(color_));
  AddAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE));
}

BitpopBubbleDelegateView::BitpopBubbleDelegateView(
    View* anchor_view,
    BitpopBubbleBorder::ArrowLocation arrow_location)
    : close_on_esc_(true),
      close_on_deactivate_(true),
      anchor_view_(anchor_view),
      anchor_widget_(NULL),
      move_with_anchor_(false),
      arrow_location_(arrow_location),
      color_(kBackgroundColor),
      margins_(kDefaultMargin, kDefaultMargin, kDefaultMargin, kDefaultMargin),
      original_opacity_(255),
      border_widget_(NULL),
      use_focusless_(false),
      try_mirroring_arrow_(true),
      parent_window_(NULL) {
  set_background(Background::CreateSolidBackground(color_));
  AddAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE));
}

BitpopBubbleDelegateView::~BitpopBubbleDelegateView() {
  if (anchor_widget() != NULL)
    anchor_widget()->RemoveObserver(this);
  anchor_widget_ = NULL;
  anchor_view_ = NULL;
}

// static
Widget* BitpopBubbleDelegateView::CreateBubble(BitpopBubbleDelegateView* bubble_delegate) {
  bubble_delegate->Init();
  // Determine the anchor widget from the anchor view at bubble creation time.
  bubble_delegate->anchor_widget_ = bubble_delegate->anchor_view() ?
      bubble_delegate->anchor_view()->GetWidget() : NULL;
  if (bubble_delegate->anchor_widget())
    bubble_delegate->anchor_widget()->AddObserver(bubble_delegate);

  Widget* bubble_widget = CreateBubbleWidget(bubble_delegate);

#if defined(OS_WIN) && !defined(USE_AURA)
  // First set the contents view to initialize view bounds for widget sizing.
  bubble_widget->SetContentsView(bubble_delegate->GetContentsView());
  bubble_delegate->border_widget_ = CreateBorderWidget(bubble_delegate);
#endif

  bubble_delegate->SizeToContents();
  bubble_widget->AddObserver(bubble_delegate);
  return bubble_widget;
}

//View* BitpopBubbleDelegateView::GetInitiallyFocusedView() {
//  return this;
//}

BubbleDelegateView* BitpopBubbleDelegateView::AsBubbleDelegate() {
  return reinterpret_cast<BubbleDelegateView*>(this);
}

View* BitpopBubbleDelegateView::GetContentsView() {
  return this;
}

NonClientFrameView* BitpopBubbleDelegateView::CreateNonClientFrameView(
    Widget* widget) {
  BitpopBubbleBorder::ArrowLocation arrow_loc = arrow_location();
  if (base::i18n::IsRTL())
    arrow_loc = BitpopBubbleBorder::horizontal_mirror(arrow_loc);
  // TODO(alicet): Expose the shadow option in BorderContentsView when we make
  // the fullscreen exit bubble use the new bubble code.
  BitpopBubbleBorder* border = new BitpopBubbleBorder(arrow_loc, BitpopBubbleBorder::NO_SHADOW);
  border->set_background_color(color());
  BitpopBubbleFrameView* frame_view = new BitpopBubbleFrameView(margins(), border);
  frame_view->set_background(new BitpopBubbleBackground(border));
  return frame_view;
}

void BitpopBubbleDelegateView::OnWidgetClosing(Widget* widget) {
  if (anchor_widget() == widget) {
    anchor_view_ = NULL;
    anchor_widget_ = NULL;
  }
}

void BitpopBubbleDelegateView::OnWidgetVisibilityChanged(Widget* widget,
                                                   bool visible) {
  if (widget != GetWidget())
    return;

  if (visible) {
    if (border_widget_)
      border_widget_->ShowInactive();
    //GetFocusManager()->SetFocusedView(GetInitiallyFocusedView());
    if (anchor_widget() && anchor_widget()->GetTopLevelWidget())
      anchor_widget()->GetTopLevelWidget()->DisableInactiveRendering();
  } else {
    if (border_widget_)
      border_widget_->Hide();
  }
}

void BitpopBubbleDelegateView::OnWidgetActivationChanged(Widget* widget,
                                                   bool active) {
  if (close_on_deactivate() && widget == GetWidget() && !active)
    GetWidget()->Close();
}

void BitpopBubbleDelegateView::OnWidgetMoved(Widget* widget) {
  if (move_with_anchor() && anchor_widget() == widget)
    SizeToContents();
}

gfx::Rect BitpopBubbleDelegateView::GetAnchorRect() {
  if (!anchor_view())
    return gfx::Rect();
  gfx::Rect anchor_bounds = anchor_view()->GetBoundsInScreen();
  anchor_bounds.Inset(anchor_insets_);
  return anchor_bounds;
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
  // Explicitly set the content Widget's layered style and set transparency via
  // SetLayeredWindowAttributes. This is done because initializing the Widget as
  // transparent and setting opacity via UpdateLayeredWindow doesn't support
  // hosting child native Windows controls.
  const HWND hwnd = GetWidget()->GetNativeView();
  const DWORD style = GetWindowLong(hwnd, GWL_EXSTYLE);
  if ((opacity == 255) == !!(style & WS_EX_LAYERED))
    SetWindowLong(hwnd, GWL_EXSTYLE, style ^ WS_EX_LAYERED);
  SetLayeredWindowAttributes(hwnd, 0, opacity, LWA_ALPHA);
  // Update the border widget's opacity.
  border_widget_->SetOpacity(opacity);
#endif
  GetWidget()->SetOpacity(opacity);
}

void BitpopBubbleDelegateView::Init() {}

void BitpopBubbleDelegateView::SizeToContents() {
#if defined(OS_WIN) && !defined(USE_AURA)
  border_widget_->SetBounds(GetBubbleBounds());
  GetWidget()->SetBounds(GetBubbleClientBounds());

  // Update the local client bounds clipped out by the border widget background.
  // Used to correctly display overlapping semi-transparent widgets on Windows.
  GetBubbleFrameView()->bubble_border()->set_client_bounds(
      GetBubbleFrameView()->GetBoundsForClientView());
#else
  GetWidget()->SetBounds(GetBubbleBounds());
#endif
}

BitpopBubbleFrameView* BitpopBubbleDelegateView::GetBubbleFrameView() const {
  const Widget* widget = border_widget_ ? border_widget_ : GetWidget();
  const NonClientView* view = widget ? widget->non_client_view() : NULL;
  return view ? static_cast<BitpopBubbleFrameView*>(view->frame_view()) : NULL;
}

gfx::Rect BitpopBubbleDelegateView::GetBubbleBounds() {
  // The argument rect has its origin at the bubble's arrow anchor point;
  // its size is the preferred size of the bubble's client view (this view).
  return GetBubbleFrameView()->GetUpdatedWindowBounds(GetAnchorRect(),
      GetPreferredSize(), try_mirroring_arrow_);
}

#if defined(OS_WIN) && !defined(USE_AURA)
gfx::Rect BitpopBubbleDelegateView::GetBubbleClientBounds() const {
  gfx::Rect client_bounds(GetBubbleFrameView()->GetBoundsForClientView());
  client_bounds.Offset(border_widget_->GetWindowBoundsInScreen().origin());
  return client_bounds;
}
#endif

}  // namespace views
