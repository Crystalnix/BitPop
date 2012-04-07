// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/system_modal_container_layout_manager.h"

#include "ash/ash_switches.h"
#include "ash/shell.h"
#include "ash/shell_window_ids.h"
#include "ash/wm/system_modal_container_event_filter.h"
#include "ash/wm/window_animations.h"
#include "ash/wm/window_util.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/event.h"
#include "ui/aura/root_window.h"
#include "ui/aura/window.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/compositor/layer.h"
#include "ui/gfx/compositor/layer_animator.h"
#include "ui/gfx/compositor/scoped_layer_animation_settings.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace ash {
namespace internal {

namespace {

class ScreenView : public views::View {
 public:
  ScreenView() {}
  virtual ~ScreenView() {}

  // Overridden from views::View:
  virtual void OnPaint(gfx::Canvas* canvas) OVERRIDE {
    canvas->FillRect(GetOverlayColor(), GetLocalBounds());
  }

 private:
  SkColor GetOverlayColor() {
    if (CommandLine::ForCurrentProcess()->HasSwitch(
            switches::kAuraGoogleDialogFrames)) {
      return SK_ColorWHITE;
    }
    return SK_ColorBLACK;
  }

  DISALLOW_COPY_AND_ASSIGN(ScreenView);
};

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// SystemModalContainerLayoutManager, public:

SystemModalContainerLayoutManager::SystemModalContainerLayoutManager(
    aura::Window* container)
    : container_(container),
      modal_screen_(NULL),
      ALLOW_THIS_IN_INITIALIZER_LIST(modality_filter_(
          new SystemModalContainerEventFilter(container, this))) {
}

SystemModalContainerLayoutManager::~SystemModalContainerLayoutManager() {
}

////////////////////////////////////////////////////////////////////////////////
// SystemModalContainerLayoutManager, aura::LayoutManager implementation:

void SystemModalContainerLayoutManager::OnWindowResized() {
  if (modal_screen_) {
    modal_screen_->SetBounds(gfx::Rect(0, 0, container_->bounds().width(),
                                       container_->bounds().height()));
  }
}

void SystemModalContainerLayoutManager::OnWindowAddedToLayout(
    aura::Window* child) {
  DCHECK((modal_screen_ && child == modal_screen_->GetNativeView()) ||
         child->type() == aura::client::WINDOW_TYPE_NORMAL ||
         child->type() == aura::client::WINDOW_TYPE_POPUP);
  child->AddObserver(this);
  if (child->GetIntProperty(aura::client::kModalKey))
    AddModalWindow(child);
}

void SystemModalContainerLayoutManager::OnWillRemoveWindowFromLayout(
    aura::Window* child) {
  child->RemoveObserver(this);
  if (child->GetIntProperty(aura::client::kModalKey))
    RemoveModalWindow(child);
}

void SystemModalContainerLayoutManager::OnChildWindowVisibilityChanged(
    aura::Window* child,
    bool visible) {
}

void SystemModalContainerLayoutManager::SetChildBounds(
    aura::Window* child,
    const gfx::Rect& requested_bounds) {
  SetChildBoundsDirect(child, requested_bounds);
}

////////////////////////////////////////////////////////////////////////////////
// SystemModalContainerLayoutManager, aura::WindowObserver implementation:

void SystemModalContainerLayoutManager::OnWindowPropertyChanged(
    aura::Window* window,
    const char* key,
    void* old) {
  if (key != aura::client::kModalKey)
    return;

  if (window->GetIntProperty(aura::client::kModalKey)) {
    AddModalWindow(window);
  } else if (static_cast<int>(reinterpret_cast<intptr_t>(old))) {
    RemoveModalWindow(window);
  }
}

////////////////////////////////////////////////////////////////////////////////
// SystemModalContainerLayoutManager, ui::LayerAnimationObserver implementation:

void SystemModalContainerLayoutManager::OnLayerAnimationEnded(
    const ui::LayerAnimationSequence* sequence) {
  if (modal_screen_ && !modal_screen_->GetNativeView()->layer()->ShouldDraw())
    DestroyModalScreen();
}

void SystemModalContainerLayoutManager::OnLayerAnimationAborted(
    const ui::LayerAnimationSequence* sequence) {
}

void SystemModalContainerLayoutManager::OnLayerAnimationScheduled(
    const ui::LayerAnimationSequence* sequence) {
}

////////////////////////////////////////////////////////////////////////////////
// SystemModalContainerLayoutManager,
//     SystemModalContainerEventFilter::Delegate implementation:

bool SystemModalContainerLayoutManager::CanWindowReceiveEvents(
    aura::Window* window) {
  // This container can not handle events if the screen is locked and it is not
  // above the lock screen layer (crbug.com/110920).
  if (ash::Shell::GetInstance()->IsScreenLocked() &&
      container_->id() < ash::internal::kShellWindowId_LockScreenContainer)
    return true;
  return GetActivatableWindow(window) == modal_window();
}

////////////////////////////////////////////////////////////////////////////////
// SystemModalContainerLayoutManager, private:

void SystemModalContainerLayoutManager::AddModalWindow(aura::Window* window) {
  modal_windows_.push_back(window);
  CreateModalScreen();
}

void SystemModalContainerLayoutManager::RemoveModalWindow(
    aura::Window* window) {
  aura::Window::Windows::iterator it =
      std::find(modal_windows_.begin(), modal_windows_.end(), window);
  if (it != modal_windows_.end())
    modal_windows_.erase(it);

  if (modal_windows_.empty())
    HideModalScreen();
  else
    ash::ActivateWindow(modal_window());
}

void SystemModalContainerLayoutManager::CreateModalScreen() {
  if (modal_screen_)
    return;
  modal_screen_ = new views::Widget;
  views::Widget::InitParams params(views::Widget::InitParams::TYPE_CONTROL);
  params.parent = container_;
  params.bounds = gfx::Rect(0, 0, container_->bounds().width(),
                            container_->bounds().height());
  modal_screen_->Init(params);
  modal_screen_->GetNativeView()->SetName(
      "SystemModalContainerLayoutManager.ModalScreen");
  modal_screen_->SetContentsView(new ScreenView);
  modal_screen_->GetNativeView()->layer()->SetOpacity(0.0f);
  modal_screen_->GetNativeView()->layer()->GetAnimator()->AddObserver(this);

  Shell::GetInstance()->AddRootWindowEventFilter(modality_filter_.get());

  ui::ScopedLayerAnimationSettings settings(
      modal_screen_->GetNativeView()->layer()->GetAnimator());
  modal_screen_->Show();
  modal_screen_->GetNativeView()->layer()->SetOpacity(0.5f);
  container_->StackChildAtTop(modal_screen_->GetNativeView());
}

void SystemModalContainerLayoutManager::DestroyModalScreen() {
  modal_screen_->GetNativeView()->layer()->GetAnimator()->RemoveObserver(this);
  modal_screen_->Close();
  modal_screen_ = NULL;
}

void SystemModalContainerLayoutManager::HideModalScreen() {
  Shell::GetInstance()->RemoveRootWindowEventFilter(modality_filter_.get());
  ui::ScopedLayerAnimationSettings settings(
      modal_screen_->GetNativeView()->layer()->GetAnimator());
  modal_screen_->GetNativeView()->layer()->SetOpacity(0.0f);
}

}  // namespace internal
}  // namespace ash
