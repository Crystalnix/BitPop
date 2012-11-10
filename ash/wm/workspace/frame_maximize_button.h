// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_WM_WORKSPACE_FRAME_MAXIMIZE_BUTTON_H_
#define ASH_WM_WORKSPACE_FRAME_MAXIMIZE_BUTTON_H_

#include "ash/ash_export.h"
#include "ash/wm/workspace/snap_types.h"
#include "base/memory/scoped_ptr.h"
#include "base/timer.h"
#include "ui/aura/window_observer.h"
#include "ui/views/controls/button/image_button.h"

namespace views {
class NonClientFrameView;
}

namespace ash {

namespace internal {
class PhantomWindowController;
class SnapSizer;
}

class MaximizeBubbleController;

// Button used for the maximize control on the frame. Handles snapping logic.
class ASH_EXPORT FrameMaximizeButton : public views::ImageButton,
                                       public aura::WindowObserver {
 public:
  FrameMaximizeButton(views::ButtonListener* listener,
                      views::NonClientFrameView* frame);
  virtual ~FrameMaximizeButton();

  // Updates |snap_type_| based on a a given snap type. This is used by
  // external hover events from the button menu.
  void SnapButtonHovered(SnapType type);

  // The user clicked the |type| button and the action needs to be performed,
  // which will at the same time close the window.
  void ExecuteSnapAndCloseMenu(SnapType type);

  // Remove the maximize menu from the screen (and destroy it).
  void DestroyMaximizeMenu();

  // Returns true when the user clicks and drags the button.
  bool is_snap_enabled() const { return is_snap_enabled_; }

  // WindowObserver overrides:
  virtual void OnWindowBoundsChanged(aura::Window* window,
                                     const gfx::Rect& old_bounds,
                                     const gfx::Rect& new_bounds) OVERRIDE;
  virtual void OnWindowDestroying(aura::Window* window) OVERRIDE;

  // ImageButton overrides:
  virtual bool OnMousePressed(const views::MouseEvent& event) OVERRIDE;
  virtual void OnMouseEntered(const views::MouseEvent& event) OVERRIDE;
  virtual void OnMouseExited(const views::MouseEvent& event) OVERRIDE;
  virtual bool OnMouseDragged(const views::MouseEvent& event) OVERRIDE;
  virtual void OnMouseReleased(const views::MouseEvent& event) OVERRIDE;
  virtual void OnMouseCaptureLost() OVERRIDE;
  virtual ui::GestureStatus OnGestureEvent(
      const views::GestureEvent& event) OVERRIDE;

 private:
  class EscapeEventFilter;

  // Initializes the snap-gesture based on the event. This should only be called
  // when the event is confirmed to have started a snap gesture.
  void ProcessStartEvent(const views::LocatedEvent& event);

  // Updates the snap-state based on the current event. This should only be
  // called after the snap gesture has already started.
  void ProcessUpdateEvent(const views::LocatedEvent& event);

  // Returns true if the window was snapped. Returns false otherwise.
  bool ProcessEndEvent(const views::LocatedEvent& event);

  // Cancels snap behavior. If |keep_menu_open| is set, a possibly opened
  // bubble help will remain open.
  void Cancel(bool keep_menu_open);

  // Installs/uninstalls an EventFilter to track when escape is pressed.
  void InstallEventFilter();
  void UninstallEventFilter();

  // Updates the snap position from the event location. This is invoked by
  // |update_timer_|.
  void UpdateSnapFromEventLocation();

  // Updates |snap_type_| based on a mouse drag.
  void UpdateSnap(const gfx::Point& location);

  // Returns the type of snap based on the specified location.
  SnapType SnapTypeForLocation(const gfx::Point& location) const;

  // Returns the bounds of the resulting window for the specified type.
  gfx::Rect ScreenBoundsForType(SnapType type,
                                const internal::SnapSizer& snap_sizer) const;

  // Converts location to screen coordinates and returns it. These are the
  // coordinates used by the SnapSizer.
  gfx::Point LocationForSnapSizer(const gfx::Point& location) const;

  // Snaps the window to the current snap position.
  void Snap(const internal::SnapSizer& snap_sizer);

  // Frame that the maximize button acts on.
  views::NonClientFrameView* frame_;

  // Renders the snap position.
  scoped_ptr<internal::PhantomWindowController> phantom_window_;

  // Is snapping enabled? Set on press so that in drag we know whether we
  // should show the snap locations.
  bool is_snap_enabled_;

  // Did the user drag far enough to trigger snapping?
  bool exceeded_drag_threshold_;

  // This is the Window we are contained in.
  aura::Window* window_;

  // Location of the press.
  gfx::Point press_location_;

  // Current snap type.
  SnapType snap_type_;

  scoped_ptr<internal::SnapSizer> snap_sizer_;

  scoped_ptr<EscapeEventFilter> escape_event_filter_;

  base::OneShotTimer<FrameMaximizeButton> update_timer_;

  scoped_ptr<MaximizeBubbleController> maximizer_;

  DISALLOW_COPY_AND_ASSIGN(FrameMaximizeButton);
};

}  // namespace ash

#endif  // ASH_WM_WORKSPACE_FRAME_MAXIMIZE_BUTTON_H_
