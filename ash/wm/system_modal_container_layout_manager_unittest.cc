// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/system_modal_container_layout_manager.h"

#include "ash/shell.h"
#include "ash/shell_window_ids.h"
#include "ash/test/aura_shell_test_base.h"
#include "ash/wm/window_util.h"
#include "base/compiler_specific.h"
#include "ui/aura/root_window.h"
#include "ui/aura/test/event_generator.h"
#include "ui/aura/window.h"
#include "ui/views/events/event.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

namespace ash {
namespace test {

namespace {

aura::Window* GetModalContainer() {
  return Shell::GetInstance()->GetContainer(
      ash::internal::kShellWindowId_SystemModalContainer);
}

aura::Window* GetDefaultContainer() {
  return Shell::GetInstance()->GetContainer(
      ash::internal::kShellWindowId_DefaultContainer);
}

class TestWindow : public views::WidgetDelegateView {
 public:
  explicit TestWindow(bool modal) : modal_(modal) {}
  virtual ~TestWindow() {}

  static aura::Window* OpenTestWindow(aura::Window* parent, bool modal) {
    DCHECK(!modal || (modal && parent));
    views::Widget* widget =
        views::Widget::CreateWindowWithParent(new TestWindow(modal), parent);
    widget->Show();
    return widget->GetNativeView();
  }

  // Overridden from views::View:
  virtual gfx::Size GetPreferredSize() OVERRIDE {
    return gfx::Size(50, 50);
  }

  // Overridden from views::WidgetDelegate:
  virtual views::View* GetContentsView() OVERRIDE {
    return this;
  }
  virtual ui::ModalType GetModalType() const OVERRIDE {
    return modal_ ? ui::MODAL_TYPE_SYSTEM : ui::MODAL_TYPE_NONE;
  }

 private:
  bool modal_;

  DISALLOW_COPY_AND_ASSIGN(TestWindow);
};

class EventTestWindow : public TestWindow {
 public:
  explicit EventTestWindow(bool modal) : TestWindow(modal),
                                         mouse_presses_(0) {}
  virtual ~EventTestWindow() {}

  aura::Window* OpenTestWindow(aura::Window* parent) {
    views::Widget* widget =
        views::Widget::CreateWindowWithParent(this, parent);
    widget->Show();
    return widget->GetNativeView();
  }

  // Overridden from views::View:
  virtual bool OnMousePressed(const views::MouseEvent& event) OVERRIDE {
    mouse_presses_++;
    return false;
  }

  int mouse_presses() const { return mouse_presses_; }
 private:
  int mouse_presses_;

  DISALLOW_COPY_AND_ASSIGN(EventTestWindow);
};

class TransientWindowObserver : public aura::WindowObserver {
 public:
  TransientWindowObserver() : destroyed_(false) {}
  virtual ~TransientWindowObserver() {}

  bool destroyed() const { return destroyed_; }

  // Overridden from aura::WindowObserver:
  virtual void OnWindowDestroyed(aura::Window* window) OVERRIDE {
    destroyed_ = true;
  }

 private:
  bool destroyed_;

  DISALLOW_COPY_AND_ASSIGN(TransientWindowObserver);
};

}  // namespace

typedef AuraShellTestBase SystemModalContainerLayoutManagerTest;

TEST_F(SystemModalContainerLayoutManagerTest, NonModalTransient) {
  scoped_ptr<aura::Window> parent(TestWindow::OpenTestWindow(NULL, false));
  aura::Window* transient = TestWindow::OpenTestWindow(parent.get(), false);
  TransientWindowObserver destruction_observer;
  transient->AddObserver(&destruction_observer);

  EXPECT_EQ(parent.get(), transient->transient_parent());
  EXPECT_EQ(GetDefaultContainer(), transient->parent());

  // The transient should be destroyed with its parent.
  parent.reset();
  EXPECT_TRUE(destruction_observer.destroyed());
}

TEST_F(SystemModalContainerLayoutManagerTest, ModalTransient) {
  scoped_ptr<aura::Window> parent(TestWindow::OpenTestWindow(NULL, false));
  // parent should be active.
  EXPECT_TRUE(IsActiveWindow(parent.get()));

  aura::Window* t1 = TestWindow::OpenTestWindow(parent.get(), true);
  TransientWindowObserver do1;
  t1->AddObserver(&do1);

  EXPECT_EQ(parent.get(), t1->transient_parent());
  EXPECT_EQ(GetModalContainer(), t1->parent());

  // t1 should now be active.
  EXPECT_TRUE(IsActiveWindow(t1));

  // Attempting to click the parent should result in no activation change.
  aura::test::EventGenerator e1(parent.get());
  e1.ClickLeftButton();
  EXPECT_TRUE(IsActiveWindow(t1));

  // Now open another modal transient parented to the original modal transient.
  aura::Window* t2 = TestWindow::OpenTestWindow(t1, true);
  TransientWindowObserver do2;
  t2->AddObserver(&do2);

  EXPECT_TRUE(IsActiveWindow(t2));

  EXPECT_EQ(t1, t2->transient_parent());
  EXPECT_EQ(GetModalContainer(), t2->parent());

  // t2 should still be active, even after clicking on t1.
  aura::test::EventGenerator e2(t1);
  e2.ClickLeftButton();
  EXPECT_TRUE(IsActiveWindow(t2));

  // Both transients should be destroyed with parent.
  parent.reset();
  EXPECT_TRUE(do1.destroyed());
  EXPECT_TRUE(do2.destroyed());
}

// Tests that we can activate an unrelated window after a modal window is closed
// for a window.
// TODO(beng): This test is disabled pending a solution re: visibility & target
//             visibility.
TEST_F(SystemModalContainerLayoutManagerTest,
       DISABLED_CanActivateAfterEndModalSession) {
  scoped_ptr<aura::Window> unrelated(TestWindow::OpenTestWindow(NULL, false));
  unrelated->SetBounds(gfx::Rect(100, 100, 50, 50));
  scoped_ptr<aura::Window> parent(TestWindow::OpenTestWindow(NULL, false));
  // parent should be active.
  EXPECT_TRUE(IsActiveWindow(parent.get()));

  scoped_ptr<aura::Window> transient(
      TestWindow::OpenTestWindow(parent.get(), true));
  // t1 should now be active.
  EXPECT_TRUE(IsActiveWindow(transient.get()));

  // Attempting to click the parent should result in no activation change.
  aura::test::EventGenerator e1(parent.get());
  e1.ClickLeftButton();
  EXPECT_TRUE(IsActiveWindow(transient.get()));

  // Now close the transient.
  transient.reset();

  // parent should now be active again.
  EXPECT_TRUE(IsActiveWindow(parent.get()));

  // Attempting to click unrelated should activate it.
  aura::test::EventGenerator e2(unrelated.get());
  e2.ClickLeftButton();
  EXPECT_TRUE(IsActiveWindow(unrelated.get()));
}

TEST_F(SystemModalContainerLayoutManagerTest,
       EventFocusContainers) {
  // Create a normal window and attempt to receive a click event.
  EventTestWindow* main_delegate = new EventTestWindow(false);
  scoped_ptr<aura::Window> main(main_delegate->OpenTestWindow(NULL));
  EXPECT_TRUE(IsActiveWindow(main.get()));
  aura::test::EventGenerator e1(main.get());
  e1.ClickLeftButton();
  EXPECT_EQ(1, main_delegate->mouse_presses());

  // Create a modal window for the main window and verify that the main window
  // no longer receives mouse events.
  EventTestWindow* transient_delegate = new EventTestWindow(true);
  aura::Window* transient = transient_delegate->OpenTestWindow(main.get());
  EXPECT_TRUE(IsActiveWindow(transient));
  e1.ClickLeftButton();
  EXPECT_EQ(1, transient_delegate->mouse_presses());

  // Create a window in the lock screen container and ensure that it receives
  // the mouse event instead of the modal window (crbug.com/110920).
  EventTestWindow* lock_delegate = new EventTestWindow(false);
  scoped_ptr<aura::Window> lock(lock_delegate->OpenTestWindow(
      Shell::GetInstance()->GetContainer(
          ash::internal::kShellWindowId_LockScreenContainer)));
  EXPECT_TRUE(IsActiveWindow(lock.get()));
  e1.ClickLeftButton();
  EXPECT_EQ(1, lock_delegate->mouse_presses());

  // Make sure that a modal container created by the lock screen can still
  // receive mouse events.
  EventTestWindow* lock_modal_delegate = new EventTestWindow(true);
  aura::Window* lock_modal = lock_modal_delegate->OpenTestWindow(lock.get());
  EXPECT_TRUE(IsActiveWindow(lock_modal));
  e1.ClickLeftButton();
  EXPECT_EQ(1, main_delegate->mouse_presses());

  // Verify that none of the other containers received any more mouse presses.
  EXPECT_EQ(1, transient_delegate->mouse_presses());
  EXPECT_EQ(1, lock_delegate->mouse_presses());
  EXPECT_EQ(1, lock_modal_delegate->mouse_presses());
}

}  // namespace test
}  // namespace ash
