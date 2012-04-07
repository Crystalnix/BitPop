// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_WM_WORKSPACE_CONTROLLER_H_
#define ASH_WM_WORKSPACE_CONTROLLER_H_
#pragma once

#include "ash/ash_export.h"
#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "ui/aura/root_window_observer.h"
#include "ui/aura/window_observer.h"
#include "ui/base/models/simple_menu_model.h"

namespace aura {
class Window;
}

namespace gfx {
class Point;
class Size;
}

namespace views {
class MenuRunner;
class Widget;
}

namespace ash {
namespace internal {

class WorkspaceEventFilter;
class WorkspaceLayoutManager;
class WorkspaceManager;

// WorkspaceController acts as a central place that ties together all the
// various workspace pieces: WorkspaceManager, WorkspaceLayoutManager and
// WorkspaceEventFilter.
class ASH_EXPORT WorkspaceController :
      public aura::RootWindowObserver,
      public aura::WindowObserver,
      public ui::SimpleMenuModel::Delegate {
 public:
  explicit WorkspaceController(aura::Window* viewport);
  virtual ~WorkspaceController();

  void ToggleOverview();

  // Returns the workspace manager that this controler owns.
  WorkspaceManager* workspace_manager() {
    return workspace_manager_.get();
  }

  // Shows the menu allowing you to configure various aspects of workspaces.
  void ShowMenu(views::Widget* widget, const gfx::Point& location);

  // aura::RootWindowObserver overrides:
  virtual void OnRootWindowResized(const gfx::Size& new_size) OVERRIDE;

  // aura::WindowObserver overrides:
  virtual void OnWindowPropertyChanged(aura::Window* window,
                                       const char* key,
                                       void* old) OVERRIDE;

  // ui::SimpleMenuModel::Delegate overrides:
  virtual bool IsCommandIdChecked(int command_id) const OVERRIDE;
  virtual bool IsCommandIdEnabled(int command_id) const OVERRIDE;
  virtual void ExecuteCommand(int command_id) OVERRIDE;
  virtual bool GetAcceleratorForCommandId(
      int command_id,
      ui::Accelerator* accelerator) OVERRIDE;

 private:
  enum MenuItem {
    MENU_SNAP_TO_GRID,
    MENU_OPEN_MAXIMIZED,
  };

  aura::Window* viewport_;

  scoped_ptr<WorkspaceManager> workspace_manager_;

  // Owned by the window its attached to.
  WorkspaceLayoutManager* layout_manager_;

  // Owned the window set on.
  WorkspaceEventFilter* event_filter_;

  scoped_ptr<views::MenuRunner> menu_runner_;

  DISALLOW_COPY_AND_ASSIGN(WorkspaceController);
};

}  // namespace internal
}  // namespace ash

#endif  // ASH_WM_WORKSPACE_CONTROLLER_H_
