// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/panels/panel_drag_controller.h"

#include "base/logging.h"
#include "chrome/browser/ui/panels/detached_panel_collection.h"
#include "chrome/browser/ui/panels/docked_panel_collection.h"
#include "chrome/browser/ui/panels/panel.h"
#include "chrome/browser/ui/panels/panel_collection.h"
#include "chrome/browser/ui/panels/panel_manager.h"
#include "ui/gfx/rect.h"

// static
const int PanelDragController::kDetachDockedPanelThreshold = 100;
const int PanelDragController::kDockDetachedPanelThreshold = 30;

PanelDragController::PanelDragController(PanelManager* panel_manager)
    : panel_manager_(panel_manager),
      dragging_panel_(NULL),
      dragging_panel_original_collection_(NULL) {
}

PanelDragController::~PanelDragController() {
}

gfx::Point PanelDragController::GetPanelPositionForMouseLocation(
    const gfx::Point& mouse_location) const {
  return mouse_location - offset_from_mouse_location_on_drag_start_;
}

void PanelDragController::StartDragging(Panel* panel,
                                        const gfx::Point& mouse_location) {
  DCHECK(!dragging_panel_);

  last_mouse_location_ = mouse_location;
  offset_from_mouse_location_on_drag_start_ =
      mouse_location - panel->GetBounds().origin();

  dragging_panel_ = panel;
  dragging_panel_->SetPreviewMode(true);

  // Keep track of original collection and placement for the case that the drag
  // is cancelled.
  dragging_panel_original_collection_ = dragging_panel_->collection();
  dragging_panel_original_collection_->SavePanelPlacement(dragging_panel_);

  dragging_panel_original_collection_->StartDraggingPanelWithinCollection(
      dragging_panel_);
}

void PanelDragController::Drag(const gfx::Point& mouse_location) {
  DCHECK(dragging_panel_);

  PanelCollection* current_collection = dragging_panel_->collection();

  gfx::Rect target_panel_bounds;
  PanelCollection* target_collection = ComputeDragTargetCollection(
      mouse_location, &target_panel_bounds);
  if (target_collection != current_collection) {
    // End the dragging in old collection.
    current_collection->EndDraggingPanelWithinCollection(dragging_panel_,
        true /*aborted*/);

    // Apply new panel position.
    dragging_panel_->SetPanelBounds(target_panel_bounds);

    // Move the panel to new collection.
    panel_manager_->MovePanelToCollection(dragging_panel_,
                                          target_collection->type(),
                                          PanelCollection::KNOWN_POSITION);

    // Start the dragging in new collection.
    target_collection->StartDraggingPanelWithinCollection(dragging_panel_);
  } else {
    gfx::Point target_panel_position =
        GetPanelPositionForMouseLocation(mouse_location);

    // If the mouse is within the main screen area, make sure that the top
    // border of panel cannot go outside the work area. This is to prevent
    // panel's titlebar from being moved under the taskbar or OSX menu bar
    // that is aligned to top screen edge.
    int display_area_top_position = panel_manager_->display_area().y();
    if (panel_manager_->display_settings_provider()->
            GetPrimaryScreenArea().Contains(mouse_location) &&
        target_panel_position.y() < display_area_top_position) {
      target_panel_position.set_y(display_area_top_position);
    }

    current_collection->DragPanelWithinCollection(dragging_panel_,
                                                  target_panel_position);
  }

  last_mouse_location_ = mouse_location;
}

void PanelDragController::EndDragging(bool cancelled) {
  DCHECK(dragging_panel_);

  PanelCollection* current_collection = dragging_panel_->collection();
  if (cancelled) {
    // Abort the drag in current collection.
    current_collection->EndDraggingPanelWithinCollection(dragging_panel_,
        true /*aborted*/);

    // Restore the dragging panel to its original collection if needed.
    // Note that the bounds of dragging panel is updated later by calling
    // RestorePanelToSavedPlacement.
    if (current_collection != dragging_panel_original_collection_) {
      PanelCollection::PositioningMask positioning_mask =
          static_cast<PanelCollection::PositioningMask>(
              PanelCollection::DEFAULT_POSITION |
              PanelCollection::DO_NOT_UPDATE_BOUNDS);
      panel_manager_->MovePanelToCollection(
          dragging_panel_,
          dragging_panel_original_collection_->type(),
          positioning_mask);
    }

    // End the preview mode.
    dragging_panel_->SetPreviewMode(false);

    // Restore the dragging panel to its original placement.
    dragging_panel_original_collection_->RestorePanelToSavedPlacement();
  } else {
    // The saved placement is no longer needed.
    dragging_panel_original_collection_->DiscardSavedPanelPlacement();

    // End the preview mode.
    dragging_panel_->SetPreviewMode(false);

    // End the drag. This will cause the panel to be moved to its finalized
    // position.
    current_collection->EndDraggingPanelWithinCollection(dragging_panel_,
        false /*not aborted*/);
  }

  dragging_panel_ = NULL;
}

PanelCollection* PanelDragController::ComputeDragTargetCollection(
    const gfx::Point& mouse_location, gfx::Rect* new_panel_bounds) const {
  if (CanDragToDockedCollection(mouse_location, new_panel_bounds))
    return panel_manager_->docked_collection();
  else if (CanDragToDetachedCollection(mouse_location, new_panel_bounds))
    return panel_manager_->detached_collection();
  else
    return dragging_panel_->collection();
}

bool PanelDragController::CanDragToDockedCollection(
    const gfx::Point& mouse_location,
    gfx::Rect* new_panel_bounds) const {
  // It has to come from the detached collection.
  if (dragging_panel_->collection()->type() != PanelCollection::DETACHED)
    return false;

  // Compute target panel bounds. Only the origin is computed based on the mouse
  // location. The size remains unchanged.
  gfx::Rect target_panel_bounds = dragging_panel_->GetBounds();
  target_panel_bounds.set_origin(
      GetPanelPositionForMouseLocation(mouse_location));

  // If the target panel bounds is outside the main display area where the
  // docked collection resides, as in the multi-monitor scenario, we want it to
  // be still free-floating.
  gfx::Rect display_area = panel_manager_->display_settings_provider()->
      GetDisplayArea();
  if (!display_area.Intersects(target_panel_bounds))
    return false;

  // The bottom of the panel should come very close to or fall below the bottom
  // of the docked area.
  if (panel_manager_->docked_collection()->display_area().bottom() -
          target_panel_bounds.bottom() >
      kDockDetachedPanelThreshold)
    return false;

  *new_panel_bounds = target_panel_bounds;
  return true;
}

bool PanelDragController::CanDragToDetachedCollection(
    const gfx::Point& mouse_location,
    gfx::Rect* new_panel_bounds) const {
  // It has to come from the docked collection.
  if (dragging_panel_->collection()->type() != PanelCollection::DOCKED)
    return false;

  // The minimized docked panel is not allowed to detach.
  if (dragging_panel_->IsMinimized())
    return false;

  // Compute target panel bounds. The origin is computed based on the fact that
  // the panel should follow the mouse movement. The size remains unchanged.
  gfx::Rect target_panel_bounds = dragging_panel_->GetBounds();
  target_panel_bounds.set_origin(
      mouse_location - offset_from_mouse_location_on_drag_start_);

  // Panels in the detached collection are always at their full size.
  target_panel_bounds.set_size(dragging_panel()->full_size());

  // The panel should be dragged up high enough to pass certain threshold.
  if (panel_manager_->docked_collection()->display_area().bottom() -
          target_panel_bounds.bottom() <
      kDetachDockedPanelThreshold)
    return false;

  *new_panel_bounds = target_panel_bounds;
  return true;
}

void PanelDragController::OnPanelClosed(Panel* panel) {
  if (!dragging_panel_)
    return;

  // If the dragging panel is closed, abort the drag.
  if (dragging_panel_ == panel) {
    // The saved placement is no longer needed.
    dragging_panel_original_collection_->DiscardSavedPanelPlacement();

    // Clear the dragging state.
    dragging_panel_->collection()->ClearDraggingStateWhenPanelClosed();

    dragging_panel_ = NULL;
  }
}
