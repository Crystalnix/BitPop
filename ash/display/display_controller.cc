// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/display/display_controller.h"

#include "ash/ash_switches.h"
#include "ash/display/multi_display_manager.h"
#include "ash/root_window_controller.h"
#include "ash/screen_ash.h"
#include "ash/shell.h"
#include "ash/wm/coordinate_conversion.h"
#include "ash/wm/property_util.h"
#include "ash/wm/window_util.h"
#include "base/command_line.h"
#include "ui/aura/client/screen_position_client.h"
#include "ui/aura/env.h"
#include "ui/aura/root_window.h"
#include "ui/aura/window.h"
#include "ui/gfx/display.h"
#include "ui/gfx/screen.h"

namespace ash {
namespace internal {
namespace {
// True if the extended desktop mode is enabled.
bool extended_desktop_enabled = false;
}  // namespace

DisplayController::DisplayController()
    : secondary_display_layout_(RIGHT),
      dont_warp_mouse_(false) {
  aura::Env::GetInstance()->display_manager()->AddObserver(this);
}

DisplayController::~DisplayController() {
  aura::Env::GetInstance()->display_manager()->RemoveObserver(this);
  // Delete all root window controllers, which deletes root window
  // from the last so that the primary root window gets deleted last.
  for (std::map<int, aura::RootWindow*>::const_reverse_iterator it =
           root_windows_.rbegin(); it != root_windows_.rend(); ++it) {
    internal::RootWindowController* controller =
        GetRootWindowController(it->second);
    // RootWindow may not have RootWindowController in non
    // extended desktop mode.
    if (controller)
      delete controller;
    else
      delete it->second;
  }
}

void DisplayController::InitPrimaryDisplay() {
  aura::DisplayManager* display_manager =
      aura::Env::GetInstance()->display_manager();
  const gfx::Display* display = display_manager->GetDisplayAt(0);
  DCHECK_EQ(0, display->id());
  aura::RootWindow* root = AddRootWindowForDisplay(*display);
  root->SetHostBounds(display->bounds_in_pixel());
}

void DisplayController::InitSecondaryDisplays() {
  aura::DisplayManager* display_manager =
      aura::Env::GetInstance()->display_manager();
  for (size_t i = 1; i < display_manager->GetNumDisplays(); ++i) {
    const gfx::Display* display = display_manager->GetDisplayAt(i);
    aura::RootWindow* root = AddRootWindowForDisplay(*display);
    Shell::GetInstance()->InitRootWindowForSecondaryDisplay(root);
  }
  UpdateDisplayBoundsForLayout();
}

aura::RootWindow* DisplayController::GetPrimaryRootWindow() {
  DCHECK(!root_windows_.empty());
  return root_windows_[0];
}

aura::RootWindow* DisplayController::GetRootWindowForDisplayId(int id) {
  return root_windows_[id];
}

void DisplayController::CloseChildWindows() {
  for (std::map<int, aura::RootWindow*>::const_iterator it =
           root_windows_.begin(); it != root_windows_.end(); ++it) {
    aura::RootWindow* root_window = it->second;
    internal::RootWindowController* controller =
        GetRootWindowController(root_window);
    if (controller) {
      controller->CloseChildWindows();
    } else {
      while (!root_window->children().empty()) {
        aura::Window* child = root_window->children()[0];
        delete child;
      }
    }
  }
}

std::vector<aura::RootWindow*> DisplayController::GetAllRootWindows() {
  std::vector<aura::RootWindow*> windows;
  for (std::map<int, aura::RootWindow*>::const_iterator it =
           root_windows_.begin(); it != root_windows_.end(); ++it) {
    DCHECK(it->second);
    if (GetRootWindowController(it->second))
      windows.push_back(it->second);
  }
  return windows;
}

std::vector<internal::RootWindowController*>
DisplayController::GetAllRootWindowControllers() {
  std::vector<internal::RootWindowController*> controllers;
  for (std::map<int, aura::RootWindow*>::const_iterator it =
           root_windows_.begin(); it != root_windows_.end(); ++it) {
    internal::RootWindowController* controller =
        GetRootWindowController(it->second);
    if (controller)
      controllers.push_back(controller);
  }
  return controllers;
}

void DisplayController::SetSecondaryDisplayLayout(
    SecondaryDisplayLayout layout) {
  secondary_display_layout_ = layout;
  UpdateDisplayBoundsForLayout();
}

bool DisplayController::WarpMouseCursorIfNecessary(
    aura::RootWindow* current_root,
    const gfx::Point& point_in_root) {
  if (root_windows_.size() < 2 || dont_warp_mouse_)
    return false;

  // The pointer might be outside the |current_root|. Get the root window where
  // the pointer is currently on.
  std::pair<aura::RootWindow*, gfx::Point> actual_location =
      wm::GetRootWindowRelativeToWindow(current_root, point_in_root);
  current_root = actual_location.first;
  // Don't use |point_in_root| below. Instead, use |actual_location.second|
  // which is in |actual_location.first|'s coordinates.

  gfx::Rect root_bounds = current_root->bounds();
  int offset_x = 0;
  int offset_y = 0;
  if (actual_location.second.x() <= root_bounds.x()) {
    // Use -2, not -1, to avoid infinite loop of pointer warp.
    offset_x = -2;
  } else if (actual_location.second.x() >= root_bounds.right() - 1) {
    offset_x = 2;
  } else if (actual_location.second.y() <= root_bounds.y()) {
    offset_y = -2;
  } else if (actual_location.second.y() >= root_bounds.bottom() - 1) {
    offset_y = 2;
  } else {
    return false;
  }

  gfx::Point point_in_screen(actual_location.second);
  wm::ConvertPointToScreen(current_root, &point_in_screen);
  point_in_screen.Offset(offset_x, offset_y);

  aura::RootWindow* dst_root = wm::GetRootWindowAt(point_in_screen);
  gfx::Point point_in_dst_root(point_in_screen);
  wm::ConvertPointFromScreen(dst_root, &point_in_dst_root);

  if (dst_root->bounds().Contains(point_in_dst_root)) {
    DCHECK_NE(dst_root, current_root);
    dst_root->MoveCursorTo(point_in_dst_root);
    return true;
  }
  return false;
}

void DisplayController::OnDisplayBoundsChanged(const gfx::Display& display) {
  root_windows_[display.id()]->SetHostBounds(display.bounds_in_pixel());
  UpdateDisplayBoundsForLayout();
}

void DisplayController::OnDisplayAdded(const gfx::Display& display) {
  if (root_windows_.empty()) {
    DCHECK_EQ(0, display.id());
    root_windows_[display.id()] = Shell::GetPrimaryRootWindow();
    Shell::GetPrimaryRootWindow()->SetHostBounds(display.bounds_in_pixel());
    return;
  }
  aura::RootWindow* root = AddRootWindowForDisplay(display);
  Shell::GetInstance()->InitRootWindowForSecondaryDisplay(root);
  UpdateDisplayBoundsForLayout();
}

void DisplayController::OnDisplayRemoved(const gfx::Display& display) {
  aura::RootWindow* root = root_windows_[display.id()];
  DCHECK(root);
  // Primary display should never be removed by DisplayManager.
  DCHECK(root != Shell::GetPrimaryRootWindow());
  // Display for root window will be deleted when the Primary RootWindow
  // is deleted by the Shell.
  if (root != Shell::GetPrimaryRootWindow()) {
    root_windows_.erase(display.id());
    internal::RootWindowController* controller =
        GetRootWindowController(root);
    if (controller) {
      controller->MoveWindowsTo(Shell::GetPrimaryRootWindow());
      delete controller;
    } else {
      delete root;
    }
  }
}

// static
bool DisplayController::IsExtendedDesktopEnabled(){
  return extended_desktop_enabled ||
      CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kAshExtendedDesktop);
}

// static
void DisplayController::SetExtendedDesktopEnabled(bool enabled) {
  extended_desktop_enabled = enabled;
}

aura::RootWindow* DisplayController::AddRootWindowForDisplay(
    const gfx::Display& display) {
  aura::RootWindow* root = aura::Env::GetInstance()->display_manager()->
      CreateRootWindowForDisplay(display);
  root_windows_[display.id()] = root;
  // Confine the cursor within the window if
  // 1) Extended desktop is enabled or
  // 2) the display is primary display and the host window
  // is set to be fullscreen (this is old behavior).
  if (IsExtendedDesktopEnabled() ||
      (aura::DisplayManager::use_fullscreen_host_window() &&
       display.id() == 0)) {
    root->ConfineCursorToWindow();
  }
  return root;
}

void DisplayController::UpdateDisplayBoundsForLayout() {
  if (!IsExtendedDesktopEnabled() || gfx::Screen::GetNumDisplays() <= 1) {
    return;
  }
  DCHECK_EQ(2, gfx::Screen::GetNumDisplays());
  aura::DisplayManager* display_manager =
      aura::Env::GetInstance()->display_manager();
  const gfx::Rect& primary_bounds = display_manager->GetDisplayAt(0)->bounds();
  gfx::Display* secondary_display = display_manager->GetDisplayAt(1);
  const gfx::Rect& secondary_bounds = secondary_display->bounds();
  gfx::Point new_secondary_origin = primary_bounds.origin();

  // TODO(oshima|mukai): Implement more flexible layout.
  switch (secondary_display_layout_) {
    case TOP:
      new_secondary_origin.Offset(0, -secondary_bounds.height());
      break;
    case RIGHT:
      new_secondary_origin.Offset(primary_bounds.width(), 0);
      break;
    case BOTTOM:
      new_secondary_origin.Offset(0, primary_bounds.height());
      break;
    case LEFT:
      new_secondary_origin.Offset(-secondary_bounds.width(), 0);
      break;
  }
  gfx::Insets insets = secondary_display->GetWorkAreaInsets();
  secondary_display->set_bounds(
      gfx::Rect(new_secondary_origin, secondary_bounds.size()));
  secondary_display->UpdateWorkAreaFromInsets(insets);
}

}  // namespace internal
}  // namespace ash
