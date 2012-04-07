// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/workspace_controller.h"

#include "ash/shell.h"
#include "ash/wm/window_util.h"
#include "ash/wm/workspace/workspace_event_filter.h"
#include "ash/wm/workspace/workspace_layout_manager.h"
#include "ash/wm/workspace/workspace_manager.h"
#include "base/utf_string_conversions.h"
#include "ui/aura/client/activation_client.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/root_window.h"
#include "ui/aura/window.h"
#include "ui/views/controls/menu/menu_model_adapter.h"
#include "ui/views/controls/menu/menu_runner.h"

namespace ash {
namespace internal {

namespace {

// Size of the grid when a grid is enabled.
const int kGridSize = 8;

}  // namespace

WorkspaceController::WorkspaceController(aura::Window* viewport)
    : viewport_(viewport),
      workspace_manager_(new WorkspaceManager(viewport)),
      layout_manager_(NULL),
      event_filter_(NULL) {
  event_filter_ = new WorkspaceEventFilter(viewport);
  viewport->SetEventFilter(event_filter_);
  layout_manager_ = new WorkspaceLayoutManager(workspace_manager_.get());
  viewport->SetLayoutManager(layout_manager_);
  aura::RootWindow::GetInstance()->AddRootWindowObserver(this);
  aura::RootWindow::GetInstance()->AddObserver(this);
  workspace_manager_->set_grid_size(kGridSize);
  event_filter_->set_grid_size(kGridSize);
}

WorkspaceController::~WorkspaceController() {
  aura::RootWindow::GetInstance()->RemoveObserver(this);
  aura::RootWindow::GetInstance()->RemoveRootWindowObserver(this);
}

void WorkspaceController::ToggleOverview() {
  workspace_manager_->SetOverview(!workspace_manager_->is_overview());
}

void WorkspaceController::ShowMenu(views::Widget* widget,
                                   const gfx::Point& location) {
  ui::SimpleMenuModel menu_model(this);
  // This is just for testing and will be ripped out before we ship, so none of
  // the strings are localized.
  menu_model.AddCheckItem(MENU_SNAP_TO_GRID,
                          ASCIIToUTF16("Snap to grid"));
  menu_model.AddCheckItem(MENU_OPEN_MAXIMIZED,
                          ASCIIToUTF16("Maximize new windows"));
  views::MenuModelAdapter menu_model_adapter(&menu_model);
  menu_runner_.reset(new views::MenuRunner(menu_model_adapter.CreateMenu()));
  if (menu_runner_->RunMenuAt(
          widget, NULL, gfx::Rect(location, gfx::Size()),
          views::MenuItemView::TOPRIGHT, views::MenuRunner::HAS_MNEMONICS) ==
      views::MenuRunner::MENU_DELETED)
    return;
}

void WorkspaceController::OnRootWindowResized(const gfx::Size& new_size) {
  workspace_manager_->SetWorkspaceSize(new_size);
}

void WorkspaceController::OnWindowPropertyChanged(aura::Window* window,
                                                  const char* key,
                                                  void* old) {
  if (key == aura::client::kRootWindowActiveWindow)
    workspace_manager_->SetActiveWorkspaceByWindow(GetActiveWindow());
}

bool WorkspaceController::IsCommandIdChecked(int command_id) const {
  switch (static_cast<MenuItem>(command_id)) {
    case MENU_SNAP_TO_GRID:
      return workspace_manager_->grid_size() != 0;

    case MENU_OPEN_MAXIMIZED:
      return workspace_manager_->open_new_windows_maximized();

    default:
      break;
  }
  return false;
}

bool WorkspaceController::IsCommandIdEnabled(int command_id) const {
  switch (static_cast<MenuItem>(command_id)) {
    case MENU_OPEN_MAXIMIZED:
      return workspace_manager_->contents_view()->bounds().width() <
          WorkspaceManager::kOpenMaximizedThreshold;

    default:
      return true;
  }
  return true;
}

void WorkspaceController::ExecuteCommand(int command_id) {
  switch (static_cast<MenuItem>(command_id)) {
    case MENU_SNAP_TO_GRID: {
      int size = workspace_manager_->grid_size() == 0 ? kGridSize : 0;
      workspace_manager_->set_grid_size(size);
      event_filter_->set_grid_size(size);
      if (!size)
        return;
      for (size_t i = 0; i < viewport_->children().size(); ++i) {
        aura::Window* window = viewport_->children()[i];
        if (!window_util::IsWindowMaximized(window) &&
            !window_util::IsWindowFullscreen(window)) {
          window->SetBounds(workspace_manager_->AlignBoundsToGrid(
                                window->GetTargetBounds()));
        }
      }
      break;
    }

    case MENU_OPEN_MAXIMIZED:
      workspace_manager_->set_open_new_windows_maximized(
          !workspace_manager_->open_new_windows_maximized());
      break;
  }
}

bool WorkspaceController::GetAcceleratorForCommandId(
      int command_id,
      ui::Accelerator* accelerator) {
  return false;
}

}  // namespace internal
}  // namespace ash
