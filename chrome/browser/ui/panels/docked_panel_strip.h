// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_PANELS_DOCKED_PANEL_STRIP_H_
#define CHROME_BROWSER_UI_PANELS_DOCKED_PANEL_STRIP_H_
#pragma once

#include <set>
#include <vector>
#include "base/basictypes.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/panels/auto_hiding_desktop_bar.h"
#include "chrome/browser/ui/panels/panel.h"
#include "chrome/browser/ui/panels/panel_mouse_watcher_observer.h"
#include "ui/gfx/rect.h"

class Browser;
class PanelManager;

// This class manages a group of panels displayed in a horizontal strip,
// positioning the panels and controlling how they are displayed.
// Panels in the strip appear minimized, showing title-only or expanded.
// All panels in the strip are contained within the bounds of the strip.
class DockedPanelStrip : public PanelMouseWatcherObserver {
 public:
  typedef std::vector<Panel*> Panels;

  explicit DockedPanelStrip(PanelManager* panel_manager);
  virtual ~DockedPanelStrip();

  // Sets the bounds of the panel strip.
  // |area| is in screen coordinates.
  void SetDisplayArea(const gfx::Rect& area);

  // Adds a panel to the strip. The panel may be a newly created panel or one
  // that is transitioning from another grouping of panels.
  void AddPanel(Panel* panel);

  // Returns |false| if the panel is not in the strip.
  bool Remove(Panel* panel);
  void RemoveAll();

  // Drags the given panel.
  void StartDragging(Panel* panel);
  void Drag(int delta_x);
  void EndDragging(bool cancelled);

  // Invoked when a panel's expansion state changes.
  void OnPanelExpansionStateChanged(Panel* panel);

  // Invoked when a panel's attention state changes.
  void OnPanelAttentionStateChanged(Panel* panel);

  // Invoked when the window size of the given panel is changed.
  void OnWindowSizeChanged(
      Panel* panel, const gfx::Size& preferred_window_size);

  // Returns true if we should bring up the titlebars, given the current mouse
  // point.
  bool ShouldBringUpTitlebars(int mouse_x, int mouse_y) const;

  // Brings up or down the titlebars for all minimized panels.
  void BringUpOrDownTitlebars(bool bring_up);

  // Returns the bottom position for the panel per its expansion state. If auto-
  // hide bottom bar is present, we want to move the minimized panel to the
  // bottom of the screen, not the bottom of the work area.
  int GetBottomPositionForExpansionState(
      Panel::ExpansionState expansion_state) const;

  // num_panels() and panels() only includes panels in the panel strip that
  // do NOT have a temporary layout.
  int num_panels() const { return panels_.size(); }
  const Panels& panels() const { return panels_; }

  bool is_dragging_panel() const;
  gfx::Rect display_area() const { return display_area_; }

  int GetMaxPanelWidth() const;
  int GetMaxPanelHeight() const;
  int StartingRightPosition() const;

  void OnAutoHidingDesktopBarVisibilityChanged(
      AutoHidingDesktopBar::Alignment alignment,
      AutoHidingDesktopBar::Visibility visibility);

  void OnFullScreenModeChanged(bool is_full_screen);

#ifdef UNIT_TEST
  int num_temporary_layout_panels() const {
    return panels_in_temporary_layout_.size();
  }
#endif

 private:
  enum TitlebarAction {
    NO_ACTION,
    BRING_UP,
    BRING_DOWN
  };

  // Overridden from PanelMouseWatcherObserver:
  virtual void OnMouseMove(const gfx::Point& mouse_position) OVERRIDE;

  // Keep track of the minimized panels to control mouse watching.
  void IncrementMinimizedPanels();
  void DecrementMinimizedPanels();

  // Handles all the panels that're delayed to be removed.
  void DelayedRemove();

  // Does the actual remove. Caller is responsible for rearranging
  // the panel strip if necessary.
  // Returns |false| if panel is not in the strip.
  bool DoRemove(Panel* panel);

  // Rearranges the positions of the panels in the strip.
  // Handles moving panels to/from overflow area as needed.
  // This is called when the display space has been changed, i.e. working
  // area being changed or a panel being closed.
  void Rearrange();

  // Help functions to drag the given panel.
  void DragLeft();
  void DragRight();

  // Does the real job of bringing up or down the titlebars.
  void DoBringUpOrDownTitlebars(bool bring_up);
  // The callback for a delyed task, checks if it still need to perform
  // the delayed action.
  void DelayedBringUpOrDownTitlebarsCheck();

  int GetRightMostAvailablePosition() const;

  // Called by AddPanel() after a delay to move a newly created panel from
  // the panel strip to overflow because the panel could not fit
  // within the bounds of the panel strip. New panels are first displayed
  // in the panel strip, then moved to overflow so that all created
  // panels are (at least briefly) visible before entering overflow.
  void DelayedMovePanelToOverflow(Panel* panel);

  PanelManager* panel_manager_;  // Weak, owns us.

  // All panels in the panel strip must fit within this area.
  gfx::Rect display_area_;

  Panels panels_;

  // Stores the panels that are pending to remove. We want to delay the removal
  // when we're in the process of the dragging.
  Panels panels_pending_to_remove_;

  // Stores newly created panels that have a temporary layout until they
  // are moved to overflow after a delay.
  std::set<Panel*> panels_in_temporary_layout_;

  int minimized_panel_count_;
  bool are_titlebars_up_;

  // Panel to drag.
  size_t dragging_panel_index_;

  // Original x coordinate of the panel to drag. This is used to get back to
  // the original position when we cancel the dragging.
  int dragging_panel_original_x_;

  // Bounds of the panel to drag. It is first set to the original bounds when
  // the dragging happens. Then it is updated to the position that will be set
  // to when the dragging ends.
  gfx::Rect dragging_panel_bounds_;

  // Delayed transitions support. Sometimes transitions between minimized and
  // title-only states are delayed, for better usability with Taskbars/Docks.
  TitlebarAction delayed_titlebar_action_;

  // Owned by MessageLoop after posting.
  base::WeakPtrFactory<DockedPanelStrip> titlebar_action_factory_;

  static const int kPanelsHorizontalSpacing = 4;

  // Absolute minimum width and height for panels, including non-client area.
  // Should only be big enough to accomodate a close button on the reasonably
  // recognisable titlebar.
  static const int kPanelMinWidth;
  static const int kPanelMinHeight;

  DISALLOW_COPY_AND_ASSIGN(DockedPanelStrip);
};

#endif  // CHROME_BROWSER_UI_PANELS_DOCKED_PANEL_STRIP_H_
