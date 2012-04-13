// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FACEBOOK_CHAT_BUBBLE_BUBBLE_DELEGATE_H_
#define CHROME_BROWSER_UI_VIEWS_FACEBOOK_CHAT_BUBBLE_BUBBLE_DELEGATE_H_
#pragma once

#include "chrome/browser/ui/views/facebook_chat/bubble/bubble_border.h"
#include "ui/base/animation/animation_delegate.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

namespace ui {
class SlideAnimation;
}  // namespace ui

class BitpopBubbleFrameView;

// BitpopBubbleDelegateView creates frame and client views for bubble Widgets.
// BitpopBubbleDelegateView itself is the client's contents view.
//
///////////////////////////////////////////////////////////////////////////////
class BitpopBubbleDelegateView : public views::WidgetDelegateView,
                                        public ui::AnimationDelegate,
                                        public views::Widget::Observer {
 public:
  // The default bubble background color.
  static const SkColor kBackgroundColor;

  BitpopBubbleDelegateView();
  BitpopBubbleDelegateView(views::View* anchor_view,
                     BitpopBubbleBorder::ArrowLocation arrow_location);
  virtual ~BitpopBubbleDelegateView();

  // Create and initialize the bubble Widget(s) with proper bounds.
  static views::Widget* CreateBubble(BitpopBubbleDelegateView* bubble_delegate);

  // WidgetDelegate overrides:
  virtual views::View* GetInitiallyFocusedView() OVERRIDE;
  virtual BitpopBubbleDelegateView* AsBubbleDelegate() OVERRIDE;
  virtual views::View* GetContentsView() OVERRIDE;
  virtual views::NonClientFrameView* CreateNonClientFrameView() OVERRIDE;

  // Widget::Observer overrides:
  virtual void OnWidgetClosing(views::Widget* widget) OVERRIDE;
  virtual void OnWidgetVisibilityChanged(views::Widget* widget, bool visible) OVERRIDE;
  virtual void OnWidgetActivationChanged(views::Widget* widget, bool active) OVERRIDE;

  bool close_on_esc() const { return close_on_esc_; }
  void set_close_on_esc(bool close_on_esc) { close_on_esc_ = close_on_esc; }

  bool close_on_deactivate() const { return close_on_deactivate_; }
  void set_close_on_deactivate(bool close_on_deactivate) {
      close_on_deactivate_ = close_on_deactivate;
  }

  views::View* anchor_view() const { return anchor_view_; }
  void set_anchor_view(views::View* anchor_view) { anchor_view_ = anchor_view; }

  BitpopBubbleBorder::ArrowLocation arrow_location() const { return arrow_location_; }
  void set_arrow_location(BitpopBubbleBorder::ArrowLocation arrow_location) {
      arrow_location_ = arrow_location;
  }

  SkColor color() const { return color_; }
  void set_color(SkColor color) { color_ = color; }

  int margin() const { return margin_; }
  void set_margin(int margin) { margin_ = margin; }

  bool use_focusless() const { return use_focusless_; }
  void set_use_focusless(bool use_focusless) {
    use_focusless_ = use_focusless;
  }

  // Get the arrow's anchor rect in screen space.
  virtual gfx::Rect GetAnchorRect();

  // Show the bubble's widget (and |border_widget_| on Windows).
  void Show();

  // Fade the bubble in or out via Widget transparency.
  // Fade in calls Widget::Show; fade out calls Widget::Close upon completion.
  void StartFade(bool fade_in);

  // Reset fade and opacity of bubble. Restore the opacity of the
  // bubble to the setting before StartFade() was called.
  void ResetFade();

  // Sets the bubble alignment relative to the anchor.
  void SetAlignment(BitpopBubbleBorder::BubbleAlignment alignment);

  // Resizes and potentially moves the BitpopBubble to best accommodate the
  // contents preferred size.
  void SizeToContents();

 protected:
  // View overrides:
  virtual bool AcceleratorPressed(const ui::Accelerator& accelerator) OVERRIDE;

  // ui::AnimationDelegate overrides:
  virtual void AnimationEnded(const ui::Animation* animation) OVERRIDE;
  virtual void AnimationProgressed(const ui::Animation* animation) OVERRIDE;

  // Perform view initialization on the contents for bubble sizing.
  virtual void Init();

  BitpopBubbleFrameView* GetBubbleFrameView() const;

 private:
  // Get bubble bounds from the anchor point and client view's preferred size.
  gfx::Rect GetBubbleBounds();

#if defined(OS_WIN) && !defined(USE_AURA)
  // Get bounds for the Windows-only widget that hosts the bubble's contents.
  gfx::Rect GetBubbleClientBounds() const;
#endif

  // Fade animation for bubble.
  scoped_ptr<ui::SlideAnimation> fade_animation_;

  // Flags controlling bubble closure on the escape key and deactivation.
  bool close_on_esc_;
  bool close_on_deactivate_;

  // The view hosting this bubble; the arrow is anchored to this view.
  views::View* anchor_view_;

  // The arrow's location on the bubble.
  BitpopBubbleBorder::ArrowLocation arrow_location_;

  // The background color of the bubble.
  SkColor color_;

  // The margin between the content and the inside of the border, in pixels.
  int margin_;

  // Original opacity of the bubble.
  int original_opacity_;

  // The widget hosting the border for this bubble (non-Aura Windows only).
  views::Widget* border_widget_;

  // Create a popup window for focusless bubbles on Linux/ChromeOS.
  // These bubbles are not interactive and should not gain focus.
  bool use_focusless_;

  DISALLOW_COPY_AND_ASSIGN(BitpopBubbleDelegateView);
};

#endif  // CHROME_BROWSER_UI_VIEWS_FACEBOOK_CHAT_BUBBLE_BUBBLE_DELEGATE_H_
