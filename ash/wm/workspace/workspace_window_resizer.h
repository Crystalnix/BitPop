// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_WM_WORKSPACE_WINDOW_RESIZER_H_
#define ASH_WM_WORKSPACE_WINDOW_RESIZER_H_

#include <vector>

#include "ash/wm/window_resizer.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"

namespace ash {
namespace internal {

class PhantomWindowController;
class SnapSizer;

// WindowResizer implementation for workspaces. This enforces that windows are
// not allowed to vertically move or resize outside of the work area. As windows
// are moved outside the work area they are shrunk. We remember the height of
// the window before it was moved so that if the window is again moved up we
// attempt to restore the old height.
class ASH_EXPORT WorkspaceWindowResizer : public WindowResizer {
 public:
  // When dragging an attached window this is the min size we'll make sure is
  // visible. In the vertical direction we take the max of this and that from
  // the delegate.
  static const int kMinOnscreenSize;

  // Min height we'll force on screen when dragging the caption.
  // TODO: this should come from a property on the window.
  static const int kMinOnscreenHeight;

  virtual ~WorkspaceWindowResizer();

  static WorkspaceWindowResizer* Create(
      aura::Window* window,
      const gfx::Point& location_in_parent,
      int window_component,
      const std::vector<aura::Window*>& attached_windows);

  // Returns true if the drag will result in changing the window in anyway.
  bool is_resizable() const { return details_.is_resizable; }

  const gfx::Point& initial_location_in_parent() const {
    return details_.initial_location_in_parent;
  }

  // Overridden from WindowResizer:
  virtual void Drag(const gfx::Point& location, int event_flags) OVERRIDE;
  virtual void CompleteDrag(int event_flags) OVERRIDE;
  virtual void RevertDrag() OVERRIDE;

 private:
  WorkspaceWindowResizer(const Details& details,
                         const std::vector<aura::Window*>& attached_windows);

 private:
  // Type of snapping.
  enum SnapType {
    // Snap to the left/right edge of the screen.
    SNAP_LEFT_EDGE,
    SNAP_RIGHT_EDGE,

    // No snap position.
    SNAP_NONE
  };

  // Returns the final bounds to place the window at. This differs from
  // the current if there is a grid.
  gfx::Rect GetFinalBounds(const gfx::Rect& bounds, int grid_size) const;

  // Lays out the attached windows. |bounds| is the bounds of the main window.
  void LayoutAttachedWindows(const gfx::Rect& bounds, int grid_size);

  // Calculates the size (along the primary axis) of the attached windows.
  // |initial_size| is the initial size of the main window, |current_size| the
  // new size of the main window, |start| the position to layout the attached
  // windows from and |end| the coordinate to position to.
  void CalculateAttachedSizes(
      int initial_size,
      int current_size,
      int start,
      int end,
      int grid_size,
      std::vector<int>* sizes) const;

  // Adjusts the bounds to enforce that windows are vertically contained in the
  // work area.
  void AdjustBoundsForMainWindow(gfx::Rect* bounds, int grid_size) const;

  // Snaps the window bounds to the work area edges if necessary.
  void SnapToWorkAreaEdges(
      const gfx::Rect& work_area,
      gfx::Rect* bounds,
      int grid_size) const;

  // Returns true if the window touches the bottom edge of the work area.
  bool TouchesBottomOfScreen() const;

  // Returns a coordinate along the primary axis. Used to share code for
  // left/right multi window resize and top/bottom resize.
  int PrimaryAxisSize(const gfx::Size& size) const;
  int PrimaryAxisCoordinate(int x, int y) const;

  // Updates the bounds of the phantom window.
  void UpdatePhantomWindow(
      const gfx::Point& location,
      const gfx::Rect& bounds,
      int grid_size);

  // Restacks the windows z-order position so that one of the windows is at the
  // top of the z-order, and the rest directly underneath it.
  void RestackWindows();

  // Returns the SnapType for the specified point. SNAP_NONE is used if no
  // snapping should be used.
  SnapType GetSnapType(const gfx::Point& location) const;

  // Returns true if we should allow the mouse pointer to warp.
  bool ShouldAllowMouseWarp() const;

  aura::Window* window() const { return details_.window; }

  const Details details_;

  const std::vector<aura::Window*> attached_windows_;

  // Set to true once Drag() is invoked and the bounds of the window change.
  bool did_move_or_resize_;

  // The initial size of each of the windows in |attached_windows_| along the
  // primary axis.
  std::vector<int> initial_size_;

  // The min size of each of the windows in |attached_windows_| along the
  // primary axis.
  std::vector<int> min_size_;

  // The amount each of the windows in |attached_windows_| can be compressed.
  // This is a fraction of the amount a window can be compressed over the total
  // space the windows can be compressed.
  std::vector<float> compress_fraction_;

  // The amount each of the windows in |attached_windows_| should be expanded
  // by. This is used when the user drags to the left/up. In this case the main
  // window shrinks and the attached windows expand.
  std::vector<float> expand_fraction_;

  // Sum of sizes in |min_size_|.
  int total_min_;

  // Sum of the sizes in |initial_size_|.
  int total_initial_size_;

  // Gives a previews of where the the window will end up. Only used if there
  // is a grid and the caption is being dragged.
  scoped_ptr<PhantomWindowController> phantom_window_controller_;

  // Used to determine the target position of a snap.
  scoped_ptr<SnapSizer> snap_sizer_;

  // Last SnapType.
  SnapType snap_type_;

  // Number of mouse moves since the last bounds change. Only used for phantom
  // placement to track when the mouse is moved while pushed against the edge of
  // the screen.
  int num_mouse_moves_since_bounds_change_;

  DISALLOW_COPY_AND_ASSIGN(WorkspaceWindowResizer);
};

}  // namespace internal
}  // namespace ash

#endif  // ASH_WM_WORKSPACE_WINDOW_RESIZER_H_
