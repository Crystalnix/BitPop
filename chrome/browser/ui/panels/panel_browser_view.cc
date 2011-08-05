// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/panels/panel_browser_view.h"

#include "base/logging.h"
#include "chrome/browser/ui/panels/panel.h"
#include "chrome/browser/ui/panels/panel_browser_frame_view.h"
#include "chrome/browser/ui/panels/panel_manager.h"
#include "chrome/browser/ui/views/frame/browser_frame.h"
#include "grit/chromium_strings.h"
#include "ui/base/animation/slide_animation.h"
#include "ui/base/l10n/l10n_util.h"
#include "views/window/window.h"

namespace {
// This value is experimental and subjective.
const int kSetBoundsAnimationMs = 200;
}

BrowserWindow* Panel::CreateNativePanel(Browser* browser, Panel* panel) {
  PanelBrowserView* view = new PanelBrowserView(browser, panel);
  (new BrowserFrame(view))->InitBrowserFrame();
  return view;
}

PanelBrowserView::PanelBrowserView(Browser* browser, Panel* panel)
  : BrowserView(browser),
    panel_(panel),
    mouse_pressed_(false),
    mouse_dragging_(false) {
}

PanelBrowserView::~PanelBrowserView() {
}

void PanelBrowserView::Init() {
  BrowserView::Init();

  GetWidget()->SetAlwaysOnTop(true);
  GetWindow()->non_client_view()->SetAccessibleName(
      l10n_util::GetStringUTF16(IDS_PRODUCT_NAME));
}

void PanelBrowserView::Close() {
  if (!panel_)
    return;

  // Check if the panel is in the closing process, i.e. Panel::Close() is
  // called.
#ifndef NDEBUG
  DCHECK(panel_->closing());
#endif

  // Cancel any currently running animation since we're closing down.
  if (bounds_animator_.get())
    bounds_animator_.reset();

  ::BrowserView::Close();
  panel_ = NULL;
}

void PanelBrowserView::SetBounds(const gfx::Rect& bounds) {
  // No animation if the panel is empty or being dragged.
  if (GetBounds().IsEmpty() || mouse_dragging_) {
    ::BrowserView::SetBounds(bounds);
    return;
  }

  animation_start_bounds_ = GetBounds();
  animation_target_bounds_ = bounds;

  if (!bounds_animator_.get()) {
    bounds_animator_.reset(new ui::SlideAnimation(this));
    bounds_animator_->SetSlideDuration(kSetBoundsAnimationMs);
  }

  if (bounds_animator_->IsShowing())
    bounds_animator_->Reset();
  bounds_animator_->Show();
}

void PanelBrowserView::UpdateTitleBar() {
  ::BrowserView::UpdateTitleBar();
  GetFrameView()->UpdateTitleBar();
}

bool PanelBrowserView::GetSavedWindowBounds(gfx::Rect* bounds) const {
  *bounds = panel_->GetRestoredBounds();
  return true;
}

void PanelBrowserView::OnWindowActivationChanged(bool active) {
  ::BrowserView::OnWindowActivationChanged(active);
  GetFrameView()->OnActivationChanged(active);
}

bool PanelBrowserView::AcceleratorPressed(
    const views::Accelerator& accelerator) {
  if (mouse_pressed_ && accelerator.key_code() == ui::VKEY_ESCAPE) {
    OnTitleBarMouseCaptureLost();
    return true;
  }
  return BrowserView::AcceleratorPressed(accelerator);
}

void PanelBrowserView::AnimationProgressed(const ui::Animation* animation) {
  gfx::Rect new_bounds = bounds_animator_->CurrentValueBetween(
      animation_start_bounds_, animation_target_bounds_);
  ::BrowserView::SetBounds(new_bounds);
}

PanelBrowserFrameView* PanelBrowserView::GetFrameView() const {
  return static_cast<PanelBrowserFrameView*>(frame()->GetFrameView());
}

bool PanelBrowserView::OnTitleBarMousePressed(const views::MouseEvent& event) {
  if (!event.IsOnlyLeftMouseButton())
    return false;
  mouse_pressed_ = true;
  mouse_pressed_point_ = event.location();
  mouse_dragging_ = false;
  panel_->manager()->StartDragging(panel_);
  return true;
}

bool PanelBrowserView::OnTitleBarMouseDragged(const views::MouseEvent& event) {
  if (!mouse_pressed_)
    return false;

  // We do not allow dragging vertically.
  int delta_x = event.location().x() - mouse_pressed_point_.x();
  if (!mouse_dragging_ && ExceededDragThreshold(delta_x, 0))
    mouse_dragging_ = true;
  if (mouse_dragging_)
    panel_->manager()->Drag(delta_x);
  return true;
}

bool PanelBrowserView::OnTitleBarMouseReleased(const views::MouseEvent& event) {
  return EndDragging(false);
}

bool PanelBrowserView::OnTitleBarMouseCaptureLost() {
  return EndDragging(true);
}

bool PanelBrowserView::EndDragging(bool cancelled) {
  // Only handle clicks that started in our window.
  if (!mouse_pressed_)
    return false;
  mouse_pressed_ = false;

  if (!mouse_dragging_)
    cancelled = true;
  mouse_dragging_ = false;
  panel_->manager()->EndDragging(cancelled);
  return true;
}
