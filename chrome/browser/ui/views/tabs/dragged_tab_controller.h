// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_TABS_DRAGGED_TAB_CONTROLLER_H_
#define CHROME_BROWSER_UI_VIEWS_TABS_DRAGGED_TAB_CONTROLLER_H_
#pragma once

#include <vector>

#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "base/timer.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "chrome/browser/ui/tabs/dock_info.h"
#include "content/browser/tab_contents/tab_contents_delegate.h"
#include "content/common/notification_observer.h"
#include "content/common/notification_registrar.h"
#include "ui/gfx/rect.h"

namespace views {
class View;
}
class BaseTab;
class BaseTabStrip;
class DraggedTabView;
class TabStripModel;

struct TabRendererData;

///////////////////////////////////////////////////////////////////////////////
//
// DraggedTabController
//
//  An object that handles a drag session for an individual Tab within a
//  TabStrip. This object is created whenever the mouse is pressed down on a
//  Tab and destroyed when the mouse is released or the drag operation is
//  aborted. The Tab that the user dragged (the "source tab") owns this object
//  and must be the only one to destroy it (via |DestroyDragController|).
//
///////////////////////////////////////////////////////////////////////////////
class DraggedTabController : public TabContentsDelegate,
                             public NotificationObserver,
                             public MessageLoopForUI::Observer {
 public:
  DraggedTabController();
  virtual ~DraggedTabController();

  // Initializes DraggedTabController to drag the tabs in |tabs| originating
  // from |source_tabstrip|. |source_tab| is the tab that initiated the drag and
  // is contained in |tabs|.  |mouse_offset| is the distance of the mouse
  // pointer from the origin of the first tab in |tabs| and |source_tab_offset|
  // the offset from |source_tab|. |source_tab_offset| is the horizontal distant
  // for a horizontal tab strip, and the vertical distance for a vertical tab
  // strip.
  void Init(BaseTabStrip* source_tabstrip,
            BaseTab* source_tab,
            const std::vector<BaseTab*>& tabs,
            const gfx::Point& mouse_offset,
            int source_tab_offset);

  // Returns true if there is a drag underway and the drag is attached to
  // |tab_strip|.
  // NOTE: this returns false if the dragged tab controller is in the process
  // of finishing the drag.
  static bool IsAttachedTo(BaseTabStrip* tab_strip);

  // Responds to drag events subsequent to StartDrag. If the mouse moves a
  // sufficient distance before the mouse is released, a drag session is
  // initiated.
  void Drag();

  // Complete the current drag session. If the drag session was canceled
  // because the user pressed Escape or something interrupted it, |canceled|
  // is true so the helper can revert the state to the world before the drag
  // begun.
  void EndDrag(bool canceled);

  // Returns true if a drag started.
  bool started_drag() const { return started_drag_; }

 private:
  class DockDisplayer;
  friend class DockDisplayer;

  typedef std::set<gfx::NativeView> DockWindows;

  // Enumeration of the ways a drag session can end.
  enum EndDragType {
    // Drag session exited normally: the user released the mouse.
    NORMAL,

    // The drag session was canceled (alt-tab during drag, escape ...)
    CANCELED,

    // The tab (NavigationController) was destroyed during the drag.
    TAB_DESTROYED
  };

  // Stores the date associated with a single tab that is being dragged.
  struct TabDragData {
    TabDragData();
    ~TabDragData();

    // The TabContentsWrapper being dragged.
    TabContentsWrapper* contents;

    // The original TabContentsDelegate of |contents|, before it was detached
    // from the browser window. We store this so that we can forward certain
    // delegate notifications back to it if we can't handle them locally.
    TabContentsDelegate* original_delegate;

    // This is the index of the tab in |source_tabstrip_| when the drag
    // began. This is used to restore the previous state if the drag is aborted.
    int source_model_index;

    // If attached this is the tab in |attached_tabstrip_|.
    BaseTab* attached_tab;

    // Is the tab pinned?
    bool pinned;
  };

  typedef std::vector<TabDragData> DragData;

  // Sets |drag_data| from |tab|. This also registers for necessary
  // notifications and resets the delegate of the TabContentsWrapper.
  void InitTabDragData(BaseTab* tab, TabDragData* drag_data);

  // Overridden from TabContentsDelegate:
  virtual void OpenURLFromTab(TabContents* source,
                              const GURL& url,
                              const GURL& referrer,
                              WindowOpenDisposition disposition,
                              PageTransition::Type transition) OVERRIDE;
  virtual void NavigationStateChanged(const TabContents* source,
                                      unsigned changed_flags) OVERRIDE;
  virtual void AddNewContents(TabContents* source,
                              TabContents* new_contents,
                              WindowOpenDisposition disposition,
                              const gfx::Rect& initial_pos,
                              bool user_gesture) OVERRIDE;
  virtual void ActivateContents(TabContents* contents) OVERRIDE;
  virtual void DeactivateContents(TabContents* contents) OVERRIDE;
  virtual void LoadingStateChanged(TabContents* source) OVERRIDE;
  virtual void CloseContents(TabContents* source) OVERRIDE;
  virtual void MoveContents(TabContents* source,
                            const gfx::Rect& pos) OVERRIDE;
  virtual void UpdateTargetURL(TabContents* source, const GURL& url) OVERRIDE;
  virtual bool ShouldSuppressDialogs() OVERRIDE;

  // Overridden from NotificationObserver:
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details) OVERRIDE;

  // Overridden from MessageLoop::Observer:
#if defined(OS_WIN)
  virtual void WillProcessMessage(const MSG& msg) OVERRIDE;
  virtual void DidProcessMessage(const MSG& msg) OVERRIDE;
#else
  virtual void WillProcessEvent(GdkEvent* event) OVERRIDE;
  virtual void DidProcessEvent(GdkEvent* event) OVERRIDE;
#endif

  // Initialize the offset used to calculate the position to create windows
  // in |GetWindowCreatePoint|. This should only be invoked from |Init|.
  void InitWindowCreatePoint();

  // Returns the point where a detached window should be created given the
  // current mouse position.
  gfx::Point GetWindowCreatePoint() const;

  void UpdateDockInfo(const gfx::Point& screen_point);

  // Saves focus in the window that the drag initiated from. Focus will be
  // restored appropriately if the drag ends within this same window.
  void SaveFocus();

  // Restore focus to the View that had focus before the drag was started, if
  // the drag ends within the same Window as it began.
  void RestoreFocus();

  // Tests whether the position of the mouse is past a minimum elasticity
  // threshold required to start a drag.
  bool CanStartDrag() const;

  // Move the DraggedTabView according to the current mouse screen position,
  // potentially updating the source and other TabStrips.
  void ContinueDragging();

  // Handles dragging tabs while the tabs are attached.
  void MoveAttached(const gfx::Point& screen_point);

  // Handles dragging while the tabs are detached.
  void MoveDetached(const gfx::Point& screen_point);

  // Returns the compatible TabStrip that is under the specified point (screen
  // coordinates), or NULL if there is none.
  BaseTabStrip* GetTabStripForPoint(const gfx::Point& screen_point);

  DockInfo GetDockInfoAtPoint(const gfx::Point& screen_point);

  // Returns the specified |tabstrip| if it contains the specified point
  // (screen coordinates), NULL if it does not.
  BaseTabStrip* GetTabStripIfItContains(BaseTabStrip* tabstrip,
                                        const gfx::Point& screen_point) const;

  // Attach the dragged Tab to the specified TabStrip.
  void Attach(BaseTabStrip* attached_tabstrip, const gfx::Point& screen_point);

  // Detach the dragged Tab from the current TabStrip.
  void Detach();

  // Returns the index where the dragged TabContents should be inserted into
  // |attached_tabstrip_| given the DraggedTabView's bounds |dragged_bounds| in
  // coordinates relative to |attached_tabstrip_| and has had the mirroring
  // transformation applied.
  // NOTE: this is invoked from |Attach| before the tabs have been inserted.
  int GetInsertionIndexForDraggedBounds(const gfx::Rect& dragged_bounds) const;

  // Retrieve the bounds of the DraggedTabView relative to the attached
  // TabStrip. |tab_strip_point| is in the attached TabStrip's coordinate
  // system.
  gfx::Rect GetDraggedViewTabStripBounds(const gfx::Point& tab_strip_point);

  // Get the position of the dragged tab view relative to the attached tab
  // strip with the mirroring transform applied.
  gfx::Point GetAttachedDragPoint(const gfx::Point& screen_point);

  // Finds the Tabs within the specified TabStrip that corresponds to the
  // TabContents of the dragged tabs. Returns an empty vector if not attached.
  std::vector<BaseTab*> GetTabsMatchingDraggedContents(BaseTabStrip* tabstrip);

  // Does the work for EndDrag. If we actually started a drag and |how_end| is
  // not TAB_DESTROYED then one of EndDrag or RevertDrag is invoked.
  void EndDragImpl(EndDragType how_end);

  // Reverts a cancelled drag operation.
  void RevertDrag();

  // Reverts the tab at |drag_index| in |drag_data_|.
  void RevertDragAt(size_t drag_index);

  // Selects the dragged tabs in |model|. Does nothing if there are no longer
  // any dragged contents (as happens when a TabContents is deleted out from
  // under us).
  void ResetSelection(TabStripModel* model);

  // Finishes a succesful drag operation.
  void CompleteDrag();

  // Resets the delegates of the TabContents.
  void ResetDelegates();

  // Create the DraggedTabView.
  void CreateDraggedView(const std::vector<TabRendererData>& data,
                         const std::vector<gfx::Rect>& renderer_bounds);

  // Utility for getting the mouse position in screen coordinates.
  gfx::Point GetCursorScreenPoint() const;

  // Returns the bounds (in screen coordinates) of the specified View.
  gfx::Rect GetViewScreenBounds(views::View* tabstrip) const;

  // Hides the frame for the window that contains the TabStrip the current
  // drag session was initiated from.
  void HideFrame();

  // Closes a hidden frame at the end of a drag session.
  void CleanUpHiddenFrame();

  void DockDisplayerDestroyed(DockDisplayer* controller);

  void BringWindowUnderMouseToFront();

  // Convenience for getting the TabDragData corresponding to the tab the user
  // started dragging.
  TabDragData* source_tab_drag_data() {
    return &(drag_data_[source_tab_index_]);
  }

  // Convenience for |source_tab_drag_data()->contents|.
  TabContentsWrapper* source_dragged_contents() {
    return source_tab_drag_data()->contents;
  }

  // Returns true if the tabs were originality one after the other in
  // |source_tabstrip_|.
  bool AreTabsConsecutive();

  // Returns the TabStripModel for the specified tabstrip.
  TabStripModel* GetModel(BaseTabStrip* tabstrip) const;

  // Handles registering for notifications.
  NotificationRegistrar registrar_;

  // The TabStrip the drag originated from.
  BaseTabStrip* source_tabstrip_;

  // The TabStrip the dragged Tab is currently attached to, or NULL if the
  // dragged Tab is detached.
  BaseTabStrip* attached_tabstrip_;

  // The visual representation of the dragged Tab.
  scoped_ptr<DraggedTabView> view_;

  // The position of the mouse (in screen coordinates) at the start of the drag
  // operation. This is used to calculate minimum elasticity before a
  // DraggedTabView is constructed.
  gfx::Point start_screen_point_;

  // This is the offset of the mouse from the top left of the Tab where
  // dragging begun. This is used to ensure that the dragged view is always
  // positioned at the correct location during the drag, and to ensure that the
  // detached window is created at the right location.
  gfx::Point mouse_offset_;

  // Offset of the mouse relative to the source tab.
  int source_tab_offset_;

  // Ratio of the x-coordinate of the |source_tab_offset_| to the width of the
  // tab. Not used for vertical tabs.
  float offset_to_width_ratio_;

  // A hint to use when positioning new windows created by detaching Tabs. This
  // is the distance of the mouse from the top left of the dragged tab as if it
  // were the distance of the mouse from the top left of the first tab in the
  // attached TabStrip from the top left of the window.
  gfx::Point window_create_point_;

  // Location of the first tab in the source tabstrip in screen coordinates.
  // This is used to calculate window_create_point_.
  gfx::Point first_source_tab_point_;

  // The bounds of the browser window before the last Tab was detached. When
  // the last Tab is detached, rather than destroying the frame (which would
  // abort the drag session), the frame is moved off-screen. If the drag is
  // aborted (e.g. by the user pressing Esc, or capture being lost), the Tab is
  // attached to the hidden frame and the frame moved back to these bounds.
  gfx::Rect restore_bounds_;

  // The last view that had focus in the window containing |source_tab_|. This
  // is saved so that focus can be restored properly when a drag begins and
  // ends within this same window.
  views::View* old_focused_view_;

  // The position along the major axis of the mouse cursor in screen coordinates
  // at the time of the last re-order event.
  int last_move_screen_loc_;

  DockInfo dock_info_;

  DockWindows dock_windows_;

  std::vector<DockDisplayer*> dock_controllers_;

  // Timer used to bring the window under the cursor to front. If the user
  // stops moving the mouse for a brief time over a browser window, it is
  // brought to front.
  base::OneShotTimer<DraggedTabController> bring_to_front_timer_;

  // Did the mouse move enough that we started a drag?
  bool started_drag_;

  // Is the drag active?
  bool active_;

  DragData drag_data_;

  // Index of the source tab in drag_data_.
  size_t source_tab_index_;

  // True until |MoveAttached| is invoked once.
  bool initial_move_;

  DISALLOW_COPY_AND_ASSIGN(DraggedTabController);
};

#endif  // CHROME_BROWSER_UI_VIEWS_TABS_DRAGGED_TAB_CONTROLLER_H_
