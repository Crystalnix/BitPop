// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SHELL_H_
#define ASH_SHELL_H_
#pragma once

#include <utility>
#include <vector>

#include "ash/ash_export.h"
#include "base/basictypes.h"
#include "base/gtest_prod_util.h"
#include "base/memory/scoped_ptr.h"
#include "base/compiler_specific.h"
#include "base/memory/weak_ptr.h"

class CommandLine;

namespace aura {
class EventFilter;
class RootWindow;
class Window;
}
namespace gfx {
class Point;
class Rect;
class Size;
}
namespace views {
class NonClientFrameView;
class Widget;
}

namespace ash {

class AcceleratorController;
class Launcher;
class PowerButtonController;
class ShellDelegate;
class VideoDetector;
class WindowCycleController;

namespace internal {
class ActivationController;
class AcceleratorFilter;
class AppList;
class DragDropController;
class InputMethodEventFilter;
class RootWindowLayoutManager;
class ShadowController;
class StackingController;
class TooltipController;
class VisibilityController;
class WindowModalityController;
class WorkspaceController;
}

// Shell is a singleton object that presents the Shell API and implements the
// RootWindow's delegate interface.
//
// Upon creation, the Shell sets itself as the RootWindow's delegate, which
// takes ownership of the Shell.
class ASH_EXPORT Shell {
 public:
  // In compact window mode we fill the screen with a single maximized window,
  // similar to ChromeOS R17 and earlier.  In normal mode we have draggable
  // windows.
  enum WindowMode {
    NORMAL_MODE,
    COMPACT_MODE
  };

  // A shell must be explicitly created so that it can call |Init()| with the
  // delegate set. |delegate| can be NULL (if not required for initialization).
  static Shell* CreateInstance(ShellDelegate* delegate);

  // Should never be called before |CreateInstance()|.
  static Shell* GetInstance();

  static void DeleteInstance();

  aura::Window* GetContainer(int container_id);
  const aura::Window* GetContainer(int container_id) const;

  // Adds or removes |filter| from the RootWindowEventFilter.
  void AddRootWindowEventFilter(aura::EventFilter* filter);
  void RemoveRootWindowEventFilter(aura::EventFilter* filter);
  size_t GetRootWindowEventFilterCount() const;

  // Shows the background menu over |widget|.
  void ShowBackgroundMenu(views::Widget* widget, const gfx::Point& location);

  // Toggles app list.
  void ToggleAppList();

  // Changes the current window mode, which will cause all the open windows
  // to be laid out in the new mode and layout managers and event filters to be
  // installed or removed.
  void ChangeWindowMode(WindowMode mode);

  // Returns true if the screen is locked.
  bool IsScreenLocked() const;

  // Returns true if a modal dialog window is currently open.
  bool IsModalWindowOpen() const;

  // See enum WindowMode for details.
  bool IsWindowModeCompact() const { return window_mode_ == COMPACT_MODE; }

  // Creates a default views::NonClientFrameView for use by windows in the
  // Ash environment.
  views::NonClientFrameView* CreateDefaultNonClientFrameView(
      views::Widget* widget);

  AcceleratorController* accelerator_controller() {
    return accelerator_controller_.get();
  }
  internal::TooltipController* tooltip_controller() {
    return tooltip_controller_.get();
  }
  PowerButtonController* power_button_controller() {
    return power_button_controller_.get();
  }
  VideoDetector* video_detector() {
    return video_detector_.get();
  }
  WindowCycleController* window_cycle_controller() {
    return window_cycle_controller_.get();
  }

  ShellDelegate* delegate() { return delegate_.get(); }

  Launcher* launcher() { return launcher_.get(); }

  // Made available for tests.
  internal::ShadowController* shadow_controller() {
    return shadow_controller_.get();
  }

 private:
  FRIEND_TEST_ALL_PREFIXES(ShellTest, ComputeWindowMode);
  FRIEND_TEST_ALL_PREFIXES(ShellTest, ChangeWindowMode);

  typedef std::pair<aura::Window*, gfx::Rect> WindowAndBoundsPair;

  explicit Shell(ShellDelegate* delegate);
  virtual ~Shell();

  void Init();

  // Returns the appropriate window mode to use based on the primary monitor's
  // |monitor_size| and the user's |command_line|.
  WindowMode ComputeWindowMode(const gfx::Size& monitor_size,
                               CommandLine* command_line) const;

  // Initializes or re-initializes the layout managers and event filters needed
  // to support a given window mode and cleans up the unneeded ones.
  void SetupCompactWindowMode();
  void SetupNormalWindowMode();

  // Sets the LayoutManager of the container with the specified id to NULL. This
  // has the effect of deleting the current LayoutManager.
  void ResetLayoutManager(int container_id);

  static Shell* instance_;

  std::vector<WindowAndBoundsPair> to_restore_;

  base::WeakPtrFactory<Shell> method_factory_;

  scoped_ptr<AcceleratorController> accelerator_controller_;

  scoped_ptr<ShellDelegate> delegate_;

  scoped_ptr<Launcher> launcher_;

  scoped_ptr<internal::AppList> app_list_;

  scoped_ptr<internal::StackingController> stacking_controller_;
  scoped_ptr<internal::ActivationController> activation_controller_;
  scoped_ptr<internal::WindowModalityController> window_modality_controller_;
  scoped_ptr<internal::DragDropController> drag_drop_controller_;
  scoped_ptr<internal::WorkspaceController> workspace_controller_;
  scoped_ptr<internal::ShadowController> shadow_controller_;
  scoped_ptr<internal::TooltipController> tooltip_controller_;
  scoped_ptr<internal::VisibilityController> visibility_controller_;
  scoped_ptr<PowerButtonController> power_button_controller_;
  scoped_ptr<VideoDetector> video_detector_;
  scoped_ptr<WindowCycleController> window_cycle_controller_;

  // An event filter that pre-handles all key events to send them to an IME.
  scoped_ptr<internal::InputMethodEventFilter> input_method_filter_;
  // An event filter that pre-handles global accelerators.
  scoped_ptr<internal::AcceleratorFilter> accelerator_filter_;

  // Can change at runtime.
  WindowMode window_mode_;

  // Owned by aura::RootWindow, cached here for type safety.
  internal::RootWindowLayoutManager* root_window_layout_;

  // Status area with clock, Wi-Fi signal, etc.
  views::Widget* status_widget_;

  DISALLOW_COPY_AND_ASSIGN(Shell);
};

}  // namespace ash

#endif  // ASH_SHELL_H_
