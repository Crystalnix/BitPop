// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/aura/window.h"

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/stringprintf.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/aura/client/stacking_client.h"
#include "ui/aura/client/visibility_client.h"
#include "ui/aura/event.h"
#include "ui/aura/focus_manager.h"
#include "ui/aura/layout_manager.h"
#include "ui/aura/root_window.h"
#include "ui/aura/root_window_observer.h"
#include "ui/aura/test/aura_test_base.h"
#include "ui/aura/test/event_generator.h"
#include "ui/aura/test/test_windows.h"
#include "ui/aura/test/test_window_delegate.h"
#include "ui/aura/window_delegate.h"
#include "ui/aura/window_observer.h"
#include "ui/base/hit_test.h"
#include "ui/base/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas_skia.h"
#include "ui/gfx/compositor/layer.h"
#include "ui/gfx/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/screen.h"

namespace aura {
namespace test {

typedef AuraTestBase WindowTest;

namespace {

// Used for verifying destruction methods are invoked.
class DestroyTrackingDelegateImpl : public TestWindowDelegate {
 public:
  DestroyTrackingDelegateImpl()
      : destroying_count_(0),
        destroyed_count_(0),
        in_destroying_(false) {}

  void clear_destroying_count() { destroying_count_ = 0; }
  int destroying_count() const { return destroying_count_; }

  void clear_destroyed_count() { destroyed_count_ = 0; }
  int destroyed_count() const { return destroyed_count_; }

  bool in_destroying() const { return in_destroying_; }

  virtual void OnWindowDestroying() OVERRIDE {
    EXPECT_FALSE(in_destroying_);
    in_destroying_ = true;
    destroying_count_++;
  }

  virtual void OnWindowDestroyed() OVERRIDE {
    EXPECT_TRUE(in_destroying_);
    in_destroying_ = false;
    destroyed_count_++;
  }

 private:
  int destroying_count_;
  int destroyed_count_;
  bool in_destroying_;

  DISALLOW_COPY_AND_ASSIGN(DestroyTrackingDelegateImpl);
};

// Used to verify that when OnWindowDestroying is invoked the parent is also
// is in the process of being destroyed.
class ChildWindowDelegateImpl : public DestroyTrackingDelegateImpl {
 public:
  explicit ChildWindowDelegateImpl(
      DestroyTrackingDelegateImpl* parent_delegate)
      : parent_delegate_(parent_delegate) {
  }

  virtual void OnWindowDestroying() OVERRIDE {
    EXPECT_TRUE(parent_delegate_->in_destroying());
    DestroyTrackingDelegateImpl::OnWindowDestroying();
  }

 private:
  DestroyTrackingDelegateImpl* parent_delegate_;

  DISALLOW_COPY_AND_ASSIGN(ChildWindowDelegateImpl);
};

// Used to verify that a Window is removed from its parent when
// OnWindowDestroyed is called.
class DestroyOrphanDelegate : public TestWindowDelegate {
 public:
  DestroyOrphanDelegate() : window_(NULL) {
  }

  void set_window(Window* window) { window_ = window; }

  virtual void OnWindowDestroyed() OVERRIDE {
    EXPECT_FALSE(window_->parent());
  }

 private:
  Window* window_;
  DISALLOW_COPY_AND_ASSIGN(DestroyOrphanDelegate);
};

// Used in verifying mouse capture.
class CaptureWindowDelegateImpl : public TestWindowDelegate {
 public:
  explicit CaptureWindowDelegateImpl()
      : capture_lost_count_(0),
        mouse_event_count_(0),
        touch_event_count_(0) {
  }

  int capture_lost_count() const { return capture_lost_count_; }
  void set_capture_lost_count(int value) { capture_lost_count_ = value; }
  int mouse_event_count() const { return mouse_event_count_; }
  void set_mouse_event_count(int value) { mouse_event_count_ = value; }
  int touch_event_count() const { return touch_event_count_; }
  void set_touch_event_count(int value) { touch_event_count_ = value; }

  virtual bool OnMouseEvent(MouseEvent* event) OVERRIDE {
    mouse_event_count_++;
    return false;
  }
  virtual ui::TouchStatus OnTouchEvent(TouchEvent* event) OVERRIDE {
    touch_event_count_++;
    return ui::TOUCH_STATUS_UNKNOWN;
  }
  virtual ui::GestureStatus OnGestureEvent(GestureEvent* event) OVERRIDE {
    return ui::GESTURE_STATUS_UNKNOWN;
  }
  virtual void OnCaptureLost() OVERRIDE {
    capture_lost_count_++;
  }

 private:
  int capture_lost_count_;
  int mouse_event_count_;
  int touch_event_count_;

  DISALLOW_COPY_AND_ASSIGN(CaptureWindowDelegateImpl);
};

// Keeps track of the location of the gesture.
class GestureTrackPositionDelegate : public TestWindowDelegate {
 public:
  GestureTrackPositionDelegate() {}

  virtual ui::GestureStatus OnGestureEvent(GestureEvent* event) OVERRIDE {
    position_ = event->location();
    return ui::GESTURE_STATUS_CONSUMED;
  }

  const gfx::Point& position() const { return position_; }

 private:
  gfx::Point position_;

  DISALLOW_COPY_AND_ASSIGN(GestureTrackPositionDelegate);
};

// Keeps track of mouse events.
class MouseTrackingDelegate : public TestWindowDelegate {
 public:
  MouseTrackingDelegate()
      : mouse_enter_count_(0),
        mouse_move_count_(0),
        mouse_leave_count_(0) {
  }

  virtual bool OnMouseEvent(MouseEvent* event) OVERRIDE {
    switch (event->type()) {
      case ui::ET_MOUSE_MOVED:
        mouse_move_count_++;
        break;
      case ui::ET_MOUSE_ENTERED:
        mouse_enter_count_++;
        break;
      case ui::ET_MOUSE_EXITED:
        mouse_leave_count_++;
        break;
      default:
        break;
    }
    return false;
  }

  std::string GetMouseCountsAndReset() {
    std::string result = StringPrintf("%d %d %d",
                                      mouse_enter_count_,
                                      mouse_move_count_,
                                      mouse_leave_count_);
    mouse_enter_count_ = 0;
    mouse_move_count_ = 0;
    mouse_leave_count_ = 0;
    return result;
  }

 private:
  int mouse_enter_count_;
  int mouse_move_count_;
  int mouse_leave_count_;

  DISALLOW_COPY_AND_ASSIGN(MouseTrackingDelegate);
};

}  // namespace

TEST_F(WindowTest, GetChildById) {
  scoped_ptr<Window> w1(CreateTestWindowWithId(1, NULL));
  scoped_ptr<Window> w11(CreateTestWindowWithId(11, w1.get()));
  scoped_ptr<Window> w111(CreateTestWindowWithId(111, w11.get()));
  scoped_ptr<Window> w12(CreateTestWindowWithId(12, w1.get()));

  EXPECT_EQ(NULL, w1->GetChildById(57));
  EXPECT_EQ(w12.get(), w1->GetChildById(12));
  EXPECT_EQ(w111.get(), w1->GetChildById(111));
}

// Make sure that Window::Contains correctly handles children, grandchildren,
// and not containing NULL or parents.
TEST_F(WindowTest, Contains) {
  Window parent(NULL);
  parent.Init(ui::Layer::LAYER_NOT_DRAWN);
  Window child1(NULL);
  child1.Init(ui::Layer::LAYER_NOT_DRAWN);
  Window child2(NULL);
  child2.Init(ui::Layer::LAYER_NOT_DRAWN);

  child1.SetParent(&parent);
  child2.SetParent(&child1);

  EXPECT_TRUE(parent.Contains(&parent));
  EXPECT_TRUE(parent.Contains(&child1));
  EXPECT_TRUE(parent.Contains(&child2));

  EXPECT_FALSE(parent.Contains(NULL));
  EXPECT_FALSE(child1.Contains(&parent));
  EXPECT_FALSE(child2.Contains(&child1));
}

TEST_F(WindowTest, ConvertPointToWindow) {
  // Window::ConvertPointToWindow is mostly identical to
  // Layer::ConvertPointToLayer, except NULL values for |source| are permitted,
  // in which case the function just returns.
  scoped_ptr<Window> w1(CreateTestWindowWithId(1, NULL));
  gfx::Point reference_point(100, 100);
  gfx::Point test_point = reference_point;
  Window::ConvertPointToWindow(NULL, w1.get(), &test_point);
  EXPECT_EQ(reference_point, test_point);
}

TEST_F(WindowTest, HitTest) {
  Window w1(new ColorTestWindowDelegate(SK_ColorWHITE));
  w1.set_id(1);
  w1.Init(ui::Layer::LAYER_TEXTURED);
  w1.SetBounds(gfx::Rect(10, 10, 50, 50));
  w1.Show();
  w1.SetParent(NULL);

  // Points are in the Window's coordinates.
  EXPECT_TRUE(w1.HitTest(gfx::Point(1, 1)));
  EXPECT_FALSE(w1.HitTest(gfx::Point(-1, -1)));

  // TODO(beng): clip Window to parent.
}

TEST_F(WindowTest, GetEventHandlerForPoint) {
  scoped_ptr<Window> w1(
      CreateTestWindow(SK_ColorWHITE, 1, gfx::Rect(10, 10, 500, 500), NULL));
  scoped_ptr<Window> w11(
      CreateTestWindow(SK_ColorGREEN, 11, gfx::Rect(5, 5, 100, 100), w1.get()));
  scoped_ptr<Window> w111(
      CreateTestWindow(SK_ColorCYAN, 111, gfx::Rect(5, 5, 75, 75), w11.get()));
  scoped_ptr<Window> w1111(
      CreateTestWindow(SK_ColorRED, 1111, gfx::Rect(5, 5, 50, 50), w111.get()));
  scoped_ptr<Window> w12(
      CreateTestWindow(SK_ColorMAGENTA, 12, gfx::Rect(10, 420, 25, 25),
                       w1.get()));
  scoped_ptr<Window> w121(
      CreateTestWindow(SK_ColorYELLOW, 121, gfx::Rect(5, 5, 5, 5), w12.get()));
  scoped_ptr<Window> w13(
      CreateTestWindow(SK_ColorGRAY, 13, gfx::Rect(5, 470, 50, 50), w1.get()));

  Window* root = RootWindow::GetInstance();
  w1->parent()->SetBounds(gfx::Rect(500, 500));
  EXPECT_EQ(NULL, root->GetEventHandlerForPoint(gfx::Point(5, 5)));
  EXPECT_EQ(w1.get(), root->GetEventHandlerForPoint(gfx::Point(11, 11)));
  EXPECT_EQ(w11.get(), root->GetEventHandlerForPoint(gfx::Point(16, 16)));
  EXPECT_EQ(w111.get(), root->GetEventHandlerForPoint(gfx::Point(21, 21)));
  EXPECT_EQ(w1111.get(), root->GetEventHandlerForPoint(gfx::Point(26, 26)));
  EXPECT_EQ(w12.get(), root->GetEventHandlerForPoint(gfx::Point(21, 431)));
  EXPECT_EQ(w121.get(), root->GetEventHandlerForPoint(gfx::Point(26, 436)));
  EXPECT_EQ(w13.get(), root->GetEventHandlerForPoint(gfx::Point(26, 481)));
}

TEST_F(WindowTest, GetTopWindowContainingPoint) {
  Window* root = RootWindow::GetInstance();
  root->SetBounds(gfx::Rect(0, 0, 300, 300));

  scoped_ptr<Window> w1(
      CreateTestWindow(SK_ColorWHITE, 1, gfx::Rect(10, 10, 100, 100), NULL));
  scoped_ptr<Window> w11(
      CreateTestWindow(SK_ColorGREEN, 11, gfx::Rect(0, 0, 120, 120), w1.get()));

  scoped_ptr<Window> w2(
      CreateTestWindow(SK_ColorRED, 2, gfx::Rect(5, 5, 55, 55), NULL));

  scoped_ptr<Window> w3(
      CreateTestWindowWithDelegate(
          NULL, 3, gfx::Rect(200, 200, 100, 100), NULL));
  scoped_ptr<Window> w31(
      CreateTestWindow(SK_ColorCYAN, 31, gfx::Rect(0, 0, 50, 50), w3.get()));
  scoped_ptr<Window> w311(
      CreateTestWindow(SK_ColorBLUE, 311, gfx::Rect(0, 0, 10, 10), w31.get()));

  // The stop-event-propagation flag shouldn't have any effect on the behavior
  // of this method.
  w3->set_stops_event_propagation(true);

  EXPECT_EQ(NULL, root->GetTopWindowContainingPoint(gfx::Point(0, 0)));
  EXPECT_EQ(w2.get(), root->GetTopWindowContainingPoint(gfx::Point(5, 5)));
  EXPECT_EQ(w2.get(), root->GetTopWindowContainingPoint(gfx::Point(10, 10)));
  EXPECT_EQ(w2.get(), root->GetTopWindowContainingPoint(gfx::Point(59, 59)));
  EXPECT_EQ(w1.get(), root->GetTopWindowContainingPoint(gfx::Point(60, 60)));
  EXPECT_EQ(w1.get(), root->GetTopWindowContainingPoint(gfx::Point(109, 109)));
  EXPECT_EQ(NULL, root->GetTopWindowContainingPoint(gfx::Point(110, 110)));
  EXPECT_EQ(w31.get(), root->GetTopWindowContainingPoint(gfx::Point(200, 200)));
  EXPECT_EQ(w31.get(), root->GetTopWindowContainingPoint(gfx::Point(220, 220)));
  EXPECT_EQ(NULL, root->GetTopWindowContainingPoint(gfx::Point(260, 260)));
}

TEST_F(WindowTest, GetToplevelWindow) {
  const gfx::Rect kBounds(0, 0, 10, 10);
  TestWindowDelegate delegate;

  Window* root = aura::RootWindow::GetInstance();
  scoped_ptr<Window> w1(CreateTestWindowWithId(1, root));
  scoped_ptr<Window> w11(
      CreateTestWindowWithDelegate(&delegate, 11, kBounds, w1.get()));
  scoped_ptr<Window> w111(CreateTestWindowWithId(111, w11.get()));
  scoped_ptr<Window> w1111(
      CreateTestWindowWithDelegate(&delegate, 1111, kBounds, w111.get()));

  EXPECT_TRUE(root->GetToplevelWindow() == NULL);
  EXPECT_TRUE(w1->GetToplevelWindow() == NULL);
  EXPECT_EQ(w11.get(), w11->GetToplevelWindow());
  EXPECT_EQ(w11.get(), w111->GetToplevelWindow());
  EXPECT_EQ(w11.get(), w1111->GetToplevelWindow());
}

// Various destruction assertions.
TEST_F(WindowTest, DestroyTest) {
  DestroyTrackingDelegateImpl parent_delegate;
  ChildWindowDelegateImpl child_delegate(&parent_delegate);
  {
    scoped_ptr<Window> parent(
        CreateTestWindowWithDelegate(&parent_delegate, 0, gfx::Rect(), NULL));
    CreateTestWindowWithDelegate(&child_delegate, 0, gfx::Rect(), parent.get());
  }
  // Both the parent and child should have been destroyed.
  EXPECT_EQ(1, parent_delegate.destroying_count());
  EXPECT_EQ(1, parent_delegate.destroyed_count());
  EXPECT_EQ(1, child_delegate.destroying_count());
  EXPECT_EQ(1, child_delegate.destroyed_count());
}

// Tests that a window is orphaned before OnWindowDestroyed is called.
TEST_F(WindowTest, OrphanedBeforeOnDestroyed) {
  TestWindowDelegate parent_delegate;
  DestroyOrphanDelegate child_delegate;
  {
    scoped_ptr<Window> parent(
        CreateTestWindowWithDelegate(&parent_delegate, 0, gfx::Rect(), NULL));
    scoped_ptr<Window> child(CreateTestWindowWithDelegate(&child_delegate, 0,
          gfx::Rect(), parent.get()));
    child_delegate.set_window(child.get());
  }
}

// Make sure StackChildAtTop moves both the window and layer to the front.
TEST_F(WindowTest, StackChildAtTop) {
  Window parent(NULL);
  parent.Init(ui::Layer::LAYER_NOT_DRAWN);
  Window child1(NULL);
  child1.Init(ui::Layer::LAYER_NOT_DRAWN);
  Window child2(NULL);
  child2.Init(ui::Layer::LAYER_NOT_DRAWN);

  child1.SetParent(&parent);
  child2.SetParent(&parent);
  ASSERT_EQ(2u, parent.children().size());
  EXPECT_EQ(&child1, parent.children()[0]);
  EXPECT_EQ(&child2, parent.children()[1]);
  ASSERT_EQ(2u, parent.layer()->children().size());
  EXPECT_EQ(child1.layer(), parent.layer()->children()[0]);
  EXPECT_EQ(child2.layer(), parent.layer()->children()[1]);

  parent.StackChildAtTop(&child1);
  ASSERT_EQ(2u, parent.children().size());
  EXPECT_EQ(&child1, parent.children()[1]);
  EXPECT_EQ(&child2, parent.children()[0]);
  ASSERT_EQ(2u, parent.layer()->children().size());
  EXPECT_EQ(child1.layer(), parent.layer()->children()[1]);
  EXPECT_EQ(child2.layer(), parent.layer()->children()[0]);
}

// Various assertions for StackChildAbove.
TEST_F(WindowTest, StackChildAbove) {
  Window parent(NULL);
  parent.Init(ui::Layer::LAYER_NOT_DRAWN);
  Window child1(NULL);
  child1.Init(ui::Layer::LAYER_NOT_DRAWN);
  Window child2(NULL);
  child2.Init(ui::Layer::LAYER_NOT_DRAWN);
  Window child3(NULL);
  child3.Init(ui::Layer::LAYER_NOT_DRAWN);

  child1.SetParent(&parent);
  child2.SetParent(&parent);

  // Move 1 in front of 2.
  parent.StackChildAbove(&child1, &child2);
  ASSERT_EQ(2u, parent.children().size());
  EXPECT_EQ(&child2, parent.children()[0]);
  EXPECT_EQ(&child1, parent.children()[1]);
  ASSERT_EQ(2u, parent.layer()->children().size());
  EXPECT_EQ(child2.layer(), parent.layer()->children()[0]);
  EXPECT_EQ(child1.layer(), parent.layer()->children()[1]);

  // Add 3, resulting in order [2, 1, 3], then move 2 in front of 1, resulting
  // in [1, 2, 3].
  child3.SetParent(&parent);
  parent.StackChildAbove(&child2, &child1);
  ASSERT_EQ(3u, parent.children().size());
  EXPECT_EQ(&child1, parent.children()[0]);
  EXPECT_EQ(&child2, parent.children()[1]);
  EXPECT_EQ(&child3, parent.children()[2]);
  ASSERT_EQ(3u, parent.layer()->children().size());
  EXPECT_EQ(child1.layer(), parent.layer()->children()[0]);
  EXPECT_EQ(child2.layer(), parent.layer()->children()[1]);
  EXPECT_EQ(child3.layer(), parent.layer()->children()[2]);

  // Move 1 in front of 3, resulting in [2, 3, 1].
  parent.StackChildAbove(&child1, &child3);
  ASSERT_EQ(3u, parent.children().size());
  EXPECT_EQ(&child2, parent.children()[0]);
  EXPECT_EQ(&child3, parent.children()[1]);
  EXPECT_EQ(&child1, parent.children()[2]);
  ASSERT_EQ(3u, parent.layer()->children().size());
  EXPECT_EQ(child2.layer(), parent.layer()->children()[0]);
  EXPECT_EQ(child3.layer(), parent.layer()->children()[1]);
  EXPECT_EQ(child1.layer(), parent.layer()->children()[2]);

  // Moving 1 in front of 2 should lower it, resulting in [2, 1, 3].
  parent.StackChildAbove(&child1, &child2);
  ASSERT_EQ(3u, parent.children().size());
  EXPECT_EQ(&child2, parent.children()[0]);
  EXPECT_EQ(&child1, parent.children()[1]);
  EXPECT_EQ(&child3, parent.children()[2]);
  ASSERT_EQ(3u, parent.layer()->children().size());
  EXPECT_EQ(child2.layer(), parent.layer()->children()[0]);
  EXPECT_EQ(child1.layer(), parent.layer()->children()[1]);
  EXPECT_EQ(child3.layer(), parent.layer()->children()[2]);
}

// Various capture assertions.
TEST_F(WindowTest, CaptureTests) {
  aura::RootWindow* root_window = aura::RootWindow::GetInstance();
  CaptureWindowDelegateImpl delegate;
  scoped_ptr<Window> window(CreateTestWindowWithDelegate(
      &delegate, 0, gfx::Rect(0, 0, 20, 20), NULL));
  EXPECT_FALSE(window->HasCapture());

  // Do a capture.
  window->SetCapture();
  EXPECT_TRUE(window->HasCapture());
  EXPECT_EQ(0, delegate.capture_lost_count());
  EventGenerator generator(gfx::Point(50, 50));
  generator.PressLeftButton();
  EXPECT_EQ(1, delegate.mouse_event_count());
  generator.ReleaseLeftButton();

  EXPECT_EQ(2, delegate.mouse_event_count());
  delegate.set_mouse_event_count(0);

  TouchEvent touchev(ui::ET_TOUCH_PRESSED, gfx::Point(50, 50), 0);
  root_window->DispatchTouchEvent(&touchev);
  EXPECT_EQ(1, delegate.touch_event_count());
  delegate.set_touch_event_count(0);

  window->ReleaseCapture();
  EXPECT_FALSE(window->HasCapture());
  EXPECT_EQ(1, delegate.capture_lost_count());

  generator.PressLeftButton();
  EXPECT_EQ(0, delegate.mouse_event_count());

  root_window->DispatchTouchEvent(&touchev);
  EXPECT_EQ(0, delegate.touch_event_count());

  // Removing the capture window from parent should reset the capture window
  // in the root window.
  window->SetCapture();
  EXPECT_EQ(window.get(), root_window->capture_window());
  window->parent()->RemoveChild(window.get());
  EXPECT_FALSE(window->HasCapture());
  EXPECT_EQ(NULL, root_window->capture_window());
}

// Changes capture while capture is already ongoing.
TEST_F(WindowTest, ChangeCaptureWhileMouseDown) {
  CaptureWindowDelegateImpl delegate;
  scoped_ptr<Window> window(CreateTestWindowWithDelegate(
      &delegate, 0, gfx::Rect(0, 0, 20, 20), NULL));
  CaptureWindowDelegateImpl delegate2;
  scoped_ptr<Window> w2(CreateTestWindowWithDelegate(
      &delegate2, 0, gfx::Rect(20, 20, 20, 20), NULL));
  EXPECT_FALSE(window->HasCapture());

  // Do a capture.
  window->SetCapture();
  delegate.set_mouse_event_count(0);
  EXPECT_TRUE(window->HasCapture());
  EXPECT_EQ(0, delegate.capture_lost_count());
  EventGenerator generator(gfx::Point(50, 50));
  generator.PressLeftButton();
  EXPECT_EQ(1, delegate.mouse_event_count());
  delegate.set_mouse_event_count(0);

  // Reset the capture.
  window->ReleaseCapture();
  w2->SetCapture();
  delegate2.set_mouse_event_count(0);
  generator.MoveMouseTo(gfx::Point(40, 40), 2);
  EXPECT_EQ(0, delegate.mouse_event_count());
  EXPECT_EQ(2, delegate2.mouse_event_count());
}

// Verifies capture is reset when a window is destroyed.
TEST_F(WindowTest, ReleaseCaptureOnDestroy) {
  RootWindow* root_window = RootWindow::GetInstance();
  CaptureWindowDelegateImpl delegate;
  scoped_ptr<Window> window(CreateTestWindowWithDelegate(
      &delegate, 0, gfx::Rect(0, 0, 20, 20), NULL));
  EXPECT_FALSE(window->HasCapture());

  // Do a capture.
  window->SetCapture();
  EXPECT_TRUE(window->HasCapture());

  // Destroy the window.
  window.reset();

  // Make sure the root window doesn't reference the window anymore.
  EXPECT_EQ(NULL, root_window->mouse_pressed_handler());
  EXPECT_EQ(NULL, root_window->capture_window());
}

TEST_F(WindowTest, GetScreenBounds) {
  scoped_ptr<Window> viewport(CreateTestWindowWithBounds(
      gfx::Rect(0, 0, 300, 300), NULL));
  scoped_ptr<Window> child(CreateTestWindowWithBounds(
      gfx::Rect(0, 0, 100, 100), viewport.get()));
  // Sanity check.
  EXPECT_EQ("0,0 100x100", child->GetScreenBounds().ToString());

  // The |child| window's screen bounds should move along with the |viewport|.
  viewport->SetBounds(gfx::Rect(-100, -100, 300, 300));
  EXPECT_EQ("-100,-100 100x100", child->GetScreenBounds().ToString());

  // The |child| window is moved to the 0,0 in screen coordinates.
  // |GetScreenBounds()| should return 0,0.
  child->SetBounds(gfx::Rect(100, 100, 100, 100));
  EXPECT_EQ("0,0 100x100", child->GetScreenBounds().ToString());
}

class MouseEnterExitWindowDelegate : public TestWindowDelegate {
 public:
  MouseEnterExitWindowDelegate() : entered_(false), exited_(false) {}

  virtual bool OnMouseEvent(MouseEvent* event) OVERRIDE {
    switch (event->type()) {
      case ui::ET_MOUSE_ENTERED:
        entered_ = true;
        break;
      case ui::ET_MOUSE_EXITED:
        exited_ = true;
        break;
      default:
        break;
    }
    return false;
  }

  bool entered() const { return entered_; }
  bool exited() const { return exited_; }

 private:
  bool entered_;
  bool exited_;

  DISALLOW_COPY_AND_ASSIGN(MouseEnterExitWindowDelegate);
};


// Verifies that the WindowDelegate receives MouseExit and MouseEnter events for
// mouse transitions from window to window.
TEST_F(WindowTest, MouseEnterExit) {
  MouseEnterExitWindowDelegate d1;
  scoped_ptr<Window> w1(
      CreateTestWindowWithDelegate(&d1, 1, gfx::Rect(10, 10, 50, 50), NULL));
  MouseEnterExitWindowDelegate d2;
  scoped_ptr<Window> w2(
      CreateTestWindowWithDelegate(&d2, 2, gfx::Rect(70, 70, 50, 50), NULL));

  test::EventGenerator generator;
  generator.MoveMouseToCenterOf(w1.get());
  EXPECT_TRUE(d1.entered());
  EXPECT_FALSE(d1.exited());
  EXPECT_FALSE(d2.entered());
  EXPECT_FALSE(d2.exited());

  generator.MoveMouseToCenterOf(w2.get());
  EXPECT_TRUE(d1.entered());
  EXPECT_TRUE(d1.exited());
  EXPECT_TRUE(d2.entered());
  EXPECT_FALSE(d2.exited());
}

// Creates a window with a delegate (w111) that can handle events at a lower
// z-index than a window without a delegate (w12). w12 is sized to fill the
// entire bounds of the container. This test verifies that
// GetEventHandlerForPoint() skips w12 even though its bounds contain the event,
// because it has no children that can handle the event and it has no delegate
// allowing it to handle the event itself.
TEST_F(WindowTest, GetEventHandlerForPoint_NoDelegate) {
  TestWindowDelegate d111;
  scoped_ptr<Window> w1(CreateTestWindowWithDelegate(NULL, 1,
      gfx::Rect(0, 0, 500, 500), NULL));
  scoped_ptr<Window> w11(CreateTestWindowWithDelegate(NULL, 11,
      gfx::Rect(0, 0, 500, 500), w1.get()));
  scoped_ptr<Window> w111(CreateTestWindowWithDelegate(&d111, 111,
      gfx::Rect(50, 50, 450, 450), w11.get()));
  scoped_ptr<Window> w12(CreateTestWindowWithDelegate(NULL, 12,
      gfx::Rect(0, 0, 500, 500), w1.get()));

  gfx::Point target_point = w111->bounds().CenterPoint();
  EXPECT_EQ(w111.get(), w1->GetEventHandlerForPoint(target_point));
}

class VisibilityWindowDelegate : public TestWindowDelegate {
 public:
  VisibilityWindowDelegate()
      : shown_(0),
        hidden_(0) {
  }

  int shown() const { return shown_; }
  int hidden() const { return hidden_; }
  void Clear() {
    shown_ = 0;
    hidden_ = 0;
  }

  virtual void OnWindowVisibilityChanged(bool visible) OVERRIDE {
    if (visible)
      shown_++;
    else
      hidden_++;
  }

 private:
  int shown_;
  int hidden_;

  DISALLOW_COPY_AND_ASSIGN(VisibilityWindowDelegate);
};

// Verifies show/hide propagate correctly to children and the layer.
TEST_F(WindowTest, Visibility) {
  VisibilityWindowDelegate d;
  scoped_ptr<Window> w1(CreateTestWindowWithDelegate(&d, 1, gfx::Rect(), NULL));
  scoped_ptr<Window> w2(CreateTestWindowWithId(2, w1.get()));
  scoped_ptr<Window> w3(CreateTestWindowWithId(3, w2.get()));

  // Create shows all the windows.
  EXPECT_TRUE(w1->IsVisible());
  EXPECT_TRUE(w2->IsVisible());
  EXPECT_TRUE(w3->IsVisible());
  EXPECT_EQ(1, d.shown());

  d.Clear();
  w1->Hide();
  EXPECT_FALSE(w1->IsVisible());
  EXPECT_FALSE(w2->IsVisible());
  EXPECT_FALSE(w3->IsVisible());
  EXPECT_EQ(1, d.hidden());
  EXPECT_EQ(0, d.shown());

  w2->Show();
  EXPECT_FALSE(w1->IsVisible());
  EXPECT_FALSE(w2->IsVisible());
  EXPECT_FALSE(w3->IsVisible());

  w3->Hide();
  EXPECT_FALSE(w1->IsVisible());
  EXPECT_FALSE(w2->IsVisible());
  EXPECT_FALSE(w3->IsVisible());

  d.Clear();
  w1->Show();
  EXPECT_TRUE(w1->IsVisible());
  EXPECT_TRUE(w2->IsVisible());
  EXPECT_FALSE(w3->IsVisible());
  EXPECT_EQ(0, d.hidden());
  EXPECT_EQ(1, d.shown());

  w3->Show();
  EXPECT_TRUE(w1->IsVisible());
  EXPECT_TRUE(w2->IsVisible());
  EXPECT_TRUE(w3->IsVisible());
}

// When set_consume_events() is called with |true| for a Window, that Window
// should make sure that none behind it in the z-order see events if it has
// children. If it does not have children, event targeting works as usual.
TEST_F(WindowTest, StopsEventPropagation) {
  TestWindowDelegate d11;
  TestWindowDelegate d111;
  TestWindowDelegate d121;
  scoped_ptr<Window> w1(CreateTestWindowWithDelegate(NULL, 1,
      gfx::Rect(0, 0, 500, 500), NULL));
  scoped_ptr<Window> w11(CreateTestWindowWithDelegate(&d11, 11,
      gfx::Rect(0, 0, 500, 500), w1.get()));
  scoped_ptr<Window> w111(CreateTestWindowWithDelegate(&d111, 111,
      gfx::Rect(50, 50, 450, 450), w11.get()));
  scoped_ptr<Window> w12(CreateTestWindowWithDelegate(NULL, 12,
      gfx::Rect(0, 0, 500, 500), w1.get()));
  scoped_ptr<Window> w121(CreateTestWindowWithDelegate(&d121, 121,
      gfx::Rect(150, 150, 50, 50), NULL));

  w12->set_stops_event_propagation(true);
  EXPECT_EQ(w11.get(), w1->GetEventHandlerForPoint(gfx::Point(10, 10)));

  EXPECT_TRUE(w111->CanFocus());
  EXPECT_TRUE(w111->CanReceiveEvents());
  w111->Focus();
  EXPECT_EQ(w111.get(), w1->GetFocusManager()->GetFocusedWindow());

  w12->AddChild(w121.get());

  EXPECT_EQ(NULL, w1->GetEventHandlerForPoint(gfx::Point(10, 10)));
  EXPECT_EQ(w121.get(), w1->GetEventHandlerForPoint(gfx::Point(175, 175)));

  // It should be possible to focus w121 since it is at or above the
  // consumes_events_ window.
  EXPECT_TRUE(w121->CanFocus());
  EXPECT_TRUE(w121->CanReceiveEvents());
  w121->Focus();
  EXPECT_EQ(w121.get(), w1->GetFocusManager()->GetFocusedWindow());

  // An attempt to focus 111 should be ignored and w121 should retain focus,
  // since a consumes_events_ window with a child is in the z-index above w111.
  EXPECT_FALSE(w111->CanReceiveEvents());
  w111->Focus();
  EXPECT_EQ(w121.get(), w1->GetFocusManager()->GetFocusedWindow());

  // Hiding w121 should make 111 focusable.
  w121->Hide();
  EXPECT_TRUE(w111->CanFocus());
  EXPECT_TRUE(w111->CanReceiveEvents());
  w111->Focus();
  EXPECT_EQ(w111.get(), w1->GetFocusManager()->GetFocusedWindow());
}

TEST_F(WindowTest, IgnoreEventsTest) {
  TestWindowDelegate d11;
  TestWindowDelegate d12;
  TestWindowDelegate d111;
  TestWindowDelegate d121;
  scoped_ptr<Window> w1(CreateTestWindowWithDelegate(NULL, 1,
      gfx::Rect(0, 0, 500, 500), NULL));
  scoped_ptr<Window> w11(CreateTestWindowWithDelegate(&d11, 11,
      gfx::Rect(0, 0, 500, 500), w1.get()));
  scoped_ptr<Window> w111(CreateTestWindowWithDelegate(&d111, 111,
      gfx::Rect(50, 50, 450, 450), w11.get()));
  scoped_ptr<Window> w12(CreateTestWindowWithDelegate(&d12, 12,
      gfx::Rect(0, 0, 500, 500), w1.get()));
  scoped_ptr<Window> w121(CreateTestWindowWithDelegate(&d121, 121,
      gfx::Rect(150, 150, 50, 50), w12.get()));

  EXPECT_EQ(w12.get(), w1->GetEventHandlerForPoint(gfx::Point(10, 10)));
  w12->set_ignore_events(true);
  EXPECT_EQ(w11.get(), w1->GetEventHandlerForPoint(gfx::Point(10, 10)));
  w12->set_ignore_events(false);

  EXPECT_EQ(w121.get(), w1->GetEventHandlerForPoint(gfx::Point(160, 160)));
  w121->set_ignore_events(true);
  EXPECT_EQ(w12.get(), w1->GetEventHandlerForPoint(gfx::Point(160, 160)));
  w12->set_ignore_events(true);
  EXPECT_EQ(w111.get(), w1->GetEventHandlerForPoint(gfx::Point(160, 160)));
  w111->set_ignore_events(true);
  EXPECT_EQ(w11.get(), w1->GetEventHandlerForPoint(gfx::Point(160, 160)));
}

// Tests transformation on the root window.
TEST_F(WindowTest, Transform) {
  RootWindow* root_window = RootWindow::GetInstance();
  gfx::Size size = root_window->GetHostSize();
  EXPECT_EQ(gfx::Rect(size),
            gfx::Screen::GetMonitorAreaNearestPoint(gfx::Point()));

  // Rotate it clock-wise 90 degrees.
  ui::Transform transform;
  transform.SetRotate(90.0f);
  transform.ConcatTranslate(size.width(), 0);
  root_window->SetTransform(transform);

  // The size should be the transformed size.
  gfx::Size transformed_size(size.height(), size.width());
  EXPECT_EQ(transformed_size.ToString(), root_window->GetHostSize().ToString());
  EXPECT_EQ(gfx::Rect(transformed_size).ToString(),
            root_window->bounds().ToString());
  EXPECT_EQ(gfx::Rect(transformed_size).ToString(),
            gfx::Screen::GetMonitorAreaNearestPoint(gfx::Point()).ToString());
}

// Tests that gesture events are transformed correctly.
// See http://crbug.com/111262
#if defined(OS_WIN)
#define MAYBE_TransformGesture FAILS_TransformGesture
#else
#define MAYBE_TransformGesture TransformGesture
#endif
TEST_F(WindowTest, MAYBE_TransformGesture) {
  RootWindow* root_window = RootWindow::GetInstance();
  gfx::Size size = root_window->GetHostSize();

  scoped_ptr<GestureTrackPositionDelegate> delegate(
      new GestureTrackPositionDelegate);
  scoped_ptr<Window> window(CreateTestWindowWithDelegate(delegate.get(), -1234,
      gfx::Rect(0, 0, 20, 20), NULL));

  // Rotate the root-window clock-wise 90 degrees.
  ui::Transform transform;
  transform.SetRotate(90.0f);
  transform.ConcatTranslate(size.width(), 0);
  root_window->SetTransform(transform);

  TouchEvent press(ui::ET_TOUCH_PRESSED,
      gfx::Point(size.height() - 10, 10), 0);
  root_window->DispatchTouchEvent(&press);
  EXPECT_EQ(gfx::Point(10, 10).ToString(), delegate->position().ToString());
}

// Various assertions for transient children.
TEST_F(WindowTest, TransientChildren) {
  scoped_ptr<Window> parent(CreateTestWindowWithId(0, NULL));
  scoped_ptr<Window> w1(CreateTestWindowWithId(1, parent.get()));
  scoped_ptr<Window> w3(CreateTestWindowWithId(3, parent.get()));
  Window* w2 = CreateTestWindowWithId(2, parent.get());
  w1->AddTransientChild(w2);  // w2 is now owned by w1.
  // Stack w1 at the top (end), this should force w2 to be last (on top of w1).
  parent->StackChildAtTop(w1.get());
  ASSERT_EQ(3u, parent->children().size());
  EXPECT_EQ(w2, parent->children().back());

  // Destroy w1, which should also destroy w3 (since it's a transient child).
  w1.reset();
  w2 = NULL;
  ASSERT_EQ(1u, parent->children().size());
  EXPECT_EQ(w3.get(), parent->children()[0]);

  w1.reset(CreateTestWindowWithId(4, parent.get()));
  w2 = CreateTestWindowWithId(5, w3.get());
  w1->AddTransientChild(w2);
  parent->StackChildAtTop(w3.get());
  // Stack w1 at the top (end), this shouldn't affect w2 since it has a
  // different parent.
  parent->StackChildAtTop(w1.get());
  ASSERT_EQ(2u, parent->children().size());
  EXPECT_EQ(w3.get(), parent->children()[0]);
  EXPECT_EQ(w1.get(), parent->children()[1]);
}

// Tests that when a focused window is closed, its parent inherits the focus.
TEST_F(WindowTest, FocusedWindowTest) {
  scoped_ptr<Window> parent(CreateTestWindowWithId(0, NULL));
  scoped_ptr<Window> child(CreateTestWindowWithId(1, parent.get()));

  parent->Show();

  child->Focus();
  EXPECT_TRUE(child->HasFocus());
  EXPECT_FALSE(parent->HasFocus());

  child.reset();
  EXPECT_TRUE(parent->HasFocus());
}

TEST_F(WindowTest, Property) {
  scoped_ptr<Window> w(CreateTestWindowWithId(0, NULL));
  const char* key = "test";
  EXPECT_EQ(NULL, w->GetProperty(key));
  EXPECT_EQ(0, w->GetIntProperty(key));

  w->SetIntProperty(key, 1);
  EXPECT_EQ(1, w->GetIntProperty(key));
  EXPECT_EQ(reinterpret_cast<void*>(static_cast<intptr_t>(1)),
            w->GetProperty(key));

  // Overwrite the property with different value type.
  w->SetProperty(key, static_cast<void*>(const_cast<char*>("string")));
  std::string expected("string");
  EXPECT_EQ(expected, static_cast<const char*>(w->GetProperty(key)));

  // Non-existent property.
  EXPECT_EQ(NULL, w->GetProperty("foo"));
  EXPECT_EQ(0, w->GetIntProperty("foo"));

  // Set NULL and make sure the property is gone.
  w->SetProperty(key, NULL);
  EXPECT_EQ(NULL, w->GetProperty(key));
  EXPECT_EQ(0, w->GetIntProperty(key));
}

TEST_F(WindowTest, SetBoundsInternalShouldCheckTargetBounds) {
  scoped_ptr<Window> w1(
      CreateTestWindowWithBounds(gfx::Rect(0, 0, 100, 100), NULL));

  EXPECT_FALSE(!w1->layer());
  w1->layer()->GetAnimator()->set_disable_timer_for_test(true);
  ui::AnimationContainerElement* element = w1->layer()->GetAnimator();

  EXPECT_EQ("0,0 100x100", w1->bounds().ToString());
  EXPECT_EQ("0,0 100x100", w1->layer()->GetTargetBounds().ToString());

  // Animate to a different position.
  {
    ui::ScopedLayerAnimationSettings settings(w1->layer()->GetAnimator());
    w1->SetBounds(gfx::Rect(100, 100, 100, 100));
  }

  EXPECT_EQ("0,0 100x100", w1->bounds().ToString());
  EXPECT_EQ("100,100 100x100", w1->layer()->GetTargetBounds().ToString());

  // Animate back to the first position. The animation hasn't started yet, so
  // the current bounds are still (0, 0, 100, 100), but the target bounds are
  // (100, 100, 100, 100). If we step the animator ahead, we should find that
  // we're at (0, 0, 100, 100). That is, the second animation should be applied.
  {
    ui::ScopedLayerAnimationSettings settings(w1->layer()->GetAnimator());
    w1->SetBounds(gfx::Rect(0, 0, 100, 100));
  }

  EXPECT_EQ("0,0 100x100", w1->bounds().ToString());
  EXPECT_EQ("0,0 100x100", w1->layer()->GetTargetBounds().ToString());

  // Confirm that the target bounds are reached.
  base::TimeTicks start_time =
      w1->layer()->GetAnimator()->get_last_step_time_for_test();

  element->Step(start_time + base::TimeDelta::FromMilliseconds(1000));

  EXPECT_EQ("0,0 100x100", w1->bounds().ToString());
}


class WindowObserverTest : public WindowTest,
                           public WindowObserver {
 public:
  struct VisibilityInfo {
    bool window_visible;
    bool visible_param;
  };

  WindowObserverTest()
      : added_count_(0),
        removed_count_(0),
        destroyed_count_(0),
        old_property_value_(0),
        new_property_value_(0) {
  }

  virtual ~WindowObserverTest() {}

  const VisibilityInfo* GetVisibilityInfo() const {
    return visibility_info_.get();
  }

  void ResetVisibilityInfo() {
    visibility_info_.reset();
  }

  // Returns a description of the WindowObserver methods that have been invoked.
  std::string WindowObserverCountStateAndClear() {
    std::string result(
        base::StringPrintf("added=%d removed=%d",
        added_count_, removed_count_));
    added_count_ = removed_count_ = 0;
    return result;
  }

  int DestroyedCountAndClear() {
    int result = destroyed_count_;
    destroyed_count_ = 0;
    return result;
  }

  // Return a string representation of the arguments passed in
  // OnWindowPropertyChanged callback.
  std::string PropertyChangeInfoAndClear() {
    std::string result(
        base::StringPrintf("name=%s old=%ld new=%ld",
                           property_name_.c_str(),
                           static_cast<long>(old_property_value_),
                           static_cast<long>(new_property_value_)));
    property_name_.clear();
    old_property_value_ = 0;
    new_property_value_ = 0;
    return result;
  }

 private:
  virtual void OnWindowAdded(Window* new_window) OVERRIDE {
    added_count_++;
  }

  virtual void OnWillRemoveWindow(Window* window) OVERRIDE {
    removed_count_++;
  }

  virtual void OnWindowVisibilityChanged(Window* window,
                                         bool visible) OVERRIDE {
    visibility_info_.reset(new VisibilityInfo);
    visibility_info_->window_visible = window->IsVisible();
    visibility_info_->visible_param = visible;
  }

  virtual void OnWindowDestroyed(Window* window) OVERRIDE {
    EXPECT_FALSE(window->parent());
    destroyed_count_++;
  }

  virtual void OnWindowPropertyChanged(Window* window,
                                       const char* name,
                                       void* old) OVERRIDE {
    property_name_ = std::string(name);
    old_property_value_ = reinterpret_cast<intptr_t>(old);
    new_property_value_ = reinterpret_cast<intptr_t>(window->GetProperty(name));
  }

  int added_count_;
  int removed_count_;
  int destroyed_count_;
  scoped_ptr<VisibilityInfo> visibility_info_;
  std::string property_name_;
  intptr_t old_property_value_;
  intptr_t new_property_value_;

  DISALLOW_COPY_AND_ASSIGN(WindowObserverTest);
};

// Various assertions for WindowObserver.
TEST_F(WindowObserverTest, WindowObserver) {
  scoped_ptr<Window> w1(CreateTestWindowWithId(1, NULL));
  w1->AddObserver(this);

  // Create a new window as a child of w1, our observer should be notified.
  scoped_ptr<Window> w2(CreateTestWindowWithId(2, w1.get()));
  EXPECT_EQ("added=1 removed=0", WindowObserverCountStateAndClear());

  // Delete w2, which should result in the remove notification.
  w2.reset();
  EXPECT_EQ("added=0 removed=1", WindowObserverCountStateAndClear());

  // Create a window that isn't parented to w1, we shouldn't get any
  // notification.
  scoped_ptr<Window> w3(CreateTestWindowWithId(3, NULL));
  EXPECT_EQ("added=0 removed=0", WindowObserverCountStateAndClear());

  // Similarly destroying w3 shouldn't notify us either.
  w3.reset();
  EXPECT_EQ("added=0 removed=0", WindowObserverCountStateAndClear());
  w1->RemoveObserver(this);
}

// Test if OnWindowVisibilityChagned is invoked with expected
// parameters.
TEST_F(WindowObserverTest, WindowVisibility) {
  scoped_ptr<Window> w1(CreateTestWindowWithId(1, NULL));
  scoped_ptr<Window> w2(CreateTestWindowWithId(1, w1.get()));
  w2->AddObserver(this);

  // Hide should make the window invisible and the passed visible
  // parameter is false.
  w2->Hide();
  EXPECT_FALSE(!GetVisibilityInfo());
  EXPECT_FALSE(!GetVisibilityInfo());
  if (!GetVisibilityInfo())
    return;
  EXPECT_FALSE(GetVisibilityInfo()->window_visible);
  EXPECT_FALSE(GetVisibilityInfo()->visible_param);

  // If parent isn't visible, showing window won't make the window visible, but
  // passed visible value must be true.
  w1->Hide();
  ResetVisibilityInfo();
  EXPECT_TRUE(!GetVisibilityInfo());
  w2->Show();
  EXPECT_FALSE(!GetVisibilityInfo());
  if (!GetVisibilityInfo())
    return;
  EXPECT_FALSE(GetVisibilityInfo()->window_visible);
  EXPECT_TRUE(GetVisibilityInfo()->visible_param);

  // If parent is visible, showing window will make the window
  // visible and the passed visible value is true.
  w1->Show();
  w2->Hide();
  ResetVisibilityInfo();
  w2->Show();
  EXPECT_FALSE(!GetVisibilityInfo());
  if (!GetVisibilityInfo())
    return;
  EXPECT_TRUE(GetVisibilityInfo()->window_visible);
  EXPECT_TRUE(GetVisibilityInfo()->visible_param);
}

// Test if OnWindowDestroyed is invoked as expected.
TEST_F(WindowObserverTest, WindowDestroyed) {
  // Delete a window should fire a destroyed notification.
  scoped_ptr<Window> w1(CreateTestWindowWithId(1, NULL));
  w1->AddObserver(this);
  w1.reset();
  EXPECT_EQ(1, DestroyedCountAndClear());

  // Observe on child and delete parent window should fire a notification.
  scoped_ptr<Window> parent(CreateTestWindowWithId(1, NULL));
  Window* child = CreateTestWindowWithId(1, parent.get());  // owned by parent
  child->AddObserver(this);
  parent.reset();
  EXPECT_EQ(1, DestroyedCountAndClear());
}

TEST_F(WindowObserverTest, PropertyChanged) {
  // Setting property should fire a property change notification.
  scoped_ptr<Window> w1(CreateTestWindowWithId(1, NULL));
  const char* key = "test";

  w1->AddObserver(this);
  w1->SetIntProperty(key, 1);
  EXPECT_EQ("name=test old=0 new=1", PropertyChangeInfoAndClear());
  w1->SetIntProperty(key, 2);
  EXPECT_EQ(2, w1->GetIntProperty(key));
  EXPECT_EQ(reinterpret_cast<void*>(2), w1->GetProperty(key));
  EXPECT_EQ("name=test old=1 new=2", PropertyChangeInfoAndClear());
  w1->SetProperty(key, NULL);
  EXPECT_EQ("name=test old=2 new=0", PropertyChangeInfoAndClear());

  // Sanity check to see if |PropertyChangeInfoAndClear| really clears.
  EXPECT_EQ("name= old=0 new=0", PropertyChangeInfoAndClear());
}

TEST_F(WindowTest, AcquireLayer) {
  scoped_ptr<Window> window1(CreateTestWindowWithId(1, NULL));
  scoped_ptr<Window> window2(CreateTestWindowWithId(2, NULL));
  ui::Layer* parent = window1->parent()->layer();
  EXPECT_EQ(2U, parent->children().size());

  Window::TestApi window1_test_api(window1.get());
  Window::TestApi window2_test_api(window2.get());

  EXPECT_TRUE(window1_test_api.OwnsLayer());
  EXPECT_TRUE(window2_test_api.OwnsLayer());

  // After acquisition, window1 should not own its layer, but it should still
  // be available to the window.
  scoped_ptr<ui::Layer> window1_layer(window1->AcquireLayer());
  EXPECT_FALSE(window1_test_api.OwnsLayer());
  EXPECT_TRUE(window1_layer.get() == window1->layer());

  // Upon destruction, window1's layer should still be valid, and in the layer
  // hierarchy, but window2's should be gone, and no longer in the hierarchy.
  window1.reset();
  window2.reset();

  // This should be set by the window's destructor.
  EXPECT_TRUE(window1_layer->delegate() == NULL);
  EXPECT_EQ(1U, parent->children().size());
}

TEST_F(WindowTest, DontRestackWindowsWhoseLayersHaveNoDelegate) {
  scoped_ptr<Window> window1(CreateTestWindowWithId(1, NULL));
  scoped_ptr<Window> window2(CreateTestWindowWithId(2, NULL));

  // This brings window1 (and its layer) to the front.
  RootWindow::GetInstance()->StackChildAbove(window1.get(), window2.get());
  EXPECT_EQ(RootWindow::GetInstance()->children().front(), window2.get());
  EXPECT_EQ(RootWindow::GetInstance()->children().back(), window1.get());
  EXPECT_EQ(RootWindow::GetInstance()->layer()->children().front(),
            window2->layer());
  EXPECT_EQ(RootWindow::GetInstance()->layer()->children().back(),
            window1->layer());

  // This brings window2 (but NOT its layer) to the front.
  window1->layer()->set_delegate(NULL);
  RootWindow::GetInstance()->StackChildAbove(window2.get(), window1.get());
  EXPECT_EQ(RootWindow::GetInstance()->children().front(), window1.get());
  EXPECT_EQ(RootWindow::GetInstance()->children().back(), window2.get());
  EXPECT_EQ(RootWindow::GetInstance()->layer()->children().front(),
            window2->layer());
  EXPECT_EQ(RootWindow::GetInstance()->layer()->children().back(),
            window1->layer());
}

class TestVisibilityClient : public client::VisibilityClient {
public:
  TestVisibilityClient() : ignore_visibility_changes_(false) {
    client::SetVisibilityClient(this);
  }
  virtual ~TestVisibilityClient() {
    client::SetVisibilityClient(NULL);
  }

  void set_ignore_visibility_changes(bool ignore_visibility_changes) {
    ignore_visibility_changes_ = ignore_visibility_changes;
  }

  // Overridden from client::VisibilityClient:
  virtual void UpdateLayerVisibility(aura::Window* window,
                                     bool visible) OVERRIDE {
    if (!ignore_visibility_changes_)
      window->layer()->SetVisible(visible);
  }

 private:
  bool ignore_visibility_changes_;
  DISALLOW_COPY_AND_ASSIGN(TestVisibilityClient);
};

TEST_F(WindowTest, VisibilityClientIsVisible) {
  TestVisibilityClient client;

  scoped_ptr<Window> window(CreateTestWindowWithId(1, NULL));
  EXPECT_TRUE(window->IsVisible());
  EXPECT_TRUE(window->layer()->visible());

  window->Hide();
  EXPECT_FALSE(window->IsVisible());
  EXPECT_FALSE(window->layer()->visible());
  window->Show();

  client.set_ignore_visibility_changes(true);
  window->Hide();
  EXPECT_FALSE(window->IsVisible());
  EXPECT_TRUE(window->layer()->visible());
}

// Tests mouse events on window change.
TEST_F(WindowTest, MouseEventsOnWindowChange) {
  RootWindow* root_window = RootWindow::GetInstance();
  gfx::Size size = root_window->GetHostSize();

  EventGenerator generator;
  generator.MoveMouseTo(50, 50);

  MouseTrackingDelegate d1;
  scoped_ptr<Window> w1(CreateTestWindowWithDelegate(&d1, 1,
      gfx::Rect(0, 0, 100, 100), root_window));
  RunAllPendingInMessageLoop();
  // The format of result is "Enter/Mouse/Leave".
  EXPECT_EQ("1 1 0", d1.GetMouseCountsAndReset());

  // Adding new window.
  MouseTrackingDelegate d11;
  scoped_ptr<Window> w11(CreateTestWindowWithDelegate(
      &d11, 1, gfx::Rect(0, 0, 100, 100), w1.get()));
  RunAllPendingInMessageLoop();
  EXPECT_EQ("0 0 1", d1.GetMouseCountsAndReset());
  EXPECT_EQ("1 1 0", d11.GetMouseCountsAndReset());

  // Move bounds.
  w11->SetBounds(gfx::Rect(0, 0, 10, 10));
  RunAllPendingInMessageLoop();
  EXPECT_EQ("1 1 0", d1.GetMouseCountsAndReset());
  EXPECT_EQ("0 0 1", d11.GetMouseCountsAndReset());

  w11->SetBounds(gfx::Rect(0, 0, 60, 60));
  RunAllPendingInMessageLoop();
  EXPECT_EQ("0 0 1", d1.GetMouseCountsAndReset());
  EXPECT_EQ("1 1 0", d11.GetMouseCountsAndReset());

  // Detach, then re-attach.
  w1->RemoveChild(w11.get());
  RunAllPendingInMessageLoop();
  EXPECT_EQ("1 1 0", d1.GetMouseCountsAndReset());
  // Window is detached, so no event is set.
  EXPECT_EQ("0 0 0", d11.GetMouseCountsAndReset());

  w1->AddChild(w11.get());
  RunAllPendingInMessageLoop();
  EXPECT_EQ("0 0 1", d1.GetMouseCountsAndReset());
  // Window is detached, so no event is set.
  EXPECT_EQ("1 1 0", d11.GetMouseCountsAndReset());

  // Visibility Change
  w11->Hide();
  RunAllPendingInMessageLoop();
  EXPECT_EQ("1 1 0", d1.GetMouseCountsAndReset());
  EXPECT_EQ("0 0 0", d11.GetMouseCountsAndReset());

  w11->Show();
  RunAllPendingInMessageLoop();
  EXPECT_EQ("0 0 1", d1.GetMouseCountsAndReset());
  EXPECT_EQ("1 1 0", d11.GetMouseCountsAndReset());

  // Transform: move d11 by 100 100.
  ui::Transform transform;
  transform.ConcatTranslate(100, 100);
  w11->SetTransform(transform);
  RunAllPendingInMessageLoop();
  EXPECT_EQ("1 1 0", d1.GetMouseCountsAndReset());
  EXPECT_EQ("0 0 1", d11.GetMouseCountsAndReset());

  w11->SetTransform(ui::Transform());
  RunAllPendingInMessageLoop();
  EXPECT_EQ("0 0 1", d1.GetMouseCountsAndReset());
  EXPECT_EQ("1 1 0", d11.GetMouseCountsAndReset());

  // Closing a window.
  w11.reset();
  RunAllPendingInMessageLoop();
  EXPECT_EQ("1 1 0", d1.GetMouseCountsAndReset());
}

class StackingMadrigalLayoutManager : public LayoutManager {
 public:
  StackingMadrigalLayoutManager() {
    RootWindow::GetInstance()->SetLayoutManager(this);
  }
  virtual ~StackingMadrigalLayoutManager() {
  }

 private:
  // Overridden from LayoutManager:
  virtual void OnWindowResized() OVERRIDE {}
  virtual void OnWindowAddedToLayout(Window* child) OVERRIDE {}
  virtual void OnWillRemoveWindowFromLayout(Window* child) OVERRIDE {}
  virtual void OnChildWindowVisibilityChanged(Window* child,
                                              bool visible) OVERRIDE {
    Window::Windows::const_iterator it =
        RootWindow::GetInstance()->children().begin();
    Window* last_window = NULL;
    for (; it != RootWindow::GetInstance()->children().end(); ++it) {
      if (*it == child && last_window) {
        if (!visible)
          RootWindow::GetInstance()->StackChildAbove(last_window, *it);
        else
          RootWindow::GetInstance()->StackChildAbove(*it, last_window);
        break;
      }
      last_window = *it;
    }
  }
  virtual void SetChildBounds(Window* child,
                              const gfx::Rect& requested_bounds) OVERRIDE {
    SetChildBoundsDirect(child, requested_bounds);
  }

  DISALLOW_COPY_AND_ASSIGN(StackingMadrigalLayoutManager);
};

class StackingMadrigalVisibilityClient : public client::VisibilityClient {
 public:
  StackingMadrigalVisibilityClient() : ignored_window_(NULL) {
    client::SetVisibilityClient(this);
  }
  virtual ~StackingMadrigalVisibilityClient() {
    client::SetVisibilityClient(NULL);
  }

  void set_ignored_window(Window* ignored_window) {
    ignored_window_ = ignored_window;
  }

 private:
  // Overridden from client::VisibilityClient:
  virtual void UpdateLayerVisibility(Window* window, bool visible) OVERRIDE {
    if (!visible) {
      if (window == ignored_window_)
        window->layer()->set_delegate(NULL);
      else
        window->layer()->SetVisible(visible);
    } else {
      window->layer()->SetVisible(visible);
    }
  }

  Window* ignored_window_;

  DISALLOW_COPY_AND_ASSIGN(StackingMadrigalVisibilityClient);
};

// This test attempts to reconstruct a circumstance that can happen when the
// aura client attempts to manipulate the visibility and delegate of a layer
// independent of window visibility.
// A use case is where the client attempts to keep a window's visible onscreen
// even after code has called Hide() on the window. The use case for this would
// be that window hides are animated (e.g. the window fades out). To prevent
// spurious updating the client code may also clear window's layer's delegate,
// so that the window cannot attempt to paint or update it further. The window
// uses the presence of a NULL layer delegate as a signal in stacking to note
// that the window is being manipulated by such a use case and its stacking
// should not be adjusted.
// One issue that can arise when a window opens two transient children, and the
// first is hidden. Subsequent attempts to activate the transient parent can
// result in the transient parent being stacked above the second transient
// child. A fix is made to Window::StackAbove to prevent this, and this test
// verifies this fix.
TEST_F(WindowTest, StackingMadrigal) {
  new StackingMadrigalLayoutManager;
  StackingMadrigalVisibilityClient visibility_client;

  scoped_ptr<Window> window1(CreateTestWindowWithId(1, NULL));
  scoped_ptr<Window> window11(CreateTransientChild(11, window1.get()));

  visibility_client.set_ignored_window(window11.get());

  window11->Show();
  window11->Hide();

  // As a transient, window11 should still be stacked above window1, even when
  // hidden.
  EXPECT_TRUE(WindowIsAbove(window11.get(), window1.get()));
  EXPECT_TRUE(LayerIsAbove(window11.get(), window1.get()));

  scoped_ptr<Window> window12(CreateTransientChild(12, window1.get()));
  window12->Show();

  EXPECT_TRUE(WindowIsAbove(window12.get(), window11.get()));
  EXPECT_TRUE(LayerIsAbove(window12.get(), window11.get()));

  // Prior to the NULL check in the transient restacking loop in
  // Window::StackChildAbove() introduced with this change, attempting to stack
  // window1 above window12 at this point would actually restack the layers
  // resulting in window12's layer being below window1's layer (though the
  // windows themselves would still be correctly stacked, so events would pass
  // through.)
  RootWindow::GetInstance()->StackChildAbove(window1.get(), window12.get());

  // Both window12 and its layer should be stacked above window1.
  EXPECT_TRUE(WindowIsAbove(window12.get(), window1.get()));
  EXPECT_TRUE(LayerIsAbove(window12.get(), window1.get()));
}

}  // namespace test
}  // namespace aura
