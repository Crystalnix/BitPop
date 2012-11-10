// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_PANELS_PANEL_VIEW_H_
#define CHROME_BROWSER_UI_PANELS_PANEL_VIEW_H_

#include "base/memory/scoped_ptr.h"
#include "chrome/browser/ui/panels/native_panel.h"
#include "ui/base/animation/animation_delegate.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/widget/widget_observer.h"

class Panel;
class PanelBoundsAnimation;
class PanelFrameView;
class TaskbarWindowThumbnailerWin;

namespace views {
class WebView;
}

class PanelView : public NativePanel,
                  public views::WidgetObserver,
                  public views::WidgetDelegateView,
                  public ui::AnimationDelegate {
 public:
  // The size of inside area used for mouse resizing.
  static const int kResizeInsideBoundsSize = 5;

  PanelView(Panel* panel, const gfx::Rect& bounds);
  virtual ~PanelView();

  // Overridden from NativePanel:
  virtual void ShowPanel() OVERRIDE;
  virtual void ShowPanelInactive() OVERRIDE;
  virtual gfx::Rect GetPanelBounds() const OVERRIDE;
  virtual void SetPanelBounds(const gfx::Rect& bounds) OVERRIDE;
  virtual void SetPanelBoundsInstantly(const gfx::Rect& bounds) OVERRIDE;
  virtual void ClosePanel() OVERRIDE;
  virtual void ActivatePanel() OVERRIDE;
  virtual void DeactivatePanel() OVERRIDE;
  virtual bool IsPanelActive() const OVERRIDE;
  virtual void PreventActivationByOS(bool prevent_activation) OVERRIDE;
  virtual gfx::NativeWindow GetNativePanelHandle() OVERRIDE;
  virtual void UpdatePanelTitleBar() OVERRIDE;
  virtual void UpdatePanelLoadingAnimations(bool should_animate) OVERRIDE;
  virtual FindBar* CreatePanelFindBar() OVERRIDE;
  virtual void NotifyPanelOnUserChangedTheme() OVERRIDE;
  virtual void PanelWebContentsFocused(content::WebContents* contents) OVERRIDE;
  virtual void PanelCut() OVERRIDE;
  virtual void PanelCopy() OVERRIDE;
  virtual void PanelPaste() OVERRIDE;
  virtual void DrawAttention(bool draw_attention) OVERRIDE;
  virtual bool IsDrawingAttention() const OVERRIDE;
  virtual bool PreHandlePanelKeyboardEvent(
      const content::NativeWebKeyboardEvent& event,
      bool* is_keyboard_shortcut) OVERRIDE;
  virtual void HandlePanelKeyboardEvent(
      const content::NativeWebKeyboardEvent& event) OVERRIDE;
  virtual void FullScreenModeChanged(bool is_full_screen) OVERRIDE;
  virtual Browser* GetPanelBrowser() const OVERRIDE;
  virtual void EnsurePanelFullyVisible() OVERRIDE;
  virtual void SetPanelAlwaysOnTop(bool on_top) OVERRIDE;
  virtual void EnableResizeByMouse(bool enable) OVERRIDE;
  virtual void UpdatePanelMinimizeRestoreButtonVisibility() OVERRIDE;
  virtual void PanelExpansionStateChanging(
      Panel::ExpansionState old_state,
      Panel::ExpansionState new_state) OVERRIDE;
  virtual void AttachWebContents(content::WebContents* contents) OVERRIDE;
  virtual void DetachWebContents(content::WebContents* contents) OVERRIDE;
  virtual gfx::Size WindowSizeFromContentSize(
      const gfx::Size& content_size) const OVERRIDE;
  virtual gfx::Size ContentSizeFromWindowSize(
      const gfx::Size& window_size) const OVERRIDE;
  virtual int TitleOnlyHeight() const OVERRIDE;
  virtual NativePanelTesting* CreateNativePanelTesting() OVERRIDE;

  // Return true if the mouse event is handled.
  // |mouse_location| is in screen coordinate system.
  bool OnTitlebarMousePressed(const gfx::Point& mouse_location);
  bool OnTitlebarMouseDragged(const gfx::Point& mouse_location);
  bool OnTitlebarMouseReleased(panel::ClickModifier modifier);
  bool OnTitlebarMouseCaptureLost();

  PanelFrameView* GetFrameView() const;
  bool IsAnimatingBounds() const;

  // The panel does not show a resizing border. Instead, the inner content area
  // can be used to trigger the mouse resizing. Return true if |mouse_location|
  // falls within this area.
  // |mouse_location| is in screen coordinate system.
  bool IsWithinResizingArea(const gfx::Point& mouse_location) const;

  Panel* panel() const { return panel_.get(); }
  views::Widget* window() const { return window_; }
  bool force_to_paint_as_inactive() const {
    return force_to_paint_as_inactive_;
  }

 private:
  enum MouseDraggingState {
    NO_DRAGGING,
    DRAGGING_STARTED,
    DRAGGING_ENDED
  };

  // Overridden from views::WidgetDelegate:
  virtual void OnDisplayChanged() OVERRIDE;
  virtual void OnWorkAreaChanged() OVERRIDE;
  virtual bool WillProcessWorkAreaChange() const OVERRIDE;
  virtual views::View* GetContentsView() OVERRIDE;
  virtual views::NonClientFrameView* CreateNonClientFrameView(
      views::Widget* widget) OVERRIDE;
  virtual bool CanResize() const OVERRIDE;
  virtual bool CanMaximize() const OVERRIDE;
  virtual views::Widget* GetWidget() OVERRIDE;
  virtual const views::Widget* GetWidget() const OVERRIDE;
  virtual string16 GetWindowTitle() const OVERRIDE;
  virtual gfx::ImageSkia GetWindowIcon() OVERRIDE;
  virtual void DeleteDelegate() OVERRIDE;
  virtual void OnWindowBeginUserBoundsChange() OVERRIDE;
  virtual void OnWindowEndUserBoundsChange() OVERRIDE;

  // Overridden from views::View:
  virtual void Layout() OVERRIDE;
  virtual gfx::Size GetMinimumSize() OVERRIDE;
  virtual gfx::Size GetMaximumSize() OVERRIDE;
  virtual bool AcceleratorPressed(const ui::Accelerator& accelerator) OVERRIDE;

  // Overridden from views::WidgetObserver:
  virtual void OnWidgetActivationChanged(views::Widget* widget,
                                         bool active) OVERRIDE;

  // Overridden from AnimationDelegate:
  virtual void AnimationEnded(const ui::Animation* animation) OVERRIDE;
  virtual void AnimationProgressed(const ui::Animation* animation) OVERRIDE;

  void UpdateLoadingAnimations(bool should_animate);
  void UpdateWindowTitle();
  void UpdateWindowIcon();
  void SetBoundsInternal(const gfx::Rect& bounds, bool animate);
  bool EndDragging(bool cancelled);
  void OnViewWasResized();

#if defined(OS_WIN) && !defined(USE_ASH) && !defined(USE_AURA)
  // Sets or clears the bitwise |attribute_value| for the attibute denoted by
  // |attribute_index|. This is used to update the style or extended style
  // for the native window.
  void UpdateWindowAttribute(int attribute_index,
                             int attribute_value,
                             bool to_set);
#endif

  scoped_ptr<Panel> panel_;
  gfx::Rect bounds_;

  // The window that holds all panel views.
  views::Widget* window_;

  // The view hosting the web contents.
  views::WebView* web_view_;

  // Is the panel receiving the focus?
  bool focused_;

  // Is the mouse button currently down?
  bool mouse_pressed_;

  // Location the mouse was pressed at or dragged to last time when we process
  // the mouse event. Used in drag-and-drop.
  // This point is represented in the screen coordinate system.
  gfx::Point last_mouse_location_;

  // Is the titlebar currently being dragged?  That is, has the cursor
  // moved more than kDragThreshold away from its starting position?
  MouseDraggingState mouse_dragging_state_;

  // Used to animate the bounds change.
  scoped_ptr<PanelBoundsAnimation> bounds_animator_;
  gfx::Rect animation_start_bounds_;

  // Is the panel in highlighted state to draw people's attention?
  bool is_drawing_attention_;

  // Should we force to paint the panel as inactive? This is needed when we need
  // to capture the screenshot before an active panel goes minimized.
  bool force_to_paint_as_inactive_;

  // The last view that had focus in the panel. This is saved so that focus can
  // be restored properly when a drag ends.
  views::View* old_focused_view_;

#if defined(OS_WIN) && !defined(USE_ASH) && !defined(USE_AURA)
  // Used to provide custom taskbar thumbnail for Windows 7 and later.
  scoped_ptr<TaskbarWindowThumbnailerWin> thumbnailer_;
#endif

  DISALLOW_COPY_AND_ASSIGN(PanelView);
};

#endif  // CHROME_BROWSER_UI_PANELS_PANEL_VIEW_H_
