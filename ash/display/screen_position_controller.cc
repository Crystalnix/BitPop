// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/display/screen_position_controller.h"

#include "ash/display/display_controller.h"
#include "ash/shell.h"
#include "ash/shell_window_ids.h"
#include "ash/wm/coordinate_conversion.h"
#include "ash/wm/system_modal_container_layout_manager.h"
#include "ash/wm/window_properties.h"
#include "ui/aura/client/activation_client.h"
#include "ui/aura/client/capture_client.h"
#include "ui/aura/client/stacking_client.h"
#include "ui/aura/focus_manager.h"
#include "ui/aura/root_window.h"
#include "ui/aura/window_tracker.h"
#include "ui/gfx/display.h"
#include "ui/gfx/screen.h"

namespace ash {
namespace {

// Move all transient children to |dst_root|, including the ones in
// the child windows and transient children of the transient children.
void MoveAllTransientChildrenToNewRoot(aura::RootWindow* dst_root,
                                    aura::Window* window) {
  aura::Window::Windows transient_children = window->transient_children();
  for (aura::Window::Windows::iterator iter = transient_children.begin();
       iter != transient_children.end(); ++iter) {
    aura::Window* transient_child = *iter;
    int container_id = transient_child->parent()->id();
    DCHECK_GE(container_id, 0);
    aura::Window* container = Shell::GetContainer(dst_root, container_id);
    gfx::Rect parent_bounds_in_screen = transient_child->GetBoundsInScreen();
    container->AddChild(transient_child);
    transient_child->SetBoundsInScreen(parent_bounds_in_screen);

    // Transient children may have transient children.
    MoveAllTransientChildrenToNewRoot(dst_root,
                                   transient_child);
  }
  // Move transient children of the child windows if any.
  aura::Window::Windows children = window->children();
  for (aura::Window::Windows::iterator iter = children.begin();
       iter != children.end(); ++iter)
    MoveAllTransientChildrenToNewRoot(dst_root, *iter);
}

}  // namespace

namespace internal {

void ScreenPositionController::ConvertPointToScreen(
    const aura::Window* window,
    gfx::Point* point) {
  const aura::RootWindow* root = window->GetRootWindow();
  aura::Window::ConvertPointToWindow(window, root, point);
  if (DisplayController::IsExtendedDesktopEnabled()) {
    const gfx::Point display_origin =
        gfx::Screen::GetDisplayNearestWindow(
            const_cast<aura::RootWindow*>(root)).bounds().origin();
    point->Offset(display_origin.x(), display_origin.y());
  }
}

void ScreenPositionController::ConvertPointFromScreen(
    const aura::Window* window,
    gfx::Point* point) {
  const aura::RootWindow* root = window->GetRootWindow();
  if (DisplayController::IsExtendedDesktopEnabled()) {
    const gfx::Point display_origin =
        gfx::Screen::GetDisplayNearestWindow(
            const_cast<aura::RootWindow*>(root)).bounds().origin();
    point->Offset(-display_origin.x(), -display_origin.y());
  }
  aura::Window::ConvertPointToWindow(root, window, point);
}

void ScreenPositionController::SetBounds(
    aura::Window* window,
    const gfx::Rect& bounds) {
  if (!DisplayController::IsExtendedDesktopEnabled() ||
      !window->parent()->GetProperty(internal::kUsesScreenCoordinatesKey)) {
    window->SetBounds(bounds);
    return;
  }

  // Don't move a transient windows to other root window.
  // It moves when its transient_parent moves.
  if (!window->transient_parent()) {
    aura::RootWindow* dst_root = wm::GetRootWindowMatching(bounds);
    aura::Window* dst_container = NULL;
    if (dst_root != window->GetRootWindow()) {
      int container_id = window->parent()->id();
      // All containers that uses screen coordinates must have valid
      // window ids.
      DCHECK_GE(container_id, 0);
      // Don't move modal screen.
      if (!SystemModalContainerLayoutManager::IsModalScreen(window))
        dst_container = Shell::GetContainer(dst_root, container_id);
    }

    if (dst_container && window->parent() != dst_container) {
      aura::Window* focused = window->GetFocusManager()->GetFocusedWindow();
      aura::client::ActivationClient* activation_client =
          aura::client::GetActivationClient(window->GetRootWindow());
      aura::Window* active = activation_client->GetActiveWindow();

      aura::WindowTracker tracker;
      if (focused)
        tracker.Add(focused);
      if (active && focused != active)
        tracker.Add(active);

      dst_container->AddChild(window);

      MoveAllTransientChildrenToNewRoot(dst_root, window);

      // Restore focused/active window.
      if (tracker.Contains(focused))
        window->GetFocusManager()->SetFocusedWindow(focused, NULL);
      else if (tracker.Contains(active))
        activation_client->ActivateWindow(active);
    }
  }

  gfx::Point origin(bounds.origin());
  const gfx::Point display_origin =
      gfx::Screen::GetDisplayNearestWindow(window).bounds().origin();
  origin.Offset(-display_origin.x(), -display_origin.y());
  window->SetBounds(gfx::Rect(origin, bounds.size()));
}

}  // internal
}  // ash
