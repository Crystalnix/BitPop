// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_WM_WORKSPACE_WORKSPACE_LAYOUT_MANAGER_H_
#define ASH_WM_WORKSPACE_WORKSPACE_LAYOUT_MANAGER_H_
#pragma once

#include "ash/ash_export.h"
#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "ui/aura/layout_manager.h"

namespace aura {
class MouseEvent;
class Window;
}

namespace gfx {
class Rect;
}

namespace ash {
namespace internal {

class WorkspaceManager;

// LayoutManager for top level windows when WorkspaceManager is enabled.
class ASH_EXPORT WorkspaceLayoutManager : public aura::LayoutManager {
 public:
  explicit WorkspaceLayoutManager(WorkspaceManager* workspace_manager);
  virtual ~WorkspaceLayoutManager();

  // Returns the workspace manager for this container.
  WorkspaceManager* workspace_manager() {
    return workspace_manager_;
  }

  // Invoked when a window receives drag event.
  void PrepareForMoveOrResize(aura::Window* drag, aura::MouseEvent* event);

  // Invoked when a drag event didn't start any drag operation.
  void CancelMoveOrResize(aura::Window* drag, aura::MouseEvent* event);

  // Invoked when a drag event moved the |window|.
  void ProcessMove(aura::Window* window, aura::MouseEvent* event);

  // Invoked when a user finished moving window.
  void EndMove(aura::Window* drag, aura::MouseEvent* event);

  // Invoked when a user finished resizing window.
  void EndResize(aura::Window* drag, aura::MouseEvent* event);

  // Overridden from aura::LayoutManager:
  virtual void OnWindowResized() OVERRIDE;
  virtual void OnWindowAddedToLayout(aura::Window* child) OVERRIDE;
  virtual void OnWillRemoveWindowFromLayout(aura::Window* child) OVERRIDE;
  virtual void OnChildWindowVisibilityChanged(aura::Window* child,
                                              bool visibile) OVERRIDE;
  virtual void SetChildBounds(aura::Window* child,
                              const gfx::Rect& requested_bounds) OVERRIDE;

 private:
  // Owned by WorkspaceController.
  WorkspaceManager* workspace_manager_;

  DISALLOW_COPY_AND_ASSIGN(WorkspaceLayoutManager);
};

}  // namespace internal
}  // namespace ash

#endif  // ASH_WM_WORKSPACE_WORKSPACE_LAYOUT_MANAGER_H_
