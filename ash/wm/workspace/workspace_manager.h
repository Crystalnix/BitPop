// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_WM_WORKSPACE_MANAGER_H_
#define ASH_WM_WORKSPACE_MANAGER_H_

#include <vector>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "ash/ash_export.h"
#include "ui/aura/window_observer.h"
#include "ui/gfx/insets.h"
#include "ui/gfx/size.h"

namespace aura {
class Window;
}

namespace gfx {
class Point;
class Rect;
}

namespace ash {
namespace internal {
class Workspace;
class WorkspaceManagerTest;

// WorkspaceManager manages multiple workspaces in the desktop.
class ASH_EXPORT WorkspaceManager : public aura::WindowObserver{
 public:
  // If open_new_windows_maximized() is true and the size of the viewport is
  // smaller than this value, newly created windows are forced maximized.
  static const int kOpenMaximizedThreshold;

  explicit WorkspaceManager(aura::Window* viewport);
  virtual ~WorkspaceManager();

  // Returns true if |window| should be managed by the WorkspaceManager.
  bool IsManagedWindow(aura::Window* window) const;

  // Returns true if |window| should be maximized.
  bool ShouldMaximize(aura::Window* window) const;

  // Adds/removes a window creating/destroying workspace as necessary.
  void AddWindow(aura::Window* window);
  void RemoveWindow(aura::Window* window);

  // Activates the workspace containing |window|. Does nothing if |window| is
  // NULL or not contained in a workspace.
  void SetActiveWorkspaceByWindow(aura::Window* window);

  // Returns the Window this WorkspaceManager controls.
  aura::Window* contents_view() { return contents_view_; }

  // Returns the window for rotate operation based on the |location|.
  // TODO: this isn't currently used; remove if we do away with overview.
  aura::Window* FindRotateWindowForLocation(const gfx::Point& location);

  // Returns the bounds in which a window can be moved/resized.
  gfx::Rect GetDragAreaBounds();

  // Turn on/off overview mode.
  void SetOverview(bool overview);
  bool is_overview() const { return is_overview_; }

  // Sets the size of a single workspace (all workspaces have the same size).
  void SetWorkspaceSize(const gfx::Size& workspace_size);

  // Returns the window the layout manager should allow the size to be set for.
  // TODO: maybe this should be set on WorkspaceLayoutManager.
  aura::Window* ignored_window() { return ignored_window_; }

  // Sets whether newly added windows open maximized. This is only applicable if
  // the size of the root window is less than kOpenMaximizedThreshold. Default
  // is true.
  void set_open_new_windows_maximized(bool value) {
    open_new_windows_maximized_ = value;
  }
  bool open_new_windows_maximized() const {
    return open_new_windows_maximized_;
  }

  // Sets the size of the grid. Newly added windows are forced to align to the
  // size of the grid.
  void set_grid_size(int size) { grid_size_ = size; }
  int grid_size() const { return grid_size_; }

  // Returns a bounds aligned to the grid. Returns |bounds| if grid_size is 0.
  gfx::Rect AlignBoundsToGrid(const gfx::Rect& bounds);

  // Overriden from aura::WindowObserver:
  virtual void OnWindowPropertyChanged(aura::Window* window,
                                       const char* name,
                                       void* old) OVERRIDE;

 private:
  friend class Workspace;
  friend class WorkspaceManagerTest;

  // See description above getter.
  void set_ignored_window(aura::Window* ignored_window) {
    ignored_window_ = ignored_window;
  }

  void AddWorkspace(Workspace* workspace);
  void RemoveWorkspace(Workspace* workspace);

  // Returns the active workspace.
  Workspace* GetActiveWorkspace() const;

  // Returns the workspace that contanis the |window|.
  Workspace* FindBy(aura::Window* window) const;

  // Sets the active workspace.
  void SetActiveWorkspace(Workspace* workspace);

  // Returns the bounds of the work area.
  gfx::Rect GetWorkAreaBounds();

  // Returns the index of the workspace that contains the |window|.
  int GetWorkspaceIndexContaining(aura::Window* window) const;

  // Sets the bounds of |window|. This sets |ignored_window_| to |window| so
  // that the bounds change is allowed through.
  void SetWindowBounds(aura::Window* window, const gfx::Rect& bounds);

  // Resets the bounds of |window| to its restored bounds (if set), ensuring
  // it fits in the the windows current workspace.
  void SetWindowBoundsFromRestoreBounds(aura::Window* window);

  // Reset the bounds of |window|. Use when |window| is fullscreen or maximized.
  void SetFullScreenOrMaximizedBounds(aura::Window* window);

  // Invoked when the type of workspace needed for |window| changes.
  void OnTypeOfWorkspacedNeededChanged(aura::Window* window);

  // Returns the Workspace whose type is TYPE_NORMAL, or NULL if there isn't
  // one.
  Workspace* GetNormalWorkspace();

  aura::Window* contents_view_;

  Workspace* active_workspace_;

  std::vector<Workspace*> workspaces_;

  // The size of a single workspace. This is generally the same as the size of
  // monitor.
  gfx::Size workspace_size_;

  // True if the workspace manager is in overview mode.
  bool is_overview_;

  // The window that WorkspaceManager does not set the bounds on.
  aura::Window* ignored_window_;

  // See description above setter.
  int grid_size_;

  // See description above setter.
  bool open_new_windows_maximized_;

  DISALLOW_COPY_AND_ASSIGN(WorkspaceManager);
};

}  // namespace internal
}  // namespace ash

#endif  // ASH_WM_WORKSPACE_MANAGER_H_
