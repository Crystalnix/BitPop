// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_PANELS_PANEL_MANAGER_H_
#define CHROME_BROWSER_UI_PANELS_PANEL_MANAGER_H_
#pragma once

#include <vector>
#include "base/basictypes.h"
#include "base/lazy_instance.h"
#include "base/memory/scoped_ptr.h"
#include "base/timer.h"
#include "chrome/browser/ui/panels/auto_hiding_desktop_bar.h"
#include "chrome/browser/ui/panels/panel.h"
#include "ui/gfx/rect.h"

class Browser;
class PanelMouseWatcher;
class OverflowPanelStrip;
class DockedPanelStrip;

// This class manages a set of panels.
class PanelManager : public AutoHidingDesktopBar::Observer {
 public:
  // Returns a single instance.
  static PanelManager* GetInstance();

  // Returns true if panels should be used for the extension.
  static bool ShouldUsePanels(const std::string& extension_id);

  // Called when the display is changed, i.e. work area is updated.
  void OnDisplayChanged();

  // Creates a panel and returns it. The panel might be queued for display
  // later.
  Panel* CreatePanel(Browser* browser);

  void Remove(Panel* panel);
  void RemoveAll();

  // Asynchronous confirmation of panel having been removed.
  void OnPanelRemoved(Panel* panel);

  // Drags the given panel.
  void StartDragging(Panel* panel);
  void Drag(int delta_x);
  void EndDragging(bool cancelled);

  // Invoked when a panel's expansion state changes.
  void OnPanelExpansionStateChanged(Panel* panel);

  // Invoked when a panel is starting/stopping drawing an attention.
  void OnPanelAttentionStateChanged(Panel* panel);

  // Invoked when the preferred window size of the given panel might need to
  // get changed.
  void OnPreferredWindowSizeChanged(
      Panel* panel, const gfx::Size& preferred_window_size);

  // Resizes the panel. Explicitly setting the panel size is not allowed
  // for panels that are auto-sized.
  void ResizePanel(Panel* panel, const gfx::Size& new_size);

  // Returns true if we should bring up the titlebars, given the current mouse
  // point.
  bool ShouldBringUpTitlebars(int mouse_x, int mouse_y) const;

  // Brings up or down the titlebars for all minimized panels.
  void BringUpOrDownTitlebars(bool bring_up);

  // Returns the next browser window which could be either panel window or
  // tabbed window, to switch to if the given panel is going to be deactivated.
  // Returns NULL if such window cannot be found.
  BrowserWindow* GetNextBrowserWindowToActivate(Panel* panel) const;

  int num_panels() const;
  int StartingRightPosition() const;
  std::vector<Panel*> panels() const;

  AutoHidingDesktopBar* auto_hiding_desktop_bar() const {
    return auto_hiding_desktop_bar_;
  }

  PanelMouseWatcher* mouse_watcher() const {
    return panel_mouse_watcher_.get();
  }

  DockedPanelStrip* docked_strip() const {
    return docked_strip_.get();
  }

  bool is_full_screen() const { return is_full_screen_; }
  OverflowPanelStrip* overflow_strip() const {
    return overflow_strip_.get();
  }

  // Reduces time interval in tests to shorten test run time.
  // Wrapper should be used around all time intervals in panels code.
  static inline double AdjustTimeInterval(double interval) {
    if (shorten_time_intervals_)
      return interval / 100.0;
    else
      return interval;
  }

#ifdef UNIT_TEST
  static void shorten_time_intervals_for_testing() {
    shorten_time_intervals_ = true;
  }

  void set_auto_hiding_desktop_bar(
      AutoHidingDesktopBar* auto_hiding_desktop_bar) {
    auto_hiding_desktop_bar_ = auto_hiding_desktop_bar;
  }

  void enable_auto_sizing(bool enabled) {
    auto_sizing_enabled_ = enabled;
  }

  const gfx::Rect& work_area() const {
    return work_area_;
  }

  void SetWorkAreaForTesting(const gfx::Rect& work_area) {
    SetWorkArea(work_area);
  }

  void SetMouseWatcherForTesting(PanelMouseWatcher* watcher) {
    SetMouseWatcher(watcher);
  }
#endif

 private:
  friend struct base::DefaultLazyInstanceTraits<PanelManager>;

  PanelManager();
  virtual ~PanelManager();

  // Overridden from AutoHidingDesktopBar::Observer:
  virtual void OnAutoHidingDesktopBarThicknessChanged() OVERRIDE;
  virtual void OnAutoHidingDesktopBarVisibilityChanged(
      AutoHidingDesktopBar::Alignment alignment,
      AutoHidingDesktopBar::Visibility visibility) OVERRIDE;

  // Applies the new work area. This is called by OnDisplayChanged and the test
  // code.
  void SetWorkArea(const gfx::Rect& work_area);

  // Adjusts the work area to exclude the influence of auto-hiding desktop bars.
  void AdjustWorkAreaForAutoHidingDesktopBars();

  // Positions the various groupings of panels.
  void Layout();

  // Tests if the current active app is in full screen mode.
  void CheckFullScreenMode();

  // Tests may want to use a mock panel mouse watcher.
  void SetMouseWatcher(PanelMouseWatcher* watcher);

  // Tests may want to shorten time intervals to reduce running time.
  static bool shorten_time_intervals_;

  scoped_ptr<DockedPanelStrip> docked_strip_;
  scoped_ptr<OverflowPanelStrip> overflow_strip_;

  // Use a mouse watcher to know when to bring up titlebars to "peek" at
  // minimized panels. Mouse movement is only tracked when there is a minimized
  // panel.
  scoped_ptr<PanelMouseWatcher> panel_mouse_watcher_;

  // The maximum work area avaialble. This area does not include the area taken
  // by the always-visible (non-auto-hiding) desktop bars.
  gfx::Rect work_area_;

  // The useable work area for computing the panel bounds. This area excludes
  // the potential area that could be taken by the auto-hiding desktop
  // bars (we only consider those bars that are aligned to bottom, left, and
  // right of the screen edges) when they become fully visible.
  gfx::Rect adjusted_work_area_;

  scoped_refptr<AutoHidingDesktopBar> auto_hiding_desktop_bar_;

  // Whether or not bounds will be updated when the preferred content size is
  // changed. The testing code could set this flag to false so that other tests
  // will not be affected.
  bool auto_sizing_enabled_;

  // Timer used to track if the current active app is in full screen mode.
  base::RepeatingTimer<PanelManager> full_screen_mode_timer_;

  // True if current active app is in full screen mode.
  bool is_full_screen_;

  DISALLOW_COPY_AND_ASSIGN(PanelManager);
};

#endif  // CHROME_BROWSER_UI_PANELS_PANEL_MANAGER_H_
