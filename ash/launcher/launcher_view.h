// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_LAUNCHER_LAUNCHER_VIEW_H_
#define ASH_LAUNCHER_LAUNCHER_VIEW_H_

#include <utility>
#include <vector>

#include "ash/launcher/launcher_button_host.h"
#include "ash/launcher/launcher_model_observer.h"
#include "ash/wm/shelf_types.h"
#include "base/observer_list.h"
#include "ui/views/animation/bounds_animator_observer.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"

namespace views {
class BoundsAnimator;
class MenuRunner;
class ViewModel;
}

namespace ash {

namespace test {
class LauncherViewTestAPI;
}

class LauncherDelegate;
struct LauncherItem;
class LauncherIconObserver;
class LauncherModel;

namespace internal {

class LauncherButton;
class LauncherTooltipManager;
class ShelfLayoutManager;
class OverflowBubble;
class OverflowButton;

class ASH_EXPORT LauncherView : public views::View,
                                public LauncherModelObserver,
                                public views::ButtonListener,
                                public LauncherButtonHost,
                                public views::ContextMenuController,
                                public views::FocusTraversable,
                                public views::BoundsAnimatorObserver {
 public:
  LauncherView(LauncherModel* model,
               LauncherDelegate* delegate,
               ShelfLayoutManager* shelf_layout_manager);
  virtual ~LauncherView();

  LauncherTooltipManager* tooltip_manager() { return tooltip_.get(); }

  void Init();

  void SetAlignment(ShelfAlignment alignment);

  // Returns the ideal bounds of the specified item, or an empty rect if id
  // isn't know.
  gfx::Rect GetIdealBoundsOfItemIcon(LauncherID id);

  void AddIconObserver(LauncherIconObserver* observer);
  void RemoveIconObserver(LauncherIconObserver* observer);

  // Returns true if we're showing a menu.
  bool IsShowingMenu() const;

  // Returns true if overflow bubble is shown.
  bool IsShowingOverflowBubble() const;

  views::View* GetAppListButtonView() const;

  // Returns true if the mouse cursor exits the area for launcher tooltip.
  // There are thin gaps between launcher buttons but the tooltip shouldn't hide
  // in the gaps, but the tooltip should hide if the mouse moved totally outside
  // of the buttons area.
  bool ShouldHideTooltip(const gfx::Point& cursor_location);

  void set_first_visible_index(int first_visible_index) {
    first_visible_index_ = first_visible_index;
  }

  int leading_inset() const { return leading_inset_; }
  void set_leading_inset(int leading_inset) { leading_inset_ = leading_inset; }

  // Overridden from FocusTraversable:
  virtual views::FocusSearch* GetFocusSearch() OVERRIDE;
  virtual FocusTraversable* GetFocusTraversableParent() OVERRIDE;
  virtual View* GetFocusTraversableParentView() OVERRIDE;

 private:
  friend class ash::test::LauncherViewTestAPI;

  class FadeOutAnimationDelegate;
  class StartFadeAnimationDelegate;

  struct IdealBounds {
    gfx::Rect overflow_bounds;
  };

  // Used in calculating ideal bounds.
  int primary_axis_coordinate(int x, int y) const {
    return is_horizontal_alignment() ? x : y;
  }

  bool is_horizontal_alignment() const {
    return alignment_ == SHELF_ALIGNMENT_BOTTOM;
  }

  bool is_overflow_mode() const {
    return first_visible_index_ > 0;
  }

  bool dragging() const {
    return drag_pointer_ != NONE;
  }

  // Sets the bounds of each view to its ideal bounds.
  void LayoutToIdealBounds();

  // Calculates the ideal bounds. The bounds of each button corresponding to an
  // item in the model is set in |view_model_|.
  void CalculateIdealBounds(IdealBounds* bounds);

  // Returns the index of the last view whose max primary axis coordinate is
  // less than |max_value|. Returns -1 if nothing fits, or there are no views.
  int DetermineLastVisibleIndex(int max_value);

  // Animates the bounds of each view to its ideal bounds.
  void AnimateToIdealBounds();

  // Creates the view used to represent |item|.
  views::View* CreateViewForItem(const LauncherItem& item);

  // Fades |view| from an opacity of 0 to 1. This is when adding a new item.
  void FadeIn(views::View* view);

  // Invoked when the pointer has moved enough to trigger a drag. Sets
  // internal state in preparation for the drag.
  void PrepareForDrag(Pointer pointer, const views::LocatedEvent& event);

  // Invoked when the mouse is dragged. Updates the models as appropriate.
  void ContinueDrag(const views::LocatedEvent& event);

  // Returns true if |typea| and |typeb| should be in the same drag range.
  bool SameDragType(LauncherItemType typea, LauncherItemType typeb) const;

  // Returns the range (in the model) the item at the specified index can be
  // dragged to.
  std::pair<int, int> GetDragRange(int index);

  // If there is a drag operation in progress it's canceled. If |modified_index|
  // is valid, the new position of the corresponding item is returned.
  int CancelDrag(int modified_index);

  // Common setup done for all children.
  void ConfigureChildView(views::View* view);

  // Returns the items whose icons are not shown because they don't fit.
  void GetOverflowItems(std::vector<LauncherItem>* items);

  // Shows the overflow menu.
  void ShowOverflowBubble();

  // Update first launcher button's padding. This method adds padding to the
  // first button to include the leading inset. It needs to be called once on
  // button creation and every time when shelf alignment is changed.
  void UpdateFirstButtonPadding();

  // Overridden from views::View:
  virtual gfx::Size GetPreferredSize() OVERRIDE;
  virtual void OnBoundsChanged(const gfx::Rect& previous_bounds) OVERRIDE;
  virtual FocusTraversable* GetPaneFocusTraversable() OVERRIDE;

  // Overridden from LauncherModelObserver:
  virtual void LauncherItemAdded(int model_index) OVERRIDE;
  virtual void LauncherItemRemoved(int model_index, LauncherID id) OVERRIDE;
  virtual void LauncherItemChanged(int model_index,
                                   const ash::LauncherItem& old_item) OVERRIDE;
  virtual void LauncherItemMoved(int start_index, int target_index) OVERRIDE;

  // Overridden from LauncherButtonHost:
  virtual void PointerPressedOnButton(
      views::View* view,
      Pointer pointer,
      const views::LocatedEvent& event) OVERRIDE;
  virtual void PointerDraggedOnButton(
      views::View* view,
      Pointer pointer,
      const views::LocatedEvent& event) OVERRIDE;
  virtual void PointerReleasedOnButton(views::View* view,
                                       Pointer pointer,
                                       bool canceled) OVERRIDE;
  virtual void MouseMovedOverButton(views::View* view) OVERRIDE;
  virtual void MouseEnteredButton(views::View* view) OVERRIDE;
  virtual void MouseExitedButton(views::View* view) OVERRIDE;
  virtual ShelfAlignment GetShelfAlignment() const OVERRIDE;
  virtual string16 GetAccessibleName(const views::View* view) OVERRIDE;

  // Overridden from views::ButtonListener:
  virtual void ButtonPressed(views::Button* sender,
                             const views::Event& event) OVERRIDE;

  // Overridden from views::ContextMenuController:
  virtual void ShowContextMenuForView(views::View* source,
                                      const gfx::Point& point) OVERRIDE;

  // Overridden from views::BoundsAnimatorObserver:
  virtual void OnBoundsAnimatorProgressed(
      views::BoundsAnimator* animator) OVERRIDE;
  virtual void OnBoundsAnimatorDone(views::BoundsAnimator* animator) OVERRIDE;

  // The model; owned by Launcher.
  LauncherModel* model_;

  // Delegate; owned by Launcher.
  LauncherDelegate* delegate_;

  // Used to manage the set of active launcher buttons. There is a view per
  // item in |model_|.
  scoped_ptr<views::ViewModel> view_model_;

  // Index of first visible launcher item. When it it greater than 0,
  // LauncherView is hosted in an overflow bubble. In this mode, it does not
  // show browser, app list and overflow button.
  int first_visible_index_;

  // Last index of a launcher button that is visible
  // (does not go into overflow).
  int last_visible_index_;

  scoped_ptr<views::BoundsAnimator> bounds_animator_;

  OverflowButton* overflow_button_;

  scoped_ptr<OverflowBubble> overflow_bubble_;

  scoped_ptr<LauncherTooltipManager> tooltip_;

  // Pointer device that initiated the current drag operation. If there is no
  // current dragging operation, this is NONE.
  Pointer drag_pointer_;

  // The view being dragged. This is set immediately when the mouse is pressed.
  // |dragging_| is set only if the mouse is dragged far enough.
  views::View* drag_view_;

  // X coordinate of the mouse down event in |drag_view_|s coordinates.
  int drag_offset_;

  // Index |drag_view_| was initially at.
  int start_drag_index_;

  // Used for the context menu of a particular item.
  LauncherID context_menu_id_;

  scoped_ptr<views::FocusSearch> focus_search_;

#if !defined(OS_MACOSX)
  scoped_ptr<views::MenuRunner> launcher_menu_runner_;
#endif

  ObserverList<LauncherIconObserver> observers_;

  ShelfAlignment alignment_;

  // Amount content is inset on the left edge (or top edge for vertical
  // alignment).
  int leading_inset_;

  DISALLOW_COPY_AND_ASSIGN(LauncherView);
};

}  // namespace internal
}  // namespace ash

#endif  // ASH_LAUNCHER_LAUNCHER_VIEW_H_
