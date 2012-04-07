// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/activation_controller.h"

#include "ash/shell.h"
#include "ash/shell_window_ids.h"
#include "ash/wm/window_modality_controller.h"
#include "ash/wm/window_util.h"
#include "base/auto_reset.h"
#include "ui/aura/client/activation_delegate.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/root_window.h"
#include "ui/aura/window.h"
#include "ui/aura/window_delegate.h"
#include "ui/base/ui_base_types.h"

namespace ash {
namespace internal {
namespace {

aura::Window* GetContainer(int id) {
  return Shell::GetInstance()->GetContainer(id);
}

// Returns true if children of |window| can be activated.
// These are the only containers in which windows can receive focus.
bool SupportsChildActivation(aura::Window* window) {
  return window->id() == kShellWindowId_DefaultContainer ||
         window->id() == kShellWindowId_AlwaysOnTopContainer ||
         window->id() == kShellWindowId_PanelContainer ||
         window->id() == kShellWindowId_SystemModalContainer ||
         window->id() == kShellWindowId_StatusContainer ||
         window->id() == kShellWindowId_LauncherContainer ||
         window->id() == kShellWindowId_LockScreenContainer ||
         window->id() == kShellWindowId_LockSystemModalContainer;
}

// Returns true if |window| can be activated or deactivated.
// A window manager typically defines some notion of "top level window" that
// supports activation/deactivation.
bool CanActivateWindow(aura::Window* window) {
  return window &&
      window->IsVisible() &&
      (!aura::client::GetActivationDelegate(window) ||
        aura::client::GetActivationDelegate(window)->ShouldActivate(NULL)) &&
      SupportsChildActivation(window->parent());
}

// When a modal window is activated, we bring its entire transient parent chain
// to the front. This function must be called before the modal transient is
// stacked at the top to ensure correct stacking order.
void StackTransientParentsBelowModalWindow(aura::Window* window) {
  if (window->GetIntProperty(aura::client::kModalKey) != ui::MODAL_TYPE_WINDOW)
    return;

  aura::Window* transient_parent = window->transient_parent();
  while (transient_parent) {
    transient_parent->parent()->StackChildAtTop(transient_parent);
    transient_parent = transient_parent->transient_parent();
  }
}

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// ActivationController, public:

ActivationController::ActivationController()
    : updating_activation_(false),
      default_container_for_test_(NULL) {
  aura::client::SetActivationClient(this);
  aura::RootWindow::GetInstance()->AddRootWindowObserver(this);
}

ActivationController::~ActivationController() {
  aura::RootWindow::GetInstance()->RemoveRootWindowObserver(this);
}

// static
aura::Window* ActivationController::GetActivatableWindow(aura::Window* window) {
  aura::Window* parent = window->parent();
  aura::Window* child = window;
  while (parent) {
    if (SupportsChildActivation(parent))
      return child;
    // If |child| isn't activatable, but has transient parent, trace
    // that path instead.
    if (child->transient_parent())
      return GetActivatableWindow(child->transient_parent());
    parent = parent->parent();
    child = child->parent();
  }
  return NULL;
}

////////////////////////////////////////////////////////////////////////////////
// ActivationController, aura::client::ActivationClient implementation:

void ActivationController::ActivateWindow(aura::Window* window) {
  aura::Window* window_modal_transient =
      WindowModalityController::GetWindowModalTransient(window);
  if (window_modal_transient) {
    ActivateWindow(window_modal_transient);
    return;
  }

  // Prevent recursion when called from focus.
  if (updating_activation_)
    return;

  AutoReset<bool> in_activate_window(&updating_activation_, true);
  if (!window)
    return;
  // Nothing may actually have changed.
  aura::Window* old_active = GetActiveWindow();
  if (old_active == window)
    return;
  // The stacking client may impose rules on what window configurations can be
  // activated or deactivated.
  if (!CanActivateWindow(window))
    return;
  // If the screen is locked, just bring the window to top so that
  // it will be activated when the lock window is destroyed.
  if (window && !window->CanReceiveEvents()) {
    StackTransientParentsBelowModalWindow(window);
    window->parent()->StackChildAtTop(window);
    return;
  }

  if (!window->Contains(window->GetFocusManager()->GetFocusedWindow()))
    window->GetFocusManager()->SetFocusedWindow(window);
  aura::RootWindow::GetInstance()->SetProperty(
      aura::client::kRootWindowActiveWindow,
      window);
  // Invoke OnLostActive after we've changed the active window. That way if the
  // delegate queries for active state it doesn't think the window is still
  // active.
  if (old_active && aura::client::GetActivationDelegate(old_active))
    aura::client::GetActivationDelegate(old_active)->OnLostActive();

  if (window) {
    StackTransientParentsBelowModalWindow(window);
    window->parent()->StackChildAtTop(window);
    if (aura::client::GetActivationDelegate(window))
      aura::client::GetActivationDelegate(window)->OnActivated();
  }
}

void ActivationController::DeactivateWindow(aura::Window* window) {
  if (window)
    ActivateNextWindow(window);
}

aura::Window* ActivationController::GetActiveWindow() {
  return reinterpret_cast<aura::Window*>(
      aura::RootWindow::GetInstance()->GetProperty(
          aura::client::kRootWindowActiveWindow));
}

bool ActivationController::CanFocusWindow(aura::Window* window) const {
  return CanActivateWindow(GetActivatableWindow(window));
}

////////////////////////////////////////////////////////////////////////////////
// ActivationController, aura::WindowObserver implementation:

void ActivationController::OnWindowVisibilityChanged(aura::Window* window,
                                                     bool visible) {
  if (!visible)
    ActivateNextWindow(window);
}

void ActivationController::OnWindowDestroying(aura::Window* window) {
  if (IsActiveWindow(window)) {
    // Clear the property before activating something else, since
    // ActivateWindow() will attempt to notify the window stored in this value
    // otherwise.
    aura::RootWindow::GetInstance()->SetProperty(
        aura::client::kRootWindowActiveWindow,
        NULL);
    ActivateWindow(GetTopmostWindowToActivate(window));
  }
  window->RemoveObserver(this);
}

////////////////////////////////////////////////////////////////////////////////
// ActivationController, aura::RootWindowObserver implementation:

void ActivationController::OnWindowInitialized(aura::Window* window) {
  window->AddObserver(this);
}

void ActivationController::OnWindowFocused(aura::Window* window) {
  ActivateWindow(GetActivatableWindow(window));
}

////////////////////////////////////////////////////////////////////////////////
// ActivationController, private:

void ActivationController::ActivateNextWindow(aura::Window* window) {
  if (IsActiveWindow(window))
    ActivateWindow(GetTopmostWindowToActivate(window));
}

aura::Window* ActivationController::GetTopmostWindowToActivate(
    aura::Window* ignore) const {
  const aura::Window* container =
      default_container_for_test_ ? default_container_for_test_ :
          GetContainer(kShellWindowId_DefaultContainer);
  // When destructing an active window that is in a container destructed after
  // the default container during shell shutdown, |container| would be NULL
  // because default container is destructed at this point.
  if (container) {
    for (aura::Window::Windows::const_reverse_iterator i =
             container->children().rbegin();
         i != container->children().rend();
         ++i) {
      if (*i != ignore && CanActivateWindow(*i))
        return *i;
    }
  }
  return NULL;
}

}  // namespace internal
}  // namespace ash
