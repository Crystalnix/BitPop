// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/shelf_layout_manager.h"

#include "ash/focus_cycler.h"
#include "ash/launcher/launcher.h"
#include "ash/screen_ash.h"
#include "ash/shell.h"
#include "ash/shell_delegate.h"
#include "ash/shell_window_ids.h"
#include "ash/system/tray/system_tray.h"
#include "ash/test/ash_test_base.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/env.h"
#include "ui/aura/display_manager.h"
#include "ui/aura/root_window.h"
#include "ui/aura/test/event_generator.h"
#include "ui/aura/window.h"
#include "ui/base/animation/animation_container_element.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/gfx/display.h"
#include "ui/gfx/screen.h"
#include "ui/views/widget/widget.h"

namespace ash {
namespace internal {

namespace {

void StepWidgetLayerAnimatorToEnd(views::Widget* widget) {
  ui::AnimationContainerElement* element =
      static_cast<ui::AnimationContainerElement*>(
      widget->GetNativeView()->layer()->GetAnimator());
  element->Step(base::TimeTicks::Now() + base::TimeDelta::FromSeconds(1));
}

ShelfLayoutManager* GetShelfLayoutManager() {
  aura::Window* window = Shell::GetContainer(
      Shell::GetPrimaryRootWindow(),
      internal::kShellWindowId_LauncherContainer);
  return static_cast<ShelfLayoutManager*>(window->layout_manager());
}

}  // namespace

class ShelfLayoutManagerTest : public ash::test::AshTestBase {
 public:
  ShelfLayoutManagerTest() {}

  void SetState(ShelfLayoutManager* shelf,
                ShelfLayoutManager::VisibilityState state) {
    shelf->SetState(state);
  }

  void UpdateAutoHideStateNow() {
    GetShelfLayoutManager()->UpdateAutoHideStateNow();
  }

  aura::Window* CreateTestWindow() {
    aura::Window* window = new aura::Window(NULL);
    window->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_NORMAL);
    window->SetType(aura::client::WINDOW_TYPE_NORMAL);
    window->Init(ui::LAYER_TEXTURED);
    aura::Window* parent = Shell::GetContainer(
        Shell::GetPrimaryRootWindow(),
        internal::kShellWindowId_DefaultContainer);
    window->SetParent(parent);
    return window;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(ShelfLayoutManagerTest);
};

// Fails on Mac only.  Need to be implemented.  http://crbug.com/111279.
#if defined(OS_MACOSX)
#define MAYBE_SetVisible FAILS_SetVisible
#else
#define MAYBE_SetVisible SetVisible
#endif
// Makes sure SetVisible updates work area and widget appropriately.
TEST_F(ShelfLayoutManagerTest, MAYBE_SetVisible) {
  ShelfLayoutManager* shelf = GetShelfLayoutManager();
  // Force an initial layout.
  shelf->LayoutShelf();
  EXPECT_EQ(ShelfLayoutManager::VISIBLE, shelf->visibility_state());

  gfx::Rect status_bounds(shelf->status()->GetWindowBoundsInScreen());
  gfx::Rect launcher_bounds(
      shelf->launcher_widget()->GetWindowBoundsInScreen());
  int shelf_height = shelf->GetIdealBounds().height();

  const aura::DisplayManager* manager =
      aura::Env::GetInstance()->display_manager();
  const gfx::Display& display =
      manager->GetDisplayNearestWindow(Shell::GetPrimaryRootWindow());
  ASSERT_NE(-1, display.id());
  // Bottom inset should be the max of widget heights.
  EXPECT_EQ(shelf_height,
            display.bounds().bottom() - display.work_area().bottom());

  // Hide the shelf.
  SetState(shelf, ShelfLayoutManager::HIDDEN);
  // Run the animation to completion.
  StepWidgetLayerAnimatorToEnd(shelf->launcher_widget());
  StepWidgetLayerAnimatorToEnd(shelf->status());
  EXPECT_EQ(ShelfLayoutManager::HIDDEN, shelf->visibility_state());
  EXPECT_EQ(0,
            display.bounds().bottom() - display.work_area().bottom());

  // Make sure the bounds of the two widgets changed.
  EXPECT_GE(shelf->launcher_widget()->GetNativeView()->bounds().y(),
            gfx::Screen::GetPrimaryDisplay().bounds().bottom());
  EXPECT_GE(shelf->status()->GetNativeView()->bounds().y(),
            gfx::Screen::GetPrimaryDisplay().bounds().bottom());

  // And show it again.
  SetState(shelf, ShelfLayoutManager::VISIBLE);
  // Run the animation to completion.
  StepWidgetLayerAnimatorToEnd(shelf->launcher_widget());
  StepWidgetLayerAnimatorToEnd(shelf->status());
  EXPECT_EQ(ShelfLayoutManager::VISIBLE, shelf->visibility_state());
  EXPECT_EQ(shelf_height,
            display.bounds().bottom() - display.work_area().bottom());

  // Make sure the bounds of the two widgets changed.
  launcher_bounds = shelf->launcher_widget()->GetNativeView()->bounds();
  int bottom = gfx::Screen::GetPrimaryDisplay().bounds().bottom() -
      shelf_height;
  EXPECT_EQ(launcher_bounds.y(),
            bottom + (shelf->GetIdealBounds().height() -
                      launcher_bounds.height()) / 2);
  status_bounds = shelf->status()->GetNativeView()->bounds();
  EXPECT_EQ(status_bounds.y(),
            bottom + shelf_height - status_bounds.height());
}

// Makes sure LayoutShelf invoked while animating cleans things up.
TEST_F(ShelfLayoutManagerTest, LayoutShelfWhileAnimating) {
  ShelfLayoutManager* shelf = GetShelfLayoutManager();
  // Force an initial layout.
  shelf->LayoutShelf();
  EXPECT_EQ(ShelfLayoutManager::VISIBLE, shelf->visibility_state());

  const aura::DisplayManager* manager =
      aura::Env::GetInstance()->display_manager();
  const gfx::Display& display =
      manager->GetDisplayNearestWindow(Shell::GetPrimaryRootWindow());

  // Hide the shelf.
  SetState(shelf, ShelfLayoutManager::HIDDEN);
  shelf->LayoutShelf();
  EXPECT_EQ(ShelfLayoutManager::HIDDEN, shelf->visibility_state());
  EXPECT_EQ(0, display.bounds().bottom() - display.work_area().bottom());

  // Make sure the bounds of the two widgets changed.
  EXPECT_GE(shelf->launcher_widget()->GetNativeView()->bounds().y(),
            gfx::Screen::GetPrimaryDisplay().bounds().bottom());
  EXPECT_GE(shelf->status()->GetNativeView()->bounds().y(),
            gfx::Screen::GetPrimaryDisplay().bounds().bottom());
}

// Makes sure the launcher is initially sized correctly.
TEST_F(ShelfLayoutManagerTest, LauncherInitiallySized) {
  Launcher* launcher = Shell::GetInstance()->launcher();
  ASSERT_TRUE(launcher);
  ShelfLayoutManager* shelf_layout_manager = GetShelfLayoutManager();
  ASSERT_TRUE(shelf_layout_manager);
  ASSERT_TRUE(shelf_layout_manager->status());
  int status_width =
      shelf_layout_manager->status()->GetWindowBoundsInScreen().width();
  // Test only makes sense if the status is > 0, which is better be.
  EXPECT_GT(status_width, 0);
  EXPECT_EQ(status_width, launcher->status_size().width());
}

// Makes sure the launcher is sized when the status area changes size.
TEST_F(ShelfLayoutManagerTest, LauncherUpdatedWhenStatusAreaChangesSize) {
  Launcher* launcher = Shell::GetInstance()->launcher();
  ASSERT_TRUE(launcher);
  ShelfLayoutManager* shelf_layout_manager = GetShelfLayoutManager();
  ASSERT_TRUE(shelf_layout_manager);
  ASSERT_TRUE(shelf_layout_manager->status());
  shelf_layout_manager->status()->SetBounds(gfx::Rect(0, 0, 200, 200));
  EXPECT_EQ(200, launcher->status_size().width());
}

// Verifies when the shell is deleted with a full screen window we don't
// crash. This test is here as originally the crash was in ShelfLayoutManager.
TEST_F(ShelfLayoutManagerTest, DontReferenceLauncherAfterDeletion) {
  views::Widget* widget = new views::Widget;
  views::Widget::InitParams params(views::Widget::InitParams::TYPE_WINDOW);
  params.bounds = gfx::Rect(0, 0, 200, 200);
  // Widget is now owned by the parent window.
  widget->Init(params);
  widget->SetFullscreen(true);
}

// Various assertions around auto-hide.
TEST_F(ShelfLayoutManagerTest, AutoHide) {
  aura::RootWindow* root = Shell::GetPrimaryRootWindow();
  aura::test::EventGenerator generator(root, root);
  generator.MoveMouseTo(0, 0);

  ShelfLayoutManager* shelf = GetShelfLayoutManager();
  views::Widget* widget = new views::Widget;
  views::Widget::InitParams params(views::Widget::InitParams::TYPE_WINDOW);
  params.bounds = gfx::Rect(0, 0, 200, 200);
  // Widget is now owned by the parent window.
  widget->Init(params);
  widget->Maximize();
  widget->Show();
  EXPECT_EQ(ShelfLayoutManager::AUTO_HIDE, shelf->visibility_state());
  EXPECT_EQ(ShelfLayoutManager::AUTO_HIDE_HIDDEN, shelf->auto_hide_state());

  // LayoutShelf() forces the animation to completion, at which point the
  // launcher should go off the screen.
  shelf->LayoutShelf();
  EXPECT_EQ(root->bounds().bottom() - ShelfLayoutManager::kAutoHideSize,
            shelf->launcher_widget()->GetWindowBoundsInScreen().y());
  EXPECT_EQ(root->bounds().bottom() - ShelfLayoutManager::kAutoHideSize,
            gfx::Screen::GetDisplayNearestWindow(root).work_area().bottom());

  // Move the mouse to the bottom of the screen.
  generator.MoveMouseTo(0, root->bounds().bottom() - 1);

  // Shelf should be shown again (but it shouldn't have changed the work area).
  SetState(shelf, ShelfLayoutManager::AUTO_HIDE);
  EXPECT_EQ(ShelfLayoutManager::AUTO_HIDE_SHOWN, shelf->auto_hide_state());
  shelf->LayoutShelf();
  EXPECT_EQ(root->bounds().bottom() - shelf->GetIdealBounds().height(),
            shelf->launcher_widget()->GetWindowBoundsInScreen().y());
  EXPECT_EQ(root->bounds().bottom() - ShelfLayoutManager::kAutoHideSize,
            gfx::Screen::GetDisplayNearestWindow(root).work_area().bottom());

  // Move mouse back up.
  generator.MoveMouseTo(0, 0);
  SetState(shelf, ShelfLayoutManager::AUTO_HIDE);
  EXPECT_EQ(ShelfLayoutManager::AUTO_HIDE_HIDDEN, shelf->auto_hide_state());
  shelf->LayoutShelf();
  EXPECT_EQ(root->bounds().bottom() - ShelfLayoutManager::kAutoHideSize,
            shelf->launcher_widget()->GetWindowBoundsInScreen().y());

  // Drag mouse to bottom of screen.
  generator.PressLeftButton();
  generator.MoveMouseTo(0, root->bounds().bottom() - 1);
  UpdateAutoHideStateNow();
  EXPECT_EQ(ShelfLayoutManager::AUTO_HIDE_HIDDEN, shelf->auto_hide_state());

  generator.ReleaseLeftButton();
  generator.MoveMouseTo(1, root->bounds().bottom() - 1);
  UpdateAutoHideStateNow();
  EXPECT_EQ(ShelfLayoutManager::AUTO_HIDE_SHOWN, shelf->auto_hide_state());
  generator.PressLeftButton();
  generator.MoveMouseTo(1, root->bounds().bottom() - 1);
  UpdateAutoHideStateNow();
  EXPECT_EQ(ShelfLayoutManager::AUTO_HIDE_SHOWN, shelf->auto_hide_state());
}

// Assertions around the lock screen showing.
TEST_F(ShelfLayoutManagerTest, VisibleWhenLockScreenShowing) {
  // Since ShelfLayoutManager queries for mouse location, move the mouse so
  // it isn't over the shelf.
  aura::test::EventGenerator generator(
      Shell::GetPrimaryRootWindow(), gfx::Point());
  generator.MoveMouseTo(0, 0);

  ShelfLayoutManager* shelf = GetShelfLayoutManager();
  views::Widget* widget = new views::Widget;
  views::Widget::InitParams params(views::Widget::InitParams::TYPE_WINDOW);
  params.bounds = gfx::Rect(0, 0, 200, 200);
  // Widget is now owned by the parent window.
  widget->Init(params);
  widget->Maximize();
  widget->Show();
  EXPECT_EQ(ShelfLayoutManager::AUTO_HIDE, shelf->visibility_state());
  EXPECT_EQ(ShelfLayoutManager::AUTO_HIDE_HIDDEN, shelf->auto_hide_state());

  aura::RootWindow* root = Shell::GetPrimaryRootWindow();
  // LayoutShelf() forces the animation to completion, at which point the
  // launcher should go off the screen.
  shelf->LayoutShelf();
  EXPECT_EQ(root->bounds().bottom() - ShelfLayoutManager::kAutoHideSize,
            shelf->launcher_widget()->GetWindowBoundsInScreen().y());

  aura::Window* lock_container = Shell::GetContainer(
      Shell::GetPrimaryRootWindow(),
      internal::kShellWindowId_LockScreenContainer);

  views::Widget* lock_widget = new views::Widget;
  views::Widget::InitParams lock_params(
      views::Widget::InitParams::TYPE_WINDOW);
  lock_params.bounds = gfx::Rect(0, 0, 200, 200);
  lock_params.parent = lock_container;
  // Widget is now owned by the parent window.
  lock_widget->Init(lock_params);
  lock_widget->Maximize();
  lock_widget->Show();

  // Lock the screen.
  Shell::GetInstance()->delegate()->LockScreen();
  shelf->UpdateVisibilityState();
  // Showing a widget in the lock screen should force the shelf to be visibile.
  EXPECT_EQ(ShelfLayoutManager::VISIBLE, shelf->visibility_state());

  Shell::GetInstance()->delegate()->UnlockScreen();
  shelf->UpdateVisibilityState();
  EXPECT_EQ(ShelfLayoutManager::AUTO_HIDE, shelf->visibility_state());
}

// Assertions around SetAutoHideBehavior.
TEST_F(ShelfLayoutManagerTest, SetAutoHideBehavior) {
  // Since ShelfLayoutManager queries for mouse location, move the mouse so
  // it isn't over the shelf.
  aura::test::EventGenerator generator(
      Shell::GetPrimaryRootWindow(), gfx::Point());
  generator.MoveMouseTo(0, 0);

  ShelfLayoutManager* shelf = GetShelfLayoutManager();
  views::Widget* widget = new views::Widget;
  views::Widget::InitParams params(views::Widget::InitParams::TYPE_WINDOW);
  params.bounds = gfx::Rect(0, 0, 200, 200);
  // Widget is now owned by the parent window.
  widget->Init(params);
  widget->Show();
  aura::Window* window = widget->GetNativeWindow();
  gfx::Rect display_bounds(
      gfx::Screen::GetDisplayNearestWindow(window).bounds());
  EXPECT_EQ(display_bounds.bottom() - ShelfLayoutManager::kAutoHideSize,
            shelf->GetMaximizedWindowBounds(window).bottom());
  EXPECT_EQ(ShelfLayoutManager::VISIBLE, shelf->visibility_state());

  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);
  EXPECT_EQ(ShelfLayoutManager::AUTO_HIDE, shelf->visibility_state());
  EXPECT_EQ(display_bounds.bottom() - ShelfLayoutManager::kAutoHideSize,
            shelf->GetMaximizedWindowBounds(window).bottom());

  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_DEFAULT);
  EXPECT_EQ(ShelfLayoutManager::VISIBLE, shelf->visibility_state());
  EXPECT_EQ(display_bounds.bottom() - ShelfLayoutManager::kAutoHideSize,
            shelf->GetMaximizedWindowBounds(window).bottom());

  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_NEVER);
  EXPECT_EQ(ShelfLayoutManager::VISIBLE, shelf->visibility_state());
  EXPECT_GT(display_bounds.bottom() - ShelfLayoutManager::kAutoHideSize,
            shelf->GetMaximizedWindowBounds(window).bottom());

  widget->Maximize();
  EXPECT_EQ(ShelfLayoutManager::VISIBLE, shelf->visibility_state());
  EXPECT_EQ(gfx::Screen::GetDisplayNearestWindow(window).work_area().bottom(),
            widget->GetWorkAreaBoundsInScreen().bottom());

  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);
  EXPECT_EQ(ShelfLayoutManager::AUTO_HIDE, shelf->visibility_state());
  EXPECT_EQ(gfx::Screen::GetDisplayNearestWindow(window).work_area().bottom(),
            widget->GetWorkAreaBoundsInScreen().bottom());

  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_NEVER);
  EXPECT_EQ(ShelfLayoutManager::VISIBLE, shelf->visibility_state());
  EXPECT_EQ(gfx::Screen::GetDisplayNearestWindow(window).work_area().bottom(),
            widget->GetWorkAreaBoundsInScreen().bottom());
}

// Verifies the shelf is visible when status/launcher is focused.
TEST_F(ShelfLayoutManagerTest, VisibleWhenStatusOrLauncherFocused) {
  // Since ShelfLayoutManager queries for mouse location, move the mouse so
  // it isn't over the shelf.
  aura::test::EventGenerator generator(
      Shell::GetPrimaryRootWindow(), gfx::Point());
  generator.MoveMouseTo(0, 0);

  ShelfLayoutManager* shelf = GetShelfLayoutManager();
  views::Widget* widget = new views::Widget;
  views::Widget::InitParams params(views::Widget::InitParams::TYPE_WINDOW);
  params.bounds = gfx::Rect(0, 0, 200, 200);
  // Widget is now owned by the parent window.
  widget->Init(params);
  widget->Show();
  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);
  EXPECT_EQ(ShelfLayoutManager::AUTO_HIDE, shelf->visibility_state());
  EXPECT_EQ(ShelfLayoutManager::AUTO_HIDE_HIDDEN, shelf->auto_hide_state());

  // Focus the launcher. Have to go through the focus cycler as normal focus
  // requests to it do nothing.
  shelf->launcher()->GetFocusCycler()->RotateFocus(FocusCycler::FORWARD);
  EXPECT_EQ(ShelfLayoutManager::AUTO_HIDE_SHOWN, shelf->auto_hide_state());

  widget->Activate();
  EXPECT_EQ(ShelfLayoutManager::AUTO_HIDE_HIDDEN, shelf->auto_hide_state());

  // Trying to activate the status should fail, since we only allow activating
  // it when the user is using the keyboard (i.e. through FocusCycler).
  shelf->status()->Activate();
  EXPECT_EQ(ShelfLayoutManager::AUTO_HIDE_HIDDEN, shelf->auto_hide_state());

  shelf->launcher()->GetFocusCycler()->RotateFocus(FocusCycler::FORWARD);
  EXPECT_EQ(ShelfLayoutManager::AUTO_HIDE_SHOWN, shelf->auto_hide_state());
}

// Makes sure shelf will be visible when app list opens as shelf is in VISIBLE
// state,and toggling app list won't change shelf visibility state.
TEST_F(ShelfLayoutManagerTest, OpenAppListWithShelfVisibleState) {
  Shell* shell = Shell::GetInstance();
  ShelfLayoutManager* shelf = Shell::GetInstance()->shelf();
  shelf->LayoutShelf();
  shell->SetShelfAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_DEFAULT);

  // Create a normal unmaximized windowm shelf should be visible.
  aura::Window* window = CreateTestWindow();
  window->SetBounds(gfx::Rect(0, 0, 100, 100));
  window->Show();
  EXPECT_FALSE(shell->GetAppListTargetVisibility());
  EXPECT_EQ(ShelfLayoutManager::VISIBLE, shelf->visibility_state());

  // Toggle app list to show, and the shelf stays visible.
  shell->ToggleAppList();
  EXPECT_TRUE(shell->GetAppListTargetVisibility());
  EXPECT_EQ(ShelfLayoutManager::VISIBLE, shelf->visibility_state());

  // Toggle app list to hide, and the shelf stays visible.
  shell->ToggleAppList();
  EXPECT_FALSE(shell->GetAppListTargetVisibility());
  EXPECT_EQ(ShelfLayoutManager::VISIBLE, shelf->visibility_state());
}

// Makes sure shelf will be shown with AUTO_HIDE_SHOWN state when app list opens
// as shelf is in AUTO_HIDE state, and toggling app list won't change shelf
// visibility state.
TEST_F(ShelfLayoutManagerTest, OpenAppListWithShelfAutoHideState) {
  Shell* shell = Shell::GetInstance();
  ShelfLayoutManager* shelf = Shell::GetInstance()->shelf();
  shelf->LayoutShelf();
  shell->SetShelfAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_DEFAULT);

  // Create a window and show it in maximized state.
  aura::Window* window = CreateTestWindow();
  window->SetBounds(gfx::Rect(0, 0, 100, 100));
  window->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);
  window->Show();

  EXPECT_FALSE(shell->GetAppListTargetVisibility());
  EXPECT_EQ(ShelfLayoutManager::AUTO_HIDE, shelf->visibility_state());

  // Toggle app list to show.
  shell->ToggleAppList();
  // The shelf's auto hide state won't be changed until the timer fires, so
  // calling shell->UpdateShelfVisibility() is kind of manually helping it to
  // update the state.
  shell->UpdateShelfVisibility();
  EXPECT_TRUE(shell->GetAppListTargetVisibility());
  EXPECT_EQ(ShelfLayoutManager::AUTO_HIDE, shelf->visibility_state());
  EXPECT_EQ(ShelfLayoutManager::AUTO_HIDE_SHOWN, shelf->auto_hide_state());

  // Toggle app list to hide.
  shell->ToggleAppList();
  EXPECT_FALSE(shell->GetAppListTargetVisibility());
  EXPECT_EQ(ShelfLayoutManager::AUTO_HIDE, shelf->visibility_state());
}

// Makes sure shelf will be hidden when app list opens as shelf is in HIDDEN
// state, and toggling app list won't change shelf visibility state.
TEST_F(ShelfLayoutManagerTest, OpenAppListWithShelfHiddenState) {
  Shell* shell = Shell::GetInstance();
  ShelfLayoutManager* shelf = Shell::GetInstance()->shelf();
  // For shelf to be visible, app list is not open in initial state.
  shelf->LayoutShelf();

  // Create a window and make it full screen.
  aura::Window* window = CreateTestWindow();
  window->SetBounds(gfx::Rect(0, 0, 100, 100));
  window->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_FULLSCREEN);
  window->Show();

  // App list and shelf is not shown.
  EXPECT_FALSE(shell->GetAppListTargetVisibility());
  EXPECT_EQ(ShelfLayoutManager::HIDDEN, shelf->visibility_state());

  // Toggle app list to show.
  shell->ToggleAppList();
  EXPECT_TRUE(shell->GetAppListTargetVisibility());
  EXPECT_EQ(ShelfLayoutManager::HIDDEN, shelf->visibility_state());

  // Toggle app list to hide.
  shell->ToggleAppList();
  EXPECT_FALSE(shell->GetAppListTargetVisibility());
  EXPECT_EQ(ShelfLayoutManager::HIDDEN, shelf->visibility_state());
}

// Tests SHELF_ALIGNMENT_LEFT and SHELF_ALIGNMENT_RIGHT.
TEST_F(ShelfLayoutManagerTest, SetAlignment) {
  ShelfLayoutManager* shelf = GetShelfLayoutManager();
  // Force an initial layout.
  shelf->LayoutShelf();
  EXPECT_EQ(ShelfLayoutManager::VISIBLE, shelf->visibility_state());

  shelf->SetAlignment(SHELF_ALIGNMENT_LEFT);

  gfx::Rect launcher_bounds(
      shelf->launcher_widget()->GetWindowBoundsInScreen());
  const aura::DisplayManager* manager =
      aura::Env::GetInstance()->display_manager();
  gfx::Display display =
      manager->GetDisplayNearestWindow(Shell::GetPrimaryRootWindow());
  ASSERT_NE(-1, display.id());
  EXPECT_EQ(shelf->GetIdealBounds().width(),
            display.GetWorkAreaInsets().left());
  EXPECT_GE(
      launcher_bounds.width(),
      shelf->launcher_widget()->GetContentsView()->GetPreferredSize().width());
  EXPECT_EQ(SHELF_ALIGNMENT_LEFT,
            Shell::GetInstance()->system_tray()->shelf_alignment());
  gfx::Rect status_bounds(shelf->status()->GetWindowBoundsInScreen());
  EXPECT_GE(status_bounds.width(),
            shelf->status()->GetContentsView()->GetPreferredSize().width());
  EXPECT_EQ(shelf->GetIdealBounds().width(),
            display.GetWorkAreaInsets().left());
  EXPECT_EQ(0, display.GetWorkAreaInsets().top());
  EXPECT_EQ(0, display.GetWorkAreaInsets().bottom());
  EXPECT_EQ(0, display.GetWorkAreaInsets().right());
  EXPECT_EQ(display.bounds().x(), launcher_bounds.x());
  EXPECT_EQ(display.bounds().y(), launcher_bounds.y());
  EXPECT_EQ(display.bounds().height(), launcher_bounds.height());

  shelf->SetAlignment(SHELF_ALIGNMENT_RIGHT);
  launcher_bounds = shelf->launcher_widget()->GetWindowBoundsInScreen();
  display = manager->GetDisplayNearestWindow(Shell::GetPrimaryRootWindow());
  ASSERT_NE(-1, display.id());
  EXPECT_EQ(shelf->GetIdealBounds().width(),
            display.GetWorkAreaInsets().right());
  EXPECT_GE(
      launcher_bounds.width(),
      shelf->launcher_widget()->GetContentsView()->GetPreferredSize().width());
  EXPECT_EQ(SHELF_ALIGNMENT_RIGHT,
            Shell::GetInstance()->system_tray()->shelf_alignment());
  status_bounds = gfx::Rect(shelf->status()->GetWindowBoundsInScreen());
  EXPECT_GE(status_bounds.width(),
            shelf->status()->GetContentsView()->GetPreferredSize().width());
  EXPECT_EQ(shelf->GetIdealBounds().width(),
            display.GetWorkAreaInsets().right());
  EXPECT_EQ(0, display.GetWorkAreaInsets().top());
  EXPECT_EQ(0, display.GetWorkAreaInsets().bottom());
  EXPECT_EQ(0, display.GetWorkAreaInsets().left());
  EXPECT_EQ(display.work_area().right(), launcher_bounds.x());
  EXPECT_EQ(display.bounds().y(), launcher_bounds.y());
  EXPECT_EQ(display.bounds().height(), launcher_bounds.height());
}

}  // namespace internal
}  // namespace ash
