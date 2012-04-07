// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/workspace/workspace_manager.h"

#include "ash/shell_window_ids.h"
#include "ash/wm/activation_controller.h"
#include "ash/wm/property_util.h"
#include "ash/wm/window_util.h"
#include "ash/wm/workspace/workspace.h"
#include "ash/wm/workspace/workspace_layout_manager.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/root_window.h"
#include "ui/aura/screen_aura.h"
#include "ui/aura/test/aura_test_base.h"
#include "ui/aura/window.h"
#include "ui/base/ui_base_types.h"
#include "ui/gfx/screen.h"

using aura::Window;

namespace ash {
namespace internal {

class WorkspaceManagerTest : public aura::test::AuraTestBase {
 public:
  WorkspaceManagerTest() : layout_manager_(NULL) {
    aura::RootWindow::GetInstance()->set_id(
        internal::kShellWindowId_DefaultContainer);
    activation_controller_.reset(new internal::ActivationController);
    activation_controller_->set_default_container_for_test(
        aura::RootWindow::GetInstance());
  }
  virtual ~WorkspaceManagerTest() {}

  virtual void SetUp() OVERRIDE {
    aura::test::AuraTestBase::SetUp();
    manager_.reset(new WorkspaceManager(viewport()));
    layout_manager_ = new WorkspaceLayoutManager(manager_.get());
    viewport()->SetLayoutManager(layout_manager_);
  }

  virtual void TearDown() OVERRIDE {
    manager_.reset();
    aura::test::AuraTestBase::TearDown();
  }

  aura::Window* CreateTestWindowUnparented() {
    aura::Window* window = new aura::Window(NULL);
    window->SetIntProperty(aura::client::kShowStateKey, ui::SHOW_STATE_NORMAL);
    window->SetType(aura::client::WINDOW_TYPE_NORMAL);
    window->Init(ui::Layer::LAYER_TEXTURED);
    return window;
  }

  aura::Window* CreateTestWindow() {
    aura::Window* window = new aura::Window(NULL);
    window->SetIntProperty(aura::client::kShowStateKey, ui::SHOW_STATE_NORMAL);
    window->SetType(aura::client::WINDOW_TYPE_NORMAL);
    window->Init(ui::Layer::LAYER_TEXTURED);
    window->SetParent(viewport());
    return window;
  }

  aura::Window* viewport() {
    return aura::RootWindow::GetInstance();
  }

  const std::vector<Workspace*>& workspaces() const {
    return manager_->workspaces_;
  }

  gfx::Rect GetWorkAreaBounds() {
    return manager_->GetWorkAreaBounds();
  }

  gfx::Rect GetFullscreenBounds(aura::Window* window) {
    return gfx::Screen::GetMonitorAreaNearestWindow(window);
  }

  Workspace* active_workspace() {
    return manager_->active_workspace_;
  }

  Workspace* FindBy(aura::Window* window) const {
    return manager_->FindBy(window);
  }

  scoped_ptr<WorkspaceManager> manager_;

  // Owned by viewport().
  WorkspaceLayoutManager* layout_manager_;

 private:
  scoped_ptr<internal::ActivationController> activation_controller_;

  DISALLOW_COPY_AND_ASSIGN(WorkspaceManagerTest);
};

// Assertions around adding a normal window.
TEST_F(WorkspaceManagerTest, AddNormalWindowWhenEmpty) {
  scoped_ptr<Window> w1(CreateTestWindow());
  w1->SetBounds(gfx::Rect(0, 0, 250, 251));

  ASSERT_TRUE(manager_->IsManagedWindow(w1.get()));
  EXPECT_FALSE(FindBy(w1.get()));

  w1->Show();

  ASSERT_TRUE(w1->layer() != NULL);
  EXPECT_TRUE(w1->layer()->visible());

  EXPECT_EQ(250, w1->bounds().width());
  EXPECT_EQ(251, w1->bounds().height());

  // Should be 1 workspace, TYPE_NORNMAL with w1.
  ASSERT_EQ(1u, workspaces().size());
  EXPECT_EQ(Workspace::TYPE_NORMAL, workspaces()[0]->type());
  ASSERT_EQ(1u, workspaces()[0]->windows().size());
  EXPECT_EQ(w1.get(), workspaces()[0]->windows()[0]);
}

// Assertions around maximizing/unmaximizing.
TEST_F(WorkspaceManagerTest, SingleMaximizeWindow) {
  scoped_ptr<Window> w1(CreateTestWindow());
  w1->SetBounds(gfx::Rect(0, 0, 250, 251));

  ASSERT_TRUE(manager_->IsManagedWindow(w1.get()));

  w1->Show();

  ASSERT_TRUE(w1->layer() != NULL);
  EXPECT_TRUE(w1->layer()->visible());

  EXPECT_EQ(250, w1->bounds().width());
  EXPECT_EQ(251, w1->bounds().height());

  // Maximize the window.
  w1->SetIntProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);

  // Should be 1 workspace, TYPE_MAXIMIZED with w1.
  ASSERT_EQ(1u, workspaces().size());
  EXPECT_EQ(Workspace::TYPE_MAXIMIZED, workspaces()[0]->type());
  ASSERT_EQ(1u, workspaces()[0]->windows().size());
  EXPECT_EQ(w1.get(), workspaces()[0]->windows()[0]);
  EXPECT_EQ(GetWorkAreaBounds().width(), w1->bounds().width());
  EXPECT_EQ(GetWorkAreaBounds().height(), w1->bounds().height());

  // Restore the window.
  w1->SetIntProperty(aura::client::kShowStateKey, ui::SHOW_STATE_NORMAL);

  // Should be 1 workspace, TYPE_NORMAL with w1.
  ASSERT_EQ(1u, workspaces().size());
  EXPECT_EQ(Workspace::TYPE_NORMAL, workspaces()[0]->type());
  ASSERT_EQ(1u, workspaces()[0]->windows().size());
  EXPECT_EQ(w1.get(), workspaces()[0]->windows()[0]);
  EXPECT_EQ(250, w1->bounds().width());
  EXPECT_EQ(251, w1->bounds().height());
}

// Assertions around closing the last window in a workspace.
TEST_F(WorkspaceManagerTest, CloseLastWindowInWorkspace) {
  scoped_ptr<Window> w1(CreateTestWindow());
  scoped_ptr<Window> w2(CreateTestWindow());
  w1->SetBounds(gfx::Rect(0, 0, 250, 251));
  w1->Show();
  w2->SetIntProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);
  w2->Show();

  // Should be 2 workspaces, TYPE_NORMAL with w1, and TYPE_MAXIMIZED with w2.
  ASSERT_EQ(2u, workspaces().size());
  EXPECT_EQ(Workspace::TYPE_NORMAL, workspaces()[0]->type());
  ASSERT_EQ(1u, workspaces()[0]->windows().size());
  EXPECT_EQ(w1.get(), workspaces()[0]->windows()[0]);
  EXPECT_EQ(Workspace::TYPE_MAXIMIZED, workspaces()[1]->type());
  ASSERT_EQ(1u, workspaces()[1]->windows().size());
  EXPECT_EQ(w2.get(), workspaces()[1]->windows()[0]);
  EXPECT_FALSE(w1->layer()->visible());
  EXPECT_TRUE(w2->layer()->visible());
  // TYPE_MAXIMIZED workspace should be active.
  EXPECT_EQ(workspaces()[1], active_workspace());

  // Close w2.
  w2.reset();

  // Should have one workspace, TYPE_NORMAL with w1.
  ASSERT_EQ(1u, workspaces().size());
  EXPECT_EQ(Workspace::TYPE_NORMAL, workspaces()[0]->type());
  ASSERT_EQ(1u, workspaces()[0]->windows().size());
  EXPECT_EQ(w1.get(), workspaces()[0]->windows()[0]);
  EXPECT_TRUE(w1->layer()->visible());
  EXPECT_EQ(workspaces()[0], active_workspace());
}

// Assertions around adding a maximized window when empty.
TEST_F(WorkspaceManagerTest, AddMaximizedWindowWhenEmpty) {
  scoped_ptr<Window> w1(CreateTestWindow());
  w1->SetBounds(gfx::Rect(0, 0, 250, 251));
  w1->SetIntProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);
  w1->Show();

  ASSERT_TRUE(w1->layer() != NULL);
  EXPECT_TRUE(w1->layer()->visible());
  gfx::Rect work_area(
      gfx::Screen::GetMonitorWorkAreaNearestWindow(w1.get()));
  EXPECT_EQ(work_area.width(), w1->bounds().width());
  EXPECT_EQ(work_area.height(), w1->bounds().height());

  // Should be 1 workspace, TYPE_NORNMAL with w1.
  ASSERT_EQ(1u, workspaces().size());
  EXPECT_EQ(Workspace::TYPE_MAXIMIZED, workspaces()[0]->type());
  ASSERT_EQ(1u, workspaces()[0]->windows().size());
  EXPECT_EQ(w1.get(), workspaces()[0]->windows()[0]);
}

// Assertions around two windows and toggling one to be maximized.
TEST_F(WorkspaceManagerTest, MaximizeWithNormalWindow) {
  scoped_ptr<Window> w1(CreateTestWindow());
  scoped_ptr<Window> w2(CreateTestWindow());
  w1->SetBounds(gfx::Rect(0, 0, 250, 251));
  w1->Show();

  ASSERT_TRUE(w1->layer() != NULL);
  EXPECT_TRUE(w1->layer()->visible());

  w2->SetBounds(gfx::Rect(0, 0, 50, 51));
  w2->SetIntProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);
  w2->Show();

  // Should now be two workspaces.
  ASSERT_EQ(2u, workspaces().size());
  EXPECT_EQ(Workspace::TYPE_NORMAL, workspaces()[0]->type());
  ASSERT_EQ(1u, workspaces()[0]->windows().size());
  EXPECT_EQ(w1.get(), workspaces()[0]->windows()[0]);
  EXPECT_EQ(Workspace::TYPE_MAXIMIZED, workspaces()[1]->type());
  ASSERT_EQ(1u, workspaces()[1]->windows().size());
  EXPECT_EQ(w2.get(), workspaces()[1]->windows()[0]);
  ASSERT_TRUE(w1->layer() != NULL);
  EXPECT_FALSE(w1->layer()->visible());
  ASSERT_TRUE(w2->layer() != NULL);
  EXPECT_TRUE(w2->layer()->visible());

  gfx::Rect work_area(
      gfx::Screen::GetMonitorWorkAreaNearestWindow(w1.get()));
  EXPECT_EQ(work_area.width(), w2->bounds().width());
  EXPECT_EQ(work_area.height(), w2->bounds().height());

  // Restore w2, which should then go back to one workspace.
  w2->SetIntProperty(aura::client::kShowStateKey, ui::SHOW_STATE_NORMAL);
  ASSERT_EQ(1u, workspaces().size());
  EXPECT_EQ(Workspace::TYPE_NORMAL, workspaces()[0]->type());
  ASSERT_EQ(2u, workspaces()[0]->windows().size());
  EXPECT_EQ(w1.get(), workspaces()[0]->windows()[0]);
  EXPECT_EQ(w2.get(), workspaces()[0]->windows()[1]);
  EXPECT_EQ(50, w2->bounds().width());
  EXPECT_EQ(51, w2->bounds().height());
  ASSERT_TRUE(w1->layer() != NULL);
  EXPECT_TRUE(w1->layer()->visible());
  ASSERT_TRUE(w2->layer() != NULL);
  EXPECT_TRUE(w2->layer()->visible());
}

// Assertions around two maximized windows.
TEST_F(WorkspaceManagerTest, TwoMaximized) {
  scoped_ptr<Window> w1(CreateTestWindow());
  scoped_ptr<Window> w2(CreateTestWindow());
  w1->SetBounds(gfx::Rect(0, 0, 250, 251));
  w1->Show();
  w1->SetIntProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);

  w2->SetBounds(gfx::Rect(0, 0, 50, 51));
  w2->SetIntProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);
  w2->Show();

  // Should now be two workspaces.
  ASSERT_EQ(2u, workspaces().size());
  EXPECT_EQ(Workspace::TYPE_MAXIMIZED, workspaces()[0]->type());
  ASSERT_EQ(1u, workspaces()[0]->windows().size());
  EXPECT_EQ(w1.get(), workspaces()[0]->windows()[0]);
  EXPECT_EQ(Workspace::TYPE_MAXIMIZED, workspaces()[1]->type());
  ASSERT_EQ(1u, workspaces()[1]->windows().size());
  EXPECT_EQ(w2.get(), workspaces()[1]->windows()[0]);
  ASSERT_TRUE(w1->layer() != NULL);
  EXPECT_FALSE(w1->layer()->visible());
  ASSERT_TRUE(w2->layer() != NULL);
  EXPECT_TRUE(w2->layer()->visible());
}

// Makes sure requests to change the bounds of a normal window go through.
TEST_F(WorkspaceManagerTest, ChangeBoundsOfNormalWindow) {
  scoped_ptr<Window> w1(CreateTestWindow());
  w1->Show();

  EXPECT_TRUE(manager_->IsManagedWindow(w1.get()));
  // Setting the bounds should go through since the window is in the normal
  // workspace.
  w1->SetBounds(gfx::Rect(0, 0, 200, 500));
  EXPECT_EQ(200, w1->bounds().width());
  EXPECT_EQ(500, w1->bounds().height());
}

// Assertions around open new windows maximized.
TEST_F(WorkspaceManagerTest, OpenNewWindowsMaximized) {
  scoped_ptr<Window> w1(CreateTestWindowUnparented());

  // Default is true for open new windows maximized.
  EXPECT_TRUE(manager_->open_new_windows_maximized());
  // SHOW_STATE_DEFAULT should end up maximized.
  w1->SetIntProperty(aura::client::kShowStateKey, ui::SHOW_STATE_DEFAULT);
  w1->SetBounds(gfx::Rect(50, 51, 52, 53));
  w1->SetParent(viewport());
  // Maximized state and bounds should be set as soon as w1 is added to the
  // parent.
  EXPECT_TRUE(window_util::IsWindowMaximized(w1.get()));
  EXPECT_EQ(gfx::Screen::GetMonitorWorkAreaNearestWindow(w1.get()),
            w1->bounds());
  w1->Show();
  EXPECT_TRUE(window_util::IsWindowMaximized(w1.get()));
  EXPECT_EQ(gfx::Screen::GetMonitorWorkAreaNearestWindow(w1.get()),
            w1->bounds());
  // Restored bounds should be saved.
  ASSERT_TRUE(GetRestoreBounds(w1.get()));
  EXPECT_EQ(gfx::Rect(50, 51, 52, 53), *GetRestoreBounds(w1.get()));

  // Show state normal should end up normal.
  scoped_ptr<Window> w2(CreateTestWindow());
  w2->SetBounds(gfx::Rect(60, 61, 62, 63));
  w2->Show();
  EXPECT_EQ(gfx::Rect(60, 61, 62, 63), w2->bounds());
  EXPECT_EQ(ui::SHOW_STATE_NORMAL,
            w2->GetIntProperty(aura::client::kShowStateKey));

  // If open news windows maximized is false, SHOW_STATE_DEFAULT should end as
  // SHOW_STATE_NORMAL.
  manager_->set_open_new_windows_maximized(false);
  scoped_ptr<Window> w3(CreateTestWindowUnparented());
  // Show state default should end up normal when open new windows maximized is
  // false.
  w3->SetIntProperty(aura::client::kShowStateKey, ui::SHOW_STATE_DEFAULT);
  w3->SetBounds(gfx::Rect(70, 71, 72, 73));
  w3->SetParent(viewport());
  w3->Show();
  EXPECT_EQ(gfx::Rect(70, 71, 72, 73), w3->bounds());
  EXPECT_EQ(ui::SHOW_STATE_NORMAL,
            w3->GetIntProperty(aura::client::kShowStateKey));
}

// Assertions around grid size.
TEST_F(WorkspaceManagerTest, SnapToGrid) {
  manager_->set_grid_size(8);

  // Verify snap to grid when bounds are set before parented.
  scoped_ptr<Window> w1(CreateTestWindowUnparented());
  w1->SetBounds(gfx::Rect(1, 6, 25, 30));
  w1->SetParent(viewport());
  EXPECT_EQ(gfx::Rect(0, 8, 24, 32), w1->bounds());

  // Verify snap to grid when bounds are set after parented.
  w1.reset(CreateTestWindow());
  w1->SetBounds(gfx::Rect(1, 6, 25, 30));
  EXPECT_EQ(gfx::Rect(0, 8, 24, 32), w1->bounds());
}

// Assertions around a fullscreen window.
TEST_F(WorkspaceManagerTest, SingleFullscreenWindow) {
  scoped_ptr<Window> w1(CreateTestWindow());
  w1->SetBounds(gfx::Rect(0, 0, 250, 251));
  // Make the window fullscreen.
  w1->SetIntProperty(aura::client::kShowStateKey, ui::SHOW_STATE_FULLSCREEN);
  w1->Show();

  // Should be 1 workspace, TYPE_MAXIMIZED with w1.
  ASSERT_EQ(1u, workspaces().size());
  EXPECT_EQ(Workspace::TYPE_MAXIMIZED, workspaces()[0]->type());
  ASSERT_EQ(1u, workspaces()[0]->windows().size());
  EXPECT_EQ(w1.get(), workspaces()[0]->windows()[0]);
  EXPECT_EQ(GetFullscreenBounds(w1.get()).width(), w1->bounds().width());
  EXPECT_EQ(GetFullscreenBounds(w1.get()).height(), w1->bounds().height());

  // Restore the window.
  w1->SetIntProperty(aura::client::kShowStateKey, ui::SHOW_STATE_NORMAL);

  // Should be 1 workspace, TYPE_NORMAL with w1.
  ASSERT_EQ(1u, workspaces().size());
  EXPECT_EQ(Workspace::TYPE_NORMAL, workspaces()[0]->type());
  ASSERT_EQ(1u, workspaces()[0]->windows().size());
  EXPECT_EQ(w1.get(), workspaces()[0]->windows()[0]);
  EXPECT_EQ(250, w1->bounds().width());
  EXPECT_EQ(251, w1->bounds().height());

  // Back to fullscreen.
  w1->SetIntProperty(aura::client::kShowStateKey, ui::SHOW_STATE_FULLSCREEN);
  ASSERT_EQ(1u, workspaces().size());
  EXPECT_EQ(Workspace::TYPE_MAXIMIZED, workspaces()[0]->type());
  ASSERT_EQ(1u, workspaces()[0]->windows().size());
  EXPECT_EQ(w1.get(), workspaces()[0]->windows()[0]);
  EXPECT_EQ(GetFullscreenBounds(w1.get()).width(), w1->bounds().width());
  EXPECT_EQ(GetFullscreenBounds(w1.get()).height(), w1->bounds().height());
  ASSERT_TRUE(GetRestoreBounds(w1.get()));
  EXPECT_EQ(gfx::Rect(0, 0, 250, 251), *GetRestoreBounds(w1.get()));
}

}  // namespace internal
}  // namespace ash
