// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>

#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/keycodes/keyboard_codes.h"
#include "ui/base/models/simple_menu_model.h"
#include "ui/gfx/canvas_skia.h"
#include "ui/gfx/path.h"
#include "ui/gfx/transform.h"
#include "views/background.h"
#include "views/controls/button/button_dropdown.h"
#include "views/controls/button/checkbox.h"
#include "views/controls/native/native_view_host.h"
#include "views/controls/scroll_view.h"
#include "views/controls/textfield/textfield.h"
#include "views/events/event.h"
#include "views/focus/accelerator_handler.h"
#include "views/focus/view_storage.h"
#include "views/test/views_test_base.h"
#include "views/view.h"
#include "views/views_delegate.h"
#include "views/widget/native_widget.h"
#include "views/widget/root_view.h"
#include "views/window/dialog_delegate.h"
#include "views/window/window.h"

#if defined(OS_WIN)
#include "views/controls/button/native_button_win.h"
#include "views/test/test_views_delegate.h"
#endif
#if defined(TOUCH_UI)
#include "views/touchui/gesture_manager.h"
#endif

using ::testing::_;

namespace views {

class ViewTest : public ViewsTestBase {
 public:
  ViewTest() {
  }

  virtual ~ViewTest() {
  }
};

////////////////////////////////////////////////////////////////////////////////
//
// A view subclass for testing purpose
//
////////////////////////////////////////////////////////////////////////////////
class TestView : public View {
 public:
  TestView() : View() {
  }

  virtual ~TestView() {}

  // Reset all test state
  void Reset() {
    did_change_bounds_ = false;
    child_added_ = false;
    child_removed_ = false;
    last_mouse_event_type_ = 0;
    location_.SetPoint(0, 0);
#if defined(TOUCH_UI)
    last_touch_event_type_ = 0;
    last_touch_event_was_handled_ = false;
#endif
    last_clip_.setEmpty();
    accelerator_count_map_.clear();
  }

  virtual void OnBoundsChanged(const gfx::Rect& previous_bounds) OVERRIDE;
  virtual void ViewHierarchyChanged(
      bool is_add, View *parent, View *child) OVERRIDE;
  virtual bool OnMousePressed(const MouseEvent& event) OVERRIDE;
  virtual bool OnMouseDragged(const MouseEvent& event) OVERRIDE;
  virtual void OnMouseReleased(const MouseEvent& event) OVERRIDE;
#if defined(TOUCH_UI)
  virtual TouchStatus OnTouchEvent(const TouchEvent& event);
#endif
  virtual void Paint(gfx::Canvas* canvas) OVERRIDE;
  virtual void SchedulePaintInRect(const gfx::Rect& rect) OVERRIDE;
  virtual bool AcceleratorPressed(const Accelerator& accelerator) OVERRIDE;

  // OnBoundsChanged test
  bool did_change_bounds_;
  gfx::Rect new_bounds_;

  // AddRemoveNotifications test
  bool child_added_;
  bool child_removed_;
  View* parent_;
  View* child_;

  // MouseEvent
  int last_mouse_event_type_;
  gfx::Point location_;

  // Painting
  std::vector<gfx::Rect> scheduled_paint_rects_;

  #if defined(TOUCH_UI)
  // TouchEvent
  int last_touch_event_type_;
  bool last_touch_event_was_handled_;
  bool in_touch_sequence_;
#endif

  // Painting
  SkRect last_clip_;

  // Accelerators
  std::map<Accelerator, int> accelerator_count_map_;
};

#if defined(TOUCH_UI)
// Mock instance of the GestureManager for testing.
class MockGestureManager : public GestureManager {
 public:
  // Reset all test state
  void Reset() {
    last_touch_event_ = 0;
    last_view_ = NULL;
    previously_handled_flag_ = false;
    dispatched_synthetic_event_ = false;
  }

  bool ProcessTouchEventForGesture(const TouchEvent& event,
                                   View* source,
                                   View::TouchStatus status);
  MockGestureManager();

  bool previously_handled_flag_;
  int last_touch_event_;
  View *last_view_;
  bool dispatched_synthetic_event_;

  DISALLOW_COPY_AND_ASSIGN(MockGestureManager);
};

// A view subclass that ignores all touch events for testing purposes.
class TestViewIgnoreTouch : public TestView {
 public:
  TestViewIgnoreTouch() : TestView() {
  }

  virtual ~TestViewIgnoreTouch() {}
 private:
  virtual TouchStatus OnTouchEvent(const TouchEvent& event);
};
#endif

////////////////////////////////////////////////////////////////////////////////
// OnBoundsChanged
////////////////////////////////////////////////////////////////////////////////

void TestView::OnBoundsChanged(const gfx::Rect& previous_bounds) {
  did_change_bounds_ = true;
  new_bounds_ = bounds();
}

TEST_F(ViewTest, OnBoundsChanged) {
  TestView* v = new TestView();

  gfx::Rect prev_rect(0, 0, 200, 200);
  gfx::Rect new_rect(100, 100, 250, 250);

  v->SetBoundsRect(prev_rect);
  v->Reset();

  v->SetBoundsRect(new_rect);
  EXPECT_EQ(v->did_change_bounds_, true);
  EXPECT_EQ(v->new_bounds_, new_rect);

  EXPECT_EQ(v->bounds(), gfx::Rect(new_rect));
  delete v;
}

////////////////////////////////////////////////////////////////////////////////
// AddRemoveNotifications
////////////////////////////////////////////////////////////////////////////////

void TestView::ViewHierarchyChanged(bool is_add, View *parent, View *child) {
  if (is_add) {
    child_added_ = true;
  } else {
    child_removed_ = true;
  }
  parent_ = parent;
  child_ = child;
}

TEST_F(ViewTest, AddRemoveNotifications) {
  TestView* v1 = new TestView();
  v1->SetBounds(0, 0, 300, 300);

  TestView* v2 = new TestView();
  v2->SetBounds(0, 0, 300, 300);

  TestView* v3 = new TestView();
  v3->SetBounds(0, 0, 300, 300);

  // Add a child. Make sure both v2 and v3 receive the right
  // notification
  v2->Reset();
  v3->Reset();
  v2->AddChildView(v3);
  EXPECT_EQ(v2->child_added_, true);
  EXPECT_EQ(v2->parent_, v2);
  EXPECT_EQ(v2->child_, v3);

  EXPECT_EQ(v3->child_added_, true);
  EXPECT_EQ(v3->parent_, v2);
  EXPECT_EQ(v3->child_, v3);

  // Add v2 and transitively v3 to v1. Make sure that all views
  // received the right notification

  v1->Reset();
  v2->Reset();
  v3->Reset();
  v1->AddChildView(v2);

  EXPECT_EQ(v1->child_added_, true);
  EXPECT_EQ(v1->child_, v2);
  EXPECT_EQ(v1->parent_, v1);

  EXPECT_EQ(v2->child_added_, true);
  EXPECT_EQ(v2->child_, v2);
  EXPECT_EQ(v2->parent_, v1);

  EXPECT_EQ(v3->child_added_, true);
  EXPECT_EQ(v3->child_, v2);
  EXPECT_EQ(v3->parent_, v1);

  // Remove v2. Make sure all views received the right notification
  v1->Reset();
  v2->Reset();
  v3->Reset();
  v1->RemoveChildView(v2);

  EXPECT_EQ(v1->child_removed_, true);
  EXPECT_EQ(v1->parent_, v1);
  EXPECT_EQ(v1->child_, v2);

  EXPECT_EQ(v2->child_removed_, true);
  EXPECT_EQ(v2->parent_, v1);
  EXPECT_EQ(v2->child_, v2);

  EXPECT_EQ(v3->child_removed_, true);
  EXPECT_EQ(v3->parent_, v1);
  EXPECT_EQ(v3->child_, v3);

  // Clean-up
  delete v1;
  delete v2;  // This also deletes v3 (child of v2).
}

////////////////////////////////////////////////////////////////////////////////
// MouseEvent
////////////////////////////////////////////////////////////////////////////////

bool TestView::OnMousePressed(const MouseEvent& event) {
  last_mouse_event_type_ = event.type();
  location_.SetPoint(event.x(), event.y());
  return true;
}

bool TestView::OnMouseDragged(const MouseEvent& event) {
  last_mouse_event_type_ = event.type();
  location_.SetPoint(event.x(), event.y());
  return true;
}

void TestView::OnMouseReleased(const MouseEvent& event) {
  last_mouse_event_type_ = event.type();
  location_.SetPoint(event.x(), event.y());
}

TEST_F(ViewTest, MouseEvent) {
  TestView* v1 = new TestView();
  v1->SetBounds(0, 0, 300, 300);

  TestView* v2 = new TestView();
  v2->SetBounds(100, 100, 100, 100);

  scoped_ptr<Widget> widget(new Widget);
  Widget::InitParams params(Widget::InitParams::TYPE_WINDOW);
  params.ownership = views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  params.bounds = gfx::Rect(50, 50, 650, 650);
  widget->Init(params);
  View* root = widget->GetRootView();

  root->AddChildView(v1);
  v1->AddChildView(v2);

  v1->Reset();
  v2->Reset();

  MouseEvent pressed(ui::ET_MOUSE_PRESSED,
                     110,
                     120,
                     ui::EF_LEFT_BUTTON_DOWN);
  root->OnMousePressed(pressed);
  EXPECT_EQ(v2->last_mouse_event_type_, ui::ET_MOUSE_PRESSED);
  EXPECT_EQ(v2->location_.x(), 10);
  EXPECT_EQ(v2->location_.y(), 20);
  // Make sure v1 did not receive the event
  EXPECT_EQ(v1->last_mouse_event_type_, 0);

  // Drag event out of bounds. Should still go to v2
  v1->Reset();
  v2->Reset();
  MouseEvent dragged(ui::ET_MOUSE_DRAGGED,
                     50,
                     40,
                     ui::EF_LEFT_BUTTON_DOWN);
  root->OnMouseDragged(dragged);
  EXPECT_EQ(v2->last_mouse_event_type_, ui::ET_MOUSE_DRAGGED);
  EXPECT_EQ(v2->location_.x(), -50);
  EXPECT_EQ(v2->location_.y(), -60);
  // Make sure v1 did not receive the event
  EXPECT_EQ(v1->last_mouse_event_type_, 0);

  // Releasted event out of bounds. Should still go to v2
  v1->Reset();
  v2->Reset();
  MouseEvent released(ui::ET_MOUSE_RELEASED, 0, 0, 0);
  root->OnMouseDragged(released);
  EXPECT_EQ(v2->last_mouse_event_type_, ui::ET_MOUSE_RELEASED);
  EXPECT_EQ(v2->location_.x(), -100);
  EXPECT_EQ(v2->location_.y(), -100);
  // Make sure v1 did not receive the event
  EXPECT_EQ(v1->last_mouse_event_type_, 0);

  widget->CloseNow();
}

#if defined(TOUCH_UI)
////////////////////////////////////////////////////////////////////////////////
// TouchEvent
////////////////////////////////////////////////////////////////////////////////
bool MockGestureManager::ProcessTouchEventForGesture(
    const TouchEvent& event,
    View* source,
    View::TouchStatus status) {
  if (status != View::TOUCH_STATUS_UNKNOWN) {
    dispatched_synthetic_event_ = false;
    return false;
  }
  last_touch_event_ =  event.type();
  last_view_ = source;
  previously_handled_flag_ = status != View::TOUCH_STATUS_UNKNOWN;
  dispatched_synthetic_event_ = true;
  return true;
}

MockGestureManager::MockGestureManager() {
}

View::TouchStatus TestView::OnTouchEvent(const TouchEvent& event) {
  last_touch_event_type_ = event.type();
  location_.SetPoint(event.x(), event.y());
  if (!in_touch_sequence_) {
    if (event.type() == ui::ET_TOUCH_PRESSED) {
      in_touch_sequence_ = true;
      return TOUCH_STATUS_START;
    }
  } else {
    if (event.type() == ui::ET_TOUCH_RELEASED) {
      in_touch_sequence_ = false;
      return TOUCH_STATUS_END;
    }
    return TOUCH_STATUS_CONTINUE;
  }
  return last_touch_event_was_handled_ ? TOUCH_STATUS_CONTINUE :
                                         TOUCH_STATUS_UNKNOWN;
}

View::TouchStatus TestViewIgnoreTouch::OnTouchEvent(const TouchEvent& event) {
  return TOUCH_STATUS_UNKNOWN;
}

TEST_F(ViewTest, TouchEvent) {
  MockGestureManager* gm = new MockGestureManager();

  TestView* v1 = new TestView();
  v1->SetBounds(0, 0, 300, 300);

  TestView* v2 = new TestView();
  v2->SetBounds(100, 100, 100, 100);

  TestView* v3 = new TestViewIgnoreTouch();
  v3->SetBounds(0, 0, 100, 100);

  scoped_ptr<Widget> widget(new Widget());
  Widget::InitParams params(Widget::InitParams::TYPE_WINDOW);
  params.ownership = views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  params.bounds = gfx::Rect(50, 50, 650, 650);
  widget->Init(params);
  View* root = widget->GetRootView();

  root->AddChildView(v1);
  static_cast<internal::RootView*>(root)->SetGestureManager(gm);
  v1->AddChildView(v2);
  v2->AddChildView(v3);

  // |v3| completely obscures |v2|, but all the touch events on |v3| should
  // reach |v2| because |v3| doesn't process any touch events.

  // Make sure if none of the views handle the touch event, the gesture manager
  // does.
  v1->Reset();
  v2->Reset();
  gm->Reset();

  TouchEvent unhandled(ui::ET_TOUCH_MOVED,
                       400,
                       400,
                       0, /* no flags */
                       0, /* first finger touch */
                       1.0, 0.0, 1.0);
  root->OnTouchEvent(unhandled);

  EXPECT_EQ(v1->last_touch_event_type_, 0);
  EXPECT_EQ(v2->last_touch_event_type_, 0);

  EXPECT_EQ(gm->previously_handled_flag_, false);
  EXPECT_EQ(gm->last_touch_event_, ui::ET_TOUCH_MOVED);
  EXPECT_EQ(gm->last_view_, root);
  EXPECT_EQ(gm->dispatched_synthetic_event_, true);

  // Test press, drag, release touch sequence.
  v1->Reset();
  v2->Reset();
  gm->Reset();

  TouchEvent pressed(ui::ET_TOUCH_PRESSED,
                     110,
                     120,
                     0, /* no flags */
                     0, /* first finger touch */
                     1.0, 0.0, 1.0);
  v2->last_touch_event_was_handled_ = true;
  root->OnTouchEvent(pressed);

  EXPECT_EQ(v2->last_touch_event_type_, ui::ET_TOUCH_PRESSED);
  EXPECT_EQ(v2->location_.x(), 10);
  EXPECT_EQ(v2->location_.y(), 20);
  // Make sure v1 did not receive the event
  EXPECT_EQ(v1->last_touch_event_type_, 0);

  // Since v2 handled the touch-event, the gesture manager should not handle it.
  EXPECT_EQ(gm->last_touch_event_, 0);
  EXPECT_EQ(NULL, gm->last_view_);
  EXPECT_EQ(gm->previously_handled_flag_, false);

  // Drag event out of bounds. Should still go to v2
  v1->Reset();
  v2->Reset();
  TouchEvent dragged(ui::ET_TOUCH_MOVED,
                     50,
                     40,
                     0, /* no flags */
                     0, /* first finger touch */
                     1.0, 0.0, 1.0);
  root->OnTouchEvent(dragged);
  EXPECT_EQ(v2->last_touch_event_type_, ui::ET_TOUCH_MOVED);
  EXPECT_EQ(v2->location_.x(), -50);
  EXPECT_EQ(v2->location_.y(), -60);
  // Make sure v1 did not receive the event
  EXPECT_EQ(v1->last_touch_event_type_, 0);

  EXPECT_EQ(gm->last_touch_event_, 0);
  EXPECT_EQ(NULL, gm->last_view_);
  EXPECT_EQ(gm->previously_handled_flag_, false);

  // Released event out of bounds. Should still go to v2
  v1->Reset();
  v2->Reset();
  TouchEvent released(ui::ET_TOUCH_RELEASED, 0, 0, 0, 0 /* first finger */,
                      1.0, 0.0, 1.0);
  v2->last_touch_event_was_handled_ = true;
  root->OnTouchEvent(released);
  EXPECT_EQ(v2->last_touch_event_type_, ui::ET_TOUCH_RELEASED);
  EXPECT_EQ(v2->location_.x(), -100);
  EXPECT_EQ(v2->location_.y(), -100);
  // Make sure v1 did not receive the event
  EXPECT_EQ(v1->last_touch_event_type_, 0);

  EXPECT_EQ(gm->last_touch_event_, 0);
  EXPECT_EQ(NULL, gm->last_view_);
  EXPECT_EQ(gm->previously_handled_flag_, false);

  widget->CloseNow();
}
#endif

////////////////////////////////////////////////////////////////////////////////
// Painting
////////////////////////////////////////////////////////////////////////////////

void TestView::Paint(gfx::Canvas* canvas) {
  canvas->AsCanvasSkia()->getClipBounds(&last_clip_);
}

void TestView::SchedulePaintInRect(const gfx::Rect& rect) {
  scheduled_paint_rects_.push_back(rect);
  View::SchedulePaintInRect(rect);
}

void CheckRect(const SkRect& check_rect, const SkRect& target_rect) {
  EXPECT_EQ(target_rect.fLeft, check_rect.fLeft);
  EXPECT_EQ(target_rect.fRight, check_rect.fRight);
  EXPECT_EQ(target_rect.fTop, check_rect.fTop);
  EXPECT_EQ(target_rect.fBottom, check_rect.fBottom);
}

/* This test is disabled because it is flakey on some systems.
TEST_F(ViewTest, DISABLED_Painting) {
  // Determine if InvalidateRect generates an empty paint rectangle.
  EmptyWindow paint_window(CRect(50, 50, 650, 650));
  paint_window.RedrawWindow(CRect(0, 0, 600, 600), NULL,
                            RDW_UPDATENOW | RDW_INVALIDATE | RDW_ALLCHILDREN);
  bool empty_paint = paint_window.empty_paint();

  NativeWidgetWin window;
  window.set_delete_on_destroy(false);
  window.set_window_style(WS_OVERLAPPEDWINDOW);
  window.Init(NULL, gfx::Rect(50, 50, 650, 650), NULL);
  View* root = window.GetRootView();

  TestView* v1 = new TestView();
  v1->SetBounds(0, 0, 650, 650);
  root->AddChildView(v1);

  TestView* v2 = new TestView();
  v2->SetBounds(10, 10, 80, 80);
  v1->AddChildView(v2);

  TestView* v3 = new TestView();
  v3->SetBounds(10, 10, 60, 60);
  v2->AddChildView(v3);

  TestView* v4 = new TestView();
  v4->SetBounds(10, 200, 100, 100);
  v1->AddChildView(v4);

  // Make sure to paint current rects
  PaintRootView(root, empty_paint);


  v1->Reset();
  v2->Reset();
  v3->Reset();
  v4->Reset();
  v3->SchedulePaintInRect(gfx::Rect(10, 10, 10, 10));
  PaintRootView(root, empty_paint);

  SkRect tmp_rect;

  tmp_rect.set(SkIntToScalar(10),
               SkIntToScalar(10),
               SkIntToScalar(20),
               SkIntToScalar(20));
  CheckRect(v3->last_clip_, tmp_rect);

  tmp_rect.set(SkIntToScalar(20),
               SkIntToScalar(20),
               SkIntToScalar(30),
               SkIntToScalar(30));
  CheckRect(v2->last_clip_, tmp_rect);

  tmp_rect.set(SkIntToScalar(30),
               SkIntToScalar(30),
               SkIntToScalar(40),
               SkIntToScalar(40));
  CheckRect(v1->last_clip_, tmp_rect);

  // Make sure v4 was not painted
  tmp_rect.setEmpty();
  CheckRect(v4->last_clip_, tmp_rect);

  window.DestroyWindow();
}
*/

#if defined(OS_WIN)
TEST_F(ViewTest, RemoveNotification) {
#elif defined(TOOLKIT_USES_GTK)
// TODO(beng): stopped working with widget hierarchy split,
//             http://crbug.com/82364
TEST_F(ViewTest, DISABLED_RemoveNotification) {
#endif
  ViewStorage* vs = ViewStorage::GetInstance();
  Widget* widget = new Widget;
  widget->Init(Widget::InitParams(Widget::InitParams::TYPE_WINDOW));
  View* root_view = widget->GetRootView();

  View* v1 = new View;
  int s1 = vs->CreateStorageID();
  vs->StoreView(s1, v1);
  root_view->AddChildView(v1);
  View* v11 = new View;
  int s11 = vs->CreateStorageID();
  vs->StoreView(s11, v11);
  v1->AddChildView(v11);
  View* v111 = new View;
  int s111 = vs->CreateStorageID();
  vs->StoreView(s111, v111);
  v11->AddChildView(v111);
  View* v112 = new View;
  int s112 = vs->CreateStorageID();
  vs->StoreView(s112, v112);
  v11->AddChildView(v112);
  View* v113 = new View;
  int s113 = vs->CreateStorageID();
  vs->StoreView(s113, v113);
  v11->AddChildView(v113);
  View* v1131 = new View;
  int s1131 = vs->CreateStorageID();
  vs->StoreView(s1131, v1131);
  v113->AddChildView(v1131);
  View* v12 = new View;
  int s12 = vs->CreateStorageID();
  vs->StoreView(s12, v12);
  v1->AddChildView(v12);

  View* v2 = new View;
  int s2 = vs->CreateStorageID();
  vs->StoreView(s2, v2);
  root_view->AddChildView(v2);
  View* v21 = new View;
  int s21 = vs->CreateStorageID();
  vs->StoreView(s21, v21);
  v2->AddChildView(v21);
  View* v211 = new View;
  int s211 = vs->CreateStorageID();
  vs->StoreView(s211, v211);
  v21->AddChildView(v211);

  size_t stored_views = vs->view_count();

  // Try removing a leaf view.
  v21->RemoveChildView(v211);
  EXPECT_EQ(stored_views - 1, vs->view_count());
  EXPECT_EQ(NULL, vs->RetrieveView(s211));
  delete v211;  // We won't use this one anymore.

  // Now try removing a view with a hierarchy of depth 1.
  v11->RemoveChildView(v113);
  EXPECT_EQ(stored_views - 3, vs->view_count());
  EXPECT_EQ(NULL, vs->RetrieveView(s113));
  EXPECT_EQ(NULL, vs->RetrieveView(s1131));
  delete v113;  // We won't use this one anymore.

  // Now remove even more.
  root_view->RemoveChildView(v1);
  EXPECT_EQ(NULL, vs->RetrieveView(s1));
  EXPECT_EQ(NULL, vs->RetrieveView(s11));
  EXPECT_EQ(NULL, vs->RetrieveView(s12));
  EXPECT_EQ(NULL, vs->RetrieveView(s111));
  EXPECT_EQ(NULL, vs->RetrieveView(s112));

  // Put v1 back for more tests.
  root_view->AddChildView(v1);
  vs->StoreView(s1, v1);

  // Synchronously closing the window deletes the view hierarchy, which should
  // remove all its views from ViewStorage.
  widget->CloseNow();
  EXPECT_EQ(stored_views - 10, vs->view_count());
  EXPECT_EQ(NULL, vs->RetrieveView(s1));
  EXPECT_EQ(NULL, vs->RetrieveView(s12));
  EXPECT_EQ(NULL, vs->RetrieveView(s11));
  EXPECT_EQ(NULL, vs->RetrieveView(s12));
  EXPECT_EQ(NULL, vs->RetrieveView(s21));
  EXPECT_EQ(NULL, vs->RetrieveView(s111));
  EXPECT_EQ(NULL, vs->RetrieveView(s112));
}

namespace {
class HitTestView : public View {
 public:
  explicit HitTestView(bool has_hittest_mask)
      : has_hittest_mask_(has_hittest_mask) {
  }
  virtual ~HitTestView() {}

 protected:
  // Overridden from View:
  virtual bool HasHitTestMask() const {
    return has_hittest_mask_;
  }
  virtual void GetHitTestMask(gfx::Path* mask) const {
    DCHECK(has_hittest_mask_);
    DCHECK(mask);

    SkScalar w = SkIntToScalar(width());
    SkScalar h = SkIntToScalar(height());

    // Create a triangular mask within the bounds of this View.
    mask->moveTo(w / 2, 0);
    mask->lineTo(w, h);
    mask->lineTo(0, h);
    mask->close();
  }

 private:
  bool has_hittest_mask_;

  DISALLOW_COPY_AND_ASSIGN(HitTestView);
};

gfx::Point ConvertPointToView(View* view, const gfx::Point& p) {
  gfx::Point tmp(p);
  View::ConvertPointToView(view->GetWidget()->GetRootView(), view, &tmp);
  return tmp;
}
}

TEST_F(ViewTest, HitTestMasks) {
  Widget* widget = new Widget;
  widget->Init(Widget::InitParams(Widget::InitParams::TYPE_WINDOW));
  View* root_view = widget->GetRootView();
  root_view->SetBounds(0, 0, 500, 500);

  gfx::Rect v1_bounds = gfx::Rect(0, 0, 100, 100);
  HitTestView* v1 = new HitTestView(false);
  v1->SetBoundsRect(v1_bounds);
  root_view->AddChildView(v1);

  gfx::Rect v2_bounds = gfx::Rect(105, 0, 100, 100);
  HitTestView* v2 = new HitTestView(true);
  v2->SetBoundsRect(v2_bounds);
  root_view->AddChildView(v2);

  gfx::Point v1_centerpoint = v1_bounds.CenterPoint();
  gfx::Point v2_centerpoint = v2_bounds.CenterPoint();
  gfx::Point v1_origin = v1_bounds.origin();
  gfx::Point v2_origin = v2_bounds.origin();

  // Test HitTest
  EXPECT_TRUE(v1->HitTest(ConvertPointToView(v1, v1_centerpoint)));
  EXPECT_TRUE(v2->HitTest(ConvertPointToView(v2, v2_centerpoint)));

  EXPECT_TRUE(v1->HitTest(ConvertPointToView(v1, v1_origin)));
  EXPECT_FALSE(v2->HitTest(ConvertPointToView(v2, v2_origin)));

  // Test GetEventHandlerForPoint
  EXPECT_EQ(v1, root_view->GetEventHandlerForPoint(v1_centerpoint));
  EXPECT_EQ(v2, root_view->GetEventHandlerForPoint(v2_centerpoint));
  EXPECT_EQ(v1, root_view->GetEventHandlerForPoint(v1_origin));
  EXPECT_EQ(root_view, root_view->GetEventHandlerForPoint(v2_origin));

  widget->CloseNow();
}

TEST_F(ViewTest, Textfield) {
  const string16 kText = ASCIIToUTF16("Reality is that which, when you stop "
                                      "believing it, doesn't go away.");
  const string16 kExtraText = ASCIIToUTF16("Pretty deep, Philip!");
  const string16 kEmptyString;

  ui::Clipboard clipboard;

  Widget* widget = new Widget;
  Widget::InitParams params(Widget::InitParams::TYPE_WINDOW);
  params.bounds = gfx::Rect(0, 0, 100, 100);
  widget->Init(params);
  View* root_view = widget->GetRootView();

  Textfield* textfield = new Textfield();
  root_view->AddChildView(textfield);

  // Test setting, appending text.
  textfield->SetText(kText);
  EXPECT_EQ(kText, textfield->text());
  textfield->AppendText(kExtraText);
  EXPECT_EQ(kText + kExtraText, textfield->text());
  textfield->SetText(string16());
  EXPECT_EQ(kEmptyString, textfield->text());

  // Test selection related methods.
  textfield->SetText(kText);
  EXPECT_EQ(kEmptyString, textfield->GetSelectedText());
  textfield->SelectAll();
  EXPECT_EQ(kText, textfield->text());
  textfield->ClearSelection();
  EXPECT_EQ(kEmptyString, textfield->GetSelectedText());

  widget->CloseNow();
}

#if defined(OS_WIN)

// Tests that the Textfield view respond appropiately to cut/copy/paste.
TEST_F(ViewTest, TextfieldCutCopyPaste) {
  const std::wstring kNormalText = L"Normal";
  const std::wstring kReadOnlyText = L"Read only";
  const std::wstring kPasswordText = L"Password! ** Secret stuff **";

  ui::Clipboard clipboard;

  Widget* widget = new Widget;
  Widget::InitParams params(Widget::InitParams::TYPE_WINDOW);
  params.bounds = gfx::Rect(0, 0, 100, 100);
  widget->Init(params);
  View* root_view = widget->GetRootView();

  Textfield* normal = new Textfield();
  Textfield* read_only = new Textfield();
  read_only->SetReadOnly(true);
  Textfield* password = new Textfield(Textfield::STYLE_PASSWORD);

  root_view->AddChildView(normal);
  root_view->AddChildView(read_only);
  root_view->AddChildView(password);

  normal->SetText(kNormalText);
  read_only->SetText(kReadOnlyText);
  password->SetText(kPasswordText);

  //
  // Test cut.
  //
  ASSERT_TRUE(normal->GetTestingHandle());
  normal->SelectAll();
  ::SendMessage(normal->GetTestingHandle(), WM_CUT, 0, 0);

  string16 result;
  clipboard.ReadText(ui::Clipboard::BUFFER_STANDARD, &result);
  EXPECT_EQ(kNormalText, result);
  normal->SetText(kNormalText);  // Let's revert to the original content.

  ASSERT_TRUE(read_only->GetTestingHandle());
  read_only->SelectAll();
  ::SendMessage(read_only->GetTestingHandle(), WM_CUT, 0, 0);
  result.clear();
  clipboard.ReadText(ui::Clipboard::BUFFER_STANDARD, &result);
  // Cut should have failed, so the clipboard content should not have changed.
  EXPECT_EQ(kNormalText, result);

  ASSERT_TRUE(password->GetTestingHandle());
  password->SelectAll();
  ::SendMessage(password->GetTestingHandle(), WM_CUT, 0, 0);
  result.clear();
  clipboard.ReadText(ui::Clipboard::BUFFER_STANDARD, &result);
  // Cut should have failed, so the clipboard content should not have changed.
  EXPECT_EQ(kNormalText, result);

  //
  // Test copy.
  //

  // Let's start with read_only as the clipboard already contains the content
  // of normal.
  read_only->SelectAll();
  ::SendMessage(read_only->GetTestingHandle(), WM_COPY, 0, 0);
  result.clear();
  clipboard.ReadText(ui::Clipboard::BUFFER_STANDARD, &result);
  EXPECT_EQ(kReadOnlyText, result);

  normal->SelectAll();
  ::SendMessage(normal->GetTestingHandle(), WM_COPY, 0, 0);
  result.clear();
  clipboard.ReadText(ui::Clipboard::BUFFER_STANDARD, &result);
  EXPECT_EQ(kNormalText, result);

  password->SelectAll();
  ::SendMessage(password->GetTestingHandle(), WM_COPY, 0, 0);
  result.clear();
  clipboard.ReadText(ui::Clipboard::BUFFER_STANDARD, &result);
  // We don't let you copy from a password field, clipboard should not have
  // changed.
  EXPECT_EQ(kNormalText, result);

  //
  // Test Paste.
  //
  // Note that we use GetWindowText instead of Textfield::GetText below as the
  // text in the Textfield class is synced to the text of the HWND on
  // WM_KEYDOWN messages that we are not simulating here.

  // Attempting to copy kNormalText in a read-only text-field should fail.
  read_only->SelectAll();
  ::SendMessage(read_only->GetTestingHandle(), WM_KEYDOWN, 0, 0);
  wchar_t buffer[1024] = { 0 };
  ::GetWindowText(read_only->GetTestingHandle(), buffer, 1024);
  EXPECT_EQ(kReadOnlyText, std::wstring(buffer));

  password->SelectAll();
  ::SendMessage(password->GetTestingHandle(), WM_PASTE, 0, 0);
  ::GetWindowText(password->GetTestingHandle(), buffer, 1024);
  EXPECT_EQ(kNormalText, std::wstring(buffer));

  // Copy from read_only so the string we are pasting is not the same as the
  // current one.
  read_only->SelectAll();
  ::SendMessage(read_only->GetTestingHandle(), WM_COPY, 0, 0);
  normal->SelectAll();
  ::SendMessage(normal->GetTestingHandle(), WM_PASTE, 0, 0);
  ::GetWindowText(normal->GetTestingHandle(), buffer, 1024);
  EXPECT_EQ(kReadOnlyText, std::wstring(buffer));
  widget->CloseNow();
}
#endif

////////////////////////////////////////////////////////////////////////////////
// Accelerators
////////////////////////////////////////////////////////////////////////////////
bool TestView::AcceleratorPressed(const Accelerator& accelerator) {
  accelerator_count_map_[accelerator]++;
  return true;
}

#if defined(OS_WIN)
TEST_F(ViewTest, ActivateAccelerator) {
  // Register a keyboard accelerator before the view is added to a window.
  Accelerator return_accelerator(ui::VKEY_RETURN, false, false, false);
  TestView* view = new TestView();
  view->Reset();
  view->AddAccelerator(return_accelerator);
  EXPECT_EQ(view->accelerator_count_map_[return_accelerator], 0);

  // Create a window and add the view as its child.
  scoped_ptr<Widget> widget(new Widget);
  Widget::InitParams params(Widget::InitParams::TYPE_WINDOW);
  params.ownership = views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  params.bounds = gfx::Rect(0, 0, 100, 100);
  widget->Init(params);
  View* root = widget->GetRootView();
  root->AddChildView(view);

  // Get the focus manager.
  FocusManager* focus_manager = FocusManager::GetFocusManagerForNativeView(
      widget->GetNativeView());
  ASSERT_TRUE(focus_manager);

  // Hit the return key and see if it takes effect.
  EXPECT_TRUE(focus_manager->ProcessAccelerator(return_accelerator));
  EXPECT_EQ(view->accelerator_count_map_[return_accelerator], 1);

  // Hit the escape key. Nothing should happen.
  Accelerator escape_accelerator(ui::VKEY_ESCAPE, false, false, false);
  EXPECT_FALSE(focus_manager->ProcessAccelerator(escape_accelerator));
  EXPECT_EQ(view->accelerator_count_map_[return_accelerator], 1);
  EXPECT_EQ(view->accelerator_count_map_[escape_accelerator], 0);

  // Now register the escape key and hit it again.
  view->AddAccelerator(escape_accelerator);
  EXPECT_TRUE(focus_manager->ProcessAccelerator(escape_accelerator));
  EXPECT_EQ(view->accelerator_count_map_[return_accelerator], 1);
  EXPECT_EQ(view->accelerator_count_map_[escape_accelerator], 1);

  // Remove the return key accelerator.
  view->RemoveAccelerator(return_accelerator);
  EXPECT_FALSE(focus_manager->ProcessAccelerator(return_accelerator));
  EXPECT_EQ(view->accelerator_count_map_[return_accelerator], 1);
  EXPECT_EQ(view->accelerator_count_map_[escape_accelerator], 1);

  // Add it again. Hit the return key and the escape key.
  view->AddAccelerator(return_accelerator);
  EXPECT_TRUE(focus_manager->ProcessAccelerator(return_accelerator));
  EXPECT_EQ(view->accelerator_count_map_[return_accelerator], 2);
  EXPECT_EQ(view->accelerator_count_map_[escape_accelerator], 1);
  EXPECT_TRUE(focus_manager->ProcessAccelerator(escape_accelerator));
  EXPECT_EQ(view->accelerator_count_map_[return_accelerator], 2);
  EXPECT_EQ(view->accelerator_count_map_[escape_accelerator], 2);

  // Remove all the accelerators.
  view->ResetAccelerators();
  EXPECT_FALSE(focus_manager->ProcessAccelerator(return_accelerator));
  EXPECT_EQ(view->accelerator_count_map_[return_accelerator], 2);
  EXPECT_EQ(view->accelerator_count_map_[escape_accelerator], 2);
  EXPECT_FALSE(focus_manager->ProcessAccelerator(escape_accelerator));
  EXPECT_EQ(view->accelerator_count_map_[return_accelerator], 2);
  EXPECT_EQ(view->accelerator_count_map_[escape_accelerator], 2);

  widget->CloseNow();
}
#endif

#if defined(OS_WIN)
TEST_F(ViewTest, HiddenViewWithAccelerator) {
  Accelerator return_accelerator(ui::VKEY_RETURN, false, false, false);
  TestView* view = new TestView();
  view->Reset();
  view->AddAccelerator(return_accelerator);
  EXPECT_EQ(view->accelerator_count_map_[return_accelerator], 0);

  scoped_ptr<Widget> widget(new Widget);
  Widget::InitParams params(Widget::InitParams::TYPE_WINDOW);
  params.ownership = views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  params.bounds = gfx::Rect(0, 0, 100, 100);
  widget->Init(params);
  View* root = widget->GetRootView();
  root->AddChildView(view);

  FocusManager* focus_manager = FocusManager::GetFocusManagerForNativeView(
      widget->GetNativeView());
  ASSERT_TRUE(focus_manager);

  view->SetVisible(false);
  EXPECT_EQ(NULL,
            focus_manager->GetCurrentTargetForAccelerator(return_accelerator));

  view->SetVisible(true);
  EXPECT_EQ(view,
            focus_manager->GetCurrentTargetForAccelerator(return_accelerator));

  widget->CloseNow();
}
#endif

#if defined(OS_WIN)
////////////////////////////////////////////////////////////////////////////////
// Mouse-wheel message rerouting
////////////////////////////////////////////////////////////////////////////////
class ButtonTest : public NativeButtonBase {
 public:
  ButtonTest(ButtonListener* listener, const std::wstring& label)
      : NativeButtonBase(listener, label) {
  }

  HWND GetHWND() {
    return static_cast<NativeButtonWin*>(native_wrapper_)->native_view();
  }
};

class CheckboxTest : public Checkbox {
 public:
  explicit CheckboxTest(const std::wstring& label) : Checkbox(label) {
  }

  HWND GetHWND() {
    return static_cast<NativeCheckboxWin*>(native_wrapper_)->native_view();
  }
};

class ScrollableTestView : public View {
 public:
  ScrollableTestView() { }

  virtual gfx::Size GetPreferredSize() {
    return gfx::Size(100, 10000);
  }

  virtual void Layout() {
    SizeToPreferredSize();
  }
};

class TestViewWithControls : public View {
 public:
  TestViewWithControls() {
    button_ = new ButtonTest(NULL, L"Button");
    checkbox_ = new CheckboxTest(L"My checkbox");
    text_field_ = new Textfield();
    AddChildView(button_);
    AddChildView(checkbox_);
    AddChildView(text_field_);
  }

  ButtonTest* button_;
  CheckboxTest* checkbox_;
  Textfield* text_field_;
};

class SimpleWindowDelegate : public WindowDelegate {
 public:
  explicit SimpleWindowDelegate(View* contents) : contents_(contents) {  }

  virtual void DeleteDelegate() { delete this; }

  virtual View* GetContentsView() { return contents_; }

 private:
  View* contents_;
};

// Tests that the mouse-wheel messages are correctly rerouted to the window
// under the mouse.
// TODO(jcampan): http://crbug.com/10572 Disabled as it fails on the Vista build
//                bot.
// Note that this fails for a variety of reasons:
// - focused view is apparently reset across window activations and never
//   properly restored
// - this test depends on you not having any other window visible open under the
//   area that it opens the test windows. --beng
TEST_F(ViewTest, DISABLED_RerouteMouseWheelTest) {
  TestViewWithControls* view_with_controls = new TestViewWithControls();
  Window* window1 = Window::CreateChromeWindow(
      NULL, gfx::Rect(0, 0, 100, 100),
      new SimpleWindowDelegate(view_with_controls));
  window1->Show();
  ScrollView* scroll_view = new ScrollView();
  scroll_view->SetContents(new ScrollableTestView());
  Window* window2 = Window::CreateChromeWindow(
      NULL, gfx::Rect(200, 200, 100, 100),
      new SimpleWindowDelegate(scroll_view));
  window2->Show();
  EXPECT_EQ(0, scroll_view->GetVisibleRect().y());

  // Make the window1 active, as this is what it would be in real-world.
  window1->Activate();

  // Let's send a mouse-wheel message to the different controls and check that
  // it is rerouted to the window under the mouse (effectively scrolling the
  // scroll-view).

  // First to the Window's HWND.
  ::SendMessage(view_with_controls->GetWidget()->GetNativeView(),
                WM_MOUSEWHEEL, MAKEWPARAM(0, -20), MAKELPARAM(250, 250));
  EXPECT_EQ(20, scroll_view->GetVisibleRect().y());

  // Then the button.
  ::SendMessage(view_with_controls->button_->GetHWND(),
                WM_MOUSEWHEEL, MAKEWPARAM(0, -20), MAKELPARAM(250, 250));
  EXPECT_EQ(40, scroll_view->GetVisibleRect().y());

  // Then the check-box.
  ::SendMessage(view_with_controls->checkbox_->GetHWND(),
                WM_MOUSEWHEEL, MAKEWPARAM(0, -20), MAKELPARAM(250, 250));
  EXPECT_EQ(60, scroll_view->GetVisibleRect().y());

  // Then the text-field.
  ::SendMessage(view_with_controls->text_field_->GetTestingHandle(),
                WM_MOUSEWHEEL, MAKEWPARAM(0, -20), MAKELPARAM(250, 250));
  EXPECT_EQ(80, scroll_view->GetVisibleRect().y());

  // Ensure we don't scroll when the mouse is not over that window.
  ::SendMessage(view_with_controls->text_field_->GetTestingHandle(),
                WM_MOUSEWHEEL, MAKEWPARAM(0, -20), MAKELPARAM(50, 50));
  EXPECT_EQ(80, scroll_view->GetVisibleRect().y());

  window1->CloseNow();
  window2->CloseNow();
}
#endif

////////////////////////////////////////////////////////////////////////////////
// Dialogs' default button
////////////////////////////////////////////////////////////////////////////////

class MockMenuModel : public ui::MenuModel {
 public:
  MOCK_CONST_METHOD0(HasIcons, bool());
  MOCK_CONST_METHOD1(GetFirstItemIndex, int(gfx::NativeMenu native_menu));
  MOCK_CONST_METHOD0(GetItemCount, int());
  MOCK_CONST_METHOD1(GetTypeAt, ItemType(int index));
  MOCK_CONST_METHOD1(GetCommandIdAt, int(int index));
  MOCK_CONST_METHOD1(GetLabelAt, string16(int index));
  MOCK_CONST_METHOD1(IsItemDynamicAt, bool(int index));
  MOCK_CONST_METHOD1(GetLabelFontAt, const gfx::Font* (int index));
  MOCK_CONST_METHOD2(GetAcceleratorAt, bool(int index,
      ui::Accelerator* accelerator));
  MOCK_CONST_METHOD1(IsItemCheckedAt, bool(int index));
  MOCK_CONST_METHOD1(GetGroupIdAt, int(int index));
  MOCK_METHOD2(GetIconAt, bool(int index, SkBitmap* icon));
  MOCK_CONST_METHOD1(GetButtonMenuItemAt, ui::ButtonMenuItemModel*(int index));
  MOCK_CONST_METHOD1(IsEnabledAt, bool(int index));
  MOCK_CONST_METHOD1(IsVisibleAt, bool(int index));
  MOCK_CONST_METHOD1(GetSubmenuModelAt, MenuModel*(int index));
  MOCK_METHOD1(HighlightChangedTo, void(int index));
  MOCK_METHOD1(ActivatedAt, void(int index));
  MOCK_METHOD2(ActivatedAtWithDisposition, void(int index,
      int disposition));
  MOCK_METHOD0(MenuWillShow, void());
  MOCK_METHOD0(MenuClosed, void());
  MOCK_METHOD1(SetMenuModelDelegate, void(ui::MenuModelDelegate* delegate));
  MOCK_METHOD3(GetModelAndIndexForCommandId, bool(int command_id,
      MenuModel** model, int* index));
};

class TestDialog : public DialogDelegate, public ButtonListener {
 public:
  explicit TestDialog(MockMenuModel* mock_menu_model)
      : contents_(NULL),
        button1_(NULL),
        button2_(NULL),
        checkbox_(NULL),
        button_drop_(NULL),
        last_pressed_button_(NULL),
        mock_menu_model_(mock_menu_model),
        canceled_(false),
        oked_(false) {
  }

  // DialogDelegate implementation:
  virtual int GetDefaultDialogButton() const {
    return MessageBoxFlags::DIALOGBUTTON_OK;
  }

  virtual View* GetContentsView() {
    if (!contents_) {
      contents_ = new View();
      button1_ = new NativeButtonBase(this, L"Button1");
      button2_ = new NativeButtonBase(this, L"Button2");
      checkbox_ = new Checkbox(L"My checkbox");
      button_drop_ = new ButtonDropDown(this, mock_menu_model_);
      contents_->AddChildView(button1_);
      contents_->AddChildView(button2_);
      contents_->AddChildView(checkbox_);
      contents_->AddChildView(button_drop_);
    }
    return contents_;
  }

  // Prevent the dialog from really closing (so we can click the OK/Cancel
  // buttons to our heart's content).
  virtual bool Cancel() {
    canceled_ = true;
    return false;
  }
  virtual bool Accept() {
    oked_ = true;
    return false;
  }

  // ButtonListener implementation.
  virtual void ButtonPressed(Button* sender, const Event& event) {
    last_pressed_button_ = sender;
  }

  void ResetStates() {
    oked_ = false;
    canceled_ = false;
    last_pressed_button_ = NULL;
  }

  // Set up expectations for methods that are called when an (empty) menu is
  // shown from a drop down button.
  void ExpectShowDropMenu() {
    if (mock_menu_model_) {
      EXPECT_CALL(*mock_menu_model_, HasIcons());
      EXPECT_CALL(*mock_menu_model_, GetFirstItemIndex(_));
      EXPECT_CALL(*mock_menu_model_, GetItemCount());
      EXPECT_CALL(*mock_menu_model_, MenuClosed());
    }
  }

  View* contents_;
  NativeButtonBase* button1_;
  NativeButtonBase* button2_;
  NativeButtonBase* checkbox_;
  ButtonDropDown* button_drop_;
  Button* last_pressed_button_;
  MockMenuModel* mock_menu_model_;

  bool canceled_;
  bool oked_;
};

class DefaultButtonTest : public ViewTest {
 public:
  enum ButtonID {
    OK,
    CANCEL,
    BUTTON1,
    BUTTON2
  };

  DefaultButtonTest()
      : focus_manager_(NULL),
        test_dialog_(NULL),
        client_view_(NULL),
        ok_button_(NULL),
        cancel_button_(NULL) {
  }

  virtual void SetUp() {
    test_dialog_ = new TestDialog(NULL);
    Window* window = Window::CreateChromeWindow(NULL, gfx::Rect(0, 0, 100, 100),
                                                test_dialog_);
    window->Show();
    focus_manager_ = test_dialog_->contents_->GetFocusManager();
    ASSERT_TRUE(focus_manager_ != NULL);
    client_view_ =
        static_cast<DialogClientView*>(window->client_view());
    ok_button_ = client_view_->ok_button();
    cancel_button_ = client_view_->cancel_button();
  }

  void SimulatePressingEnterAndCheckDefaultButton(ButtonID button_id) {
    KeyEvent event(ui::ET_KEY_PRESSED, ui::VKEY_RETURN, 0);
    focus_manager_->OnKeyEvent(event);
    switch (button_id) {
      case OK:
        EXPECT_TRUE(test_dialog_->oked_);
        EXPECT_FALSE(test_dialog_->canceled_);
        EXPECT_FALSE(test_dialog_->last_pressed_button_);
        break;
      case CANCEL:
        EXPECT_FALSE(test_dialog_->oked_);
        EXPECT_TRUE(test_dialog_->canceled_);
        EXPECT_FALSE(test_dialog_->last_pressed_button_);
        break;
      case BUTTON1:
        EXPECT_FALSE(test_dialog_->oked_);
        EXPECT_FALSE(test_dialog_->canceled_);
        EXPECT_TRUE(test_dialog_->last_pressed_button_ ==
            test_dialog_->button1_);
        break;
      case BUTTON2:
        EXPECT_FALSE(test_dialog_->oked_);
        EXPECT_FALSE(test_dialog_->canceled_);
        EXPECT_TRUE(test_dialog_->last_pressed_button_ ==
            test_dialog_->button2_);
        break;
    }
    test_dialog_->ResetStates();
  }

  FocusManager* focus_manager_;
  TestDialog* test_dialog_;
  DialogClientView* client_view_;
  NativeButton* ok_button_;
  NativeButton* cancel_button_;
};

TEST_F(DefaultButtonTest, DialogDefaultButtonTest) {
  // Window has just been shown, we expect the default button specified in the
  // DialogDelegate.
  EXPECT_TRUE(ok_button_->is_default());

  // Simulate pressing enter, that should trigger the OK button.
  SimulatePressingEnterAndCheckDefaultButton(OK);

  // Simulate focusing another button, it should become the default button.
  client_view_->FocusWillChange(ok_button_, test_dialog_->button1_);
  EXPECT_FALSE(ok_button_->is_default());
  EXPECT_TRUE(test_dialog_->button1_->is_default());
  // Simulate pressing enter, that should trigger button1.
  SimulatePressingEnterAndCheckDefaultButton(BUTTON1);

  // Now select something that is not a button, the OK should become the default
  // button again.
  client_view_->FocusWillChange(test_dialog_->button1_,
                                test_dialog_->checkbox_);
  EXPECT_TRUE(ok_button_->is_default());
  EXPECT_FALSE(test_dialog_->button1_->is_default());
  SimulatePressingEnterAndCheckDefaultButton(OK);

  // Select yet another button.
  client_view_->FocusWillChange(test_dialog_->checkbox_,
                                test_dialog_->button2_);
  EXPECT_FALSE(ok_button_->is_default());
  EXPECT_FALSE(test_dialog_->button1_->is_default());
  EXPECT_TRUE(test_dialog_->button2_->is_default());
  SimulatePressingEnterAndCheckDefaultButton(BUTTON2);

  // Focus nothing.
  client_view_->FocusWillChange(test_dialog_->button2_, NULL);
  EXPECT_TRUE(ok_button_->is_default());
  EXPECT_FALSE(test_dialog_->button1_->is_default());
  EXPECT_FALSE(test_dialog_->button2_->is_default());
  SimulatePressingEnterAndCheckDefaultButton(OK);

  // Focus the cancel button.
  client_view_->FocusWillChange(NULL, cancel_button_);
  EXPECT_FALSE(ok_button_->is_default());
  EXPECT_TRUE(cancel_button_->is_default());
  EXPECT_FALSE(test_dialog_->button1_->is_default());
  EXPECT_FALSE(test_dialog_->button2_->is_default());
  SimulatePressingEnterAndCheckDefaultButton(CANCEL);
}

class ButtonDropDownTest : public ViewTest {
 public:
  ButtonDropDownTest()
      : test_dialog_(NULL),
        button_as_view_(NULL) {
  }

  virtual void SetUp() {
    test_dialog_ = new TestDialog(&mock_menu_model_);
    Window* window = Window::CreateChromeWindow(NULL, gfx::Rect(0, 0, 100, 100),
                                                test_dialog_);
    window->Show();
    test_dialog_->button_drop_->SetBounds(0, 0, 100, 100);
    // We have to cast the button back into a View in order to invoke it's
    // OnMouseReleased method.
    button_as_view_ = static_cast<View*>(test_dialog_->button_drop_);
  }

  TestDialog* test_dialog_;
  MockMenuModel mock_menu_model_;
  // This is owned by test_dialog_.
  View* button_as_view_;

 private:
  DISALLOW_COPY_AND_ASSIGN(ButtonDropDownTest);
};

// Ensure that regular clicks on the drop down button still work. (i.e. - the
// click events are processed and the listener gets the click)
TEST_F(ButtonDropDownTest, RegularClickTest) {
  MouseEvent press_event(ui::ET_MOUSE_PRESSED, 1, 1, ui::EF_LEFT_BUTTON_DOWN);
  MouseEvent release_event(ui::ET_MOUSE_RELEASED, 1, 1,
                           ui::EF_LEFT_BUTTON_DOWN);
  button_as_view_->OnMousePressed(press_event);
  button_as_view_->OnMouseReleased(release_event);
  EXPECT_EQ(test_dialog_->last_pressed_button_, test_dialog_->button_drop_);
}

#if defined(OS_WIN)
// Ensure that dragging downwards on the button shows the menu while keeping the
// button depressed.
TEST_F(ButtonDropDownTest, DragMenuTest) {
  test_dialog_->last_pressed_button_ = NULL;
  MouseEvent press_event(ui::ET_MOUSE_PRESSED, 1, 1, ui::EF_LEFT_BUTTON_DOWN);
  MouseEvent drag_event(ui::ET_MOUSE_DRAGGED, 1, 99, ui::EF_LEFT_BUTTON_DOWN);
  test_dialog_->ExpectShowDropMenu();
  button_as_view_->OnMousePressed(press_event);
  button_as_view_->OnMouseDragged(drag_event);
  // The button should not get a press event as a result of the drag. This would
  // revert the button into an unpressed state while the menu is open.
  EXPECT_TRUE(test_dialog_->last_pressed_button_ == NULL);
}
#endif

////////////////////////////////////////////////////////////////////////////////
// View hierarchy / Visibility changes
////////////////////////////////////////////////////////////////////////////////
/*
TEST_F(ViewTest, ChangeVisibility) {
#if defined(OS_LINUX)
  // Make CRITICAL messages fatal
  // TODO(oshima): we probably should enable this for entire tests on linux.
  g_log_set_always_fatal(G_LOG_LEVEL_CRITICAL);
#endif
  scoped_ptr<Widget> window(CreateWidget());
  window->Init(NULL, gfx::Rect(0, 0, 500, 300));
  View* root_view = window->GetRootView();
  NativeButtonBase* native = new NativeButtonBase(NULL, L"Native");

  root_view->SetContentsView(native);
  native->SetVisible(true);

  root_view->RemoveChildView(native);
  native->SetVisible(false);
  // Change visibility to true with no widget.
  native->SetVisible(true);

  root_view->SetContentsView(native);
  native->SetVisible(true);
}
*/

////////////////////////////////////////////////////////////////////////////////
// Native view hierachy
////////////////////////////////////////////////////////////////////////////////
class TestNativeViewHierarchy : public View {
 public:
  TestNativeViewHierarchy() {
  }

  virtual void NativeViewHierarchyChanged(bool attached,
                                          gfx::NativeView native_view,
                                          internal::RootView* root_view) {
    NotificationInfo info;
    info.attached = attached;
    info.native_view = native_view;
    info.root_view = root_view;
    notifications_.push_back(info);
  };
  struct NotificationInfo {
    bool attached;
    gfx::NativeView native_view;
    internal::RootView* root_view;
  };
  static const size_t kTotalViews = 2;
  std::vector<NotificationInfo> notifications_;
};

class TestChangeNativeViewHierarchy {
 public:
  explicit TestChangeNativeViewHierarchy(ViewTest *view_test) {
    view_test_ = view_test;
    native_host_ = new NativeViewHost();
    host_ = new Widget;
    Widget::InitParams params(Widget::InitParams::TYPE_WINDOW);
    params.bounds = gfx::Rect(0, 0, 500, 300);
    host_->Init(params);
    host_->GetRootView()->AddChildView(native_host_);
    for (size_t i = 0; i < TestNativeViewHierarchy::kTotalViews; ++i) {
      windows_[i] = new Widget;
      Widget::InitParams params(Widget::InitParams::TYPE_WINDOW);
      params.parent = host_->GetNativeView();
      params.bounds = gfx::Rect(0, 0, 500, 300);
      windows_[i]->Init(params);
      root_views_[i] = windows_[i]->GetRootView();
      test_views_[i] = new TestNativeViewHierarchy;
      root_views_[i]->AddChildView(test_views_[i]);
    }
  }

  ~TestChangeNativeViewHierarchy() {
    for (size_t i = 0; i < TestNativeViewHierarchy::kTotalViews; ++i) {
      windows_[i]->Close();
    }
    host_->Close();
    // Will close and self-delete widgets - no need to manually delete them.
    view_test_->RunPendingMessages();
  }

  void CheckEnumeratingNativeWidgets() {
    if (!host_->GetContainingWindow())
      return;
    NativeWidget::NativeWidgets widgets;
    NativeWidget::GetAllNativeWidgets(host_->GetNativeView(), &widgets);
    EXPECT_EQ(TestNativeViewHierarchy::kTotalViews + 1, widgets.size());
    // Unfortunately there is no guarantee the sequence of views here so always
    // go through all of them.
    for (NativeWidget::NativeWidgets::iterator i = widgets.begin();
         i != widgets.end(); ++i) {
      View* root_view = (*i)->GetWidget()->GetRootView();
      if (host_->GetRootView() == root_view)
        continue;
      size_t j;
      for (j = 0; j < TestNativeViewHierarchy::kTotalViews; ++j)
        if (root_views_[j] == root_view)
          break;
      // EXPECT_LT/GT/GE() fails to compile with class-defined constants
      // with gcc, with error
      // "error: undefined reference to 'TestNativeViewHierarchy::kTotalViews'"
      // so I forced to use EXPECT_TRUE() instead.
      EXPECT_TRUE(TestNativeViewHierarchy::kTotalViews > j);
    }
  }

  void CheckChangingHierarhy() {
    size_t i;
    for (i = 0; i < TestNativeViewHierarchy::kTotalViews; ++i) {
      // TODO(georgey): use actual hierarchy changes to send notifications.
      static_cast<internal::RootView*>(root_views_[i])->
          NotifyNativeViewHierarchyChanged(false, host_->GetNativeView());
      static_cast<internal::RootView*>(root_views_[i])->
          NotifyNativeViewHierarchyChanged(true, host_->GetNativeView());
    }
    for (i = 0; i < TestNativeViewHierarchy::kTotalViews; ++i) {
      ASSERT_EQ(static_cast<size_t>(2), test_views_[i]->notifications_.size());
      EXPECT_FALSE(test_views_[i]->notifications_[0].attached);
      EXPECT_EQ(host_->GetNativeView(),
          test_views_[i]->notifications_[0].native_view);
      EXPECT_EQ(root_views_[i], test_views_[i]->notifications_[0].root_view);
      EXPECT_TRUE(test_views_[i]->notifications_[1].attached);
      EXPECT_EQ(host_->GetNativeView(),
          test_views_[i]->notifications_[1].native_view);
      EXPECT_EQ(root_views_[i], test_views_[i]->notifications_[1].root_view);
    }
  }

  NativeViewHost* native_host_;
  Widget* host_;
  Widget* windows_[TestNativeViewHierarchy::kTotalViews];
  View* root_views_[TestNativeViewHierarchy::kTotalViews];
  TestNativeViewHierarchy* test_views_[TestNativeViewHierarchy::kTotalViews];
  ViewTest* view_test_;
};

TEST_F(ViewTest, ChangeNativeViewHierarchyFindRoots) {
  // TODO(georgey): Fix the test for Linux
#if defined(OS_WIN)
  TestChangeNativeViewHierarchy test(this);
  test.CheckEnumeratingNativeWidgets();
#endif
}

TEST_F(ViewTest, ChangeNativeViewHierarchyChangeHierarchy) {
  // TODO(georgey): Fix the test for Linux
#if defined(OS_WIN)
  TestChangeNativeViewHierarchy test(this);
  test.CheckChangingHierarhy();
#endif
}

////////////////////////////////////////////////////////////////////////////////
// Transformations
////////////////////////////////////////////////////////////////////////////////

class TransformPaintView : public TestView {
 public:
  TransformPaintView() {}
  virtual ~TransformPaintView() {}

  void ClearScheduledPaintRect() {
    scheduled_paint_rect_ = gfx::Rect();
  }

  gfx::Rect scheduled_paint_rect() const { return scheduled_paint_rect_; }

  // Overridden from View:
  virtual void SchedulePaintInRect(const gfx::Rect& rect) {
    gfx::Rect xrect = ConvertRectToParent(rect);
    scheduled_paint_rect_ = scheduled_paint_rect_.Union(xrect);
  }

 private:
  gfx::Rect scheduled_paint_rect_;

  DISALLOW_COPY_AND_ASSIGN(TransformPaintView);
};

TEST_F(ViewTest, TransformPaint) {
  TransformPaintView* v1 = new TransformPaintView();
  v1->SetBounds(0, 0, 500, 300);

  TestView* v2 = new TestView();
  v2->SetBounds(100, 100, 200, 100);

  Widget* widget = new Widget;
  Widget::InitParams params(Widget::InitParams::TYPE_WINDOW);
  params.bounds = gfx::Rect(50, 50, 650, 650);
  widget->Init(params);
  widget->Show();
  View* root = widget->GetRootView();

  root->AddChildView(v1);
  v1->AddChildView(v2);

  // At this moment, |v2| occupies (100, 100) to (300, 200) in |root|.
  v1->ClearScheduledPaintRect();
  v2->SchedulePaint();

  EXPECT_EQ(gfx::Rect(100, 100, 200, 100), v1->scheduled_paint_rect());

  // Rotate |v1| counter-clockwise.
  ui::Transform transform;
  transform.SetRotate(-90.0f);
  transform.SetTranslateY(500.0f);
  v1->SetTransform(transform);

  // |v2| now occupies (100, 200) to (200, 400) in |root|.

  v1->ClearScheduledPaintRect();
  v2->SchedulePaint();

  EXPECT_EQ(gfx::Rect(100, 200, 100, 200), v1->scheduled_paint_rect());

  widget->CloseNow();
}

TEST_F(ViewTest, TransformEvent) {
  TestView* v1 = new TestView();
  v1->SetBounds(0, 0, 500, 300);

  TestView* v2 = new TestView();
  v2->SetBounds(100, 100, 200, 100);

  Widget* widget = new Widget;
  Widget::InitParams params(Widget::InitParams::TYPE_WINDOW);
  params.bounds = gfx::Rect(50, 50, 650, 650);
  widget->Init(params);
  View* root = widget->GetRootView();

  root->AddChildView(v1);
  v1->AddChildView(v2);

  // At this moment, |v2| occupies (100, 100) to (300, 200) in |root|.

  // Rotate |v1| counter-clockwise.
  ui::Transform transform(v1->GetTransform());
  transform.SetRotate(-90.0f);
  transform.SetTranslateY(500.0f);
  v1->SetTransform(transform);

  // |v2| now occupies (100, 200) to (200, 400) in |root|.
  v1->Reset();
  v2->Reset();

  MouseEvent pressed(ui::ET_MOUSE_PRESSED,
                     110, 210,
                     ui::EF_LEFT_BUTTON_DOWN);
  root->OnMousePressed(pressed);
  EXPECT_EQ(0, v1->last_mouse_event_type_);
  EXPECT_EQ(ui::ET_MOUSE_PRESSED, v2->last_mouse_event_type_);
  EXPECT_EQ(190, v2->location_.x());
  EXPECT_EQ(10, v2->location_.y());

  MouseEvent released(ui::ET_MOUSE_RELEASED, 0, 0, 0);
  root->OnMouseReleased(released);

  // Now rotate |v2| inside |v1| clockwise.
  transform = v2->GetTransform();
  transform.SetRotate(90.0f);
  transform.SetTranslateX(100.0f);
  v2->SetTransform(transform);

  // Now, |v2| occupies (100, 100) to (200, 300) in |v1|, and (100, 300) to
  // (300, 400) in |root|.

  v1->Reset();
  v2->Reset();

  MouseEvent p2(ui::ET_MOUSE_PRESSED,
                110, 320,
                ui::EF_LEFT_BUTTON_DOWN);
  root->OnMousePressed(p2);
  EXPECT_EQ(0, v1->last_mouse_event_type_);
  EXPECT_EQ(ui::ET_MOUSE_PRESSED, v2->last_mouse_event_type_);
  EXPECT_EQ(10, v2->location_.x());
  EXPECT_EQ(20, v2->location_.y());

  root->OnMouseReleased(released);

  v1->SetTransform(ui::Transform());
  v2->SetTransform(ui::Transform());

  TestView* v3 = new TestView();
  v3->SetBounds(10, 10, 20, 30);
  v2->AddChildView(v3);

  // Rotate |v3| clockwise with respect to |v2|.
  transform = v1->GetTransform();
  transform.SetRotate(90.0f);
  transform.SetTranslateX(30.0f);
  v3->SetTransform(transform);

  // Scale |v2| with respect to |v1| along both axis.
  transform = v2->GetTransform();
  transform.SetScale(0.8f, 0.5f);
  v2->SetTransform(transform);

  // |v3| occupies (108, 105) to (132, 115) in |root|.

  v1->Reset();
  v2->Reset();
  v3->Reset();

  MouseEvent p3(ui::ET_MOUSE_PRESSED,
                112, 110,
                ui::EF_LEFT_BUTTON_DOWN);
  root->OnMousePressed(p3);

  EXPECT_EQ(ui::ET_MOUSE_PRESSED, v3->last_mouse_event_type_);
  EXPECT_EQ(10, v3->location_.x());
  EXPECT_EQ(25, v3->location_.y());

  root->OnMouseReleased(released);

  v1->SetTransform(ui::Transform());
  v2->SetTransform(ui::Transform());
  v3->SetTransform(ui::Transform());

  v1->Reset();
  v2->Reset();
  v3->Reset();

  // Rotate |v3| clockwise with respect to |v2|, and scale it along both axis.
  transform = v3->GetTransform();
  transform.SetRotate(90.0f);
  transform.SetTranslateX(30.0f);
  // Rotation sets some scaling transformation. Using SetScale would overwrite
  // that and pollute the rotation. So combine the scaling with the existing
  // transforamtion.
  transform.ConcatScale(0.8f, 0.5f);
  v3->SetTransform(transform);

  // Translate |v2| with respect to |v1|.
  transform = v2->GetTransform();
  transform.SetTranslate(10, 10);
  v2->SetTransform(transform);

  // |v3| now occupies (120, 120) to (144, 130) in |root|.

  MouseEvent p4(ui::ET_MOUSE_PRESSED,
                124, 125,
                ui::EF_LEFT_BUTTON_DOWN);
  root->OnMousePressed(p4);

  EXPECT_EQ(ui::ET_MOUSE_PRESSED, v3->last_mouse_event_type_);
  EXPECT_EQ(10, v3->location_.x());
  EXPECT_EQ(25, v3->location_.y());

  root->OnMouseReleased(released);

  widget->CloseNow();
}

////////////////////////////////////////////////////////////////////////////////
// OnVisibleBoundsChanged()

class VisibleBoundsView : public View {
 public:
  VisibleBoundsView() : received_notification_(false) {}
  virtual ~VisibleBoundsView() {}

  bool received_notification() const { return received_notification_; }
  void set_received_notification(bool received) {
    received_notification_ = received;
  }

 private:
  // Overridden from View:
  virtual bool NeedsNotificationWhenVisibleBoundsChange() const {
     return true;
  }
  virtual void OnVisibleBoundsChanged() {
    received_notification_ = true;
  }

  bool received_notification_;

  DISALLOW_COPY_AND_ASSIGN(VisibleBoundsView);
};

#if defined(OS_WIN)
// TODO(beng): This can be cross platform when widget construction/init is.
TEST_F(ViewTest, OnVisibleBoundsChanged) {
  gfx::Rect viewport_bounds(0, 0, 100, 100);

  scoped_ptr<Widget> widget(new Widget);
  Widget::InitParams params(Widget::InitParams::TYPE_WINDOW);
  params.ownership = views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  params.bounds = viewport_bounds;
  widget->Init(params);
  widget->GetRootView()->SetBoundsRect(viewport_bounds);

  View* viewport = new View;
  widget->SetContentsView(viewport);
  View* contents = new View;
  viewport->AddChildView(contents);
  viewport->SetBoundsRect(viewport_bounds);
  contents->SetBounds(0, 0, 100, 200);

  // Create a view that cares about visible bounds notifications, and position
  // it just outside the visible bounds of the viewport.
  VisibleBoundsView* child = new VisibleBoundsView;
  contents->AddChildView(child);
  child->SetBounds(10, 110, 50, 50);

  // The child bound should be fully clipped.
  EXPECT_TRUE(child->GetVisibleBounds().IsEmpty());

  // Now scroll the contents, but not enough to make the child visible.
  contents->SetY(contents->y() - 1);

  // We should have received the notification since the visible bounds may have
  // changed (even though they didn't).
  EXPECT_TRUE(child->received_notification());
  EXPECT_TRUE(child->GetVisibleBounds().IsEmpty());
  child->set_received_notification(false);

  // Now scroll the contents, this time by enough to make the child visible by
  // one pixel.
  contents->SetY(contents->y() - 10);
  EXPECT_TRUE(child->received_notification());
  EXPECT_EQ(1, child->GetVisibleBounds().height());
  child->set_received_notification(false);

  widget->CloseNow();
}

#endif

////////////////////////////////////////////////////////////////////////////////
// BoundsChanged()

TEST_F(ViewTest, SetBoundsPaint) {
  TestView* top_view = new TestView;
  TestView* child_view = new TestView;

  top_view->SetBounds(0, 0, 100, 100);
  top_view->scheduled_paint_rects_.clear();
  child_view->SetBounds(10, 10, 20, 20);
  top_view->AddChildView(child_view);

  top_view->scheduled_paint_rects_.clear();
  child_view->SetBounds(30, 30, 20, 20);
  EXPECT_EQ(2U, top_view->scheduled_paint_rects_.size());

  // There should be 2 rects, spanning from (10, 10) to (50, 50).
  gfx::Rect paint_rect =
      top_view->scheduled_paint_rects_[0].Union(
          top_view->scheduled_paint_rects_[1]);
  EXPECT_EQ(gfx::Rect(10, 10, 40, 40), paint_rect);
}

// Tests conversion methods with a transform.
TEST_F(ViewTest, ConvertPointToViewWithTransform) {
  TestView top_view;
  TestView* child = new TestView;
  TestView* child_child = new TestView;

  top_view.AddChildView(child);
  child->AddChildView(child_child);

  top_view.SetBounds(0, 0, 1000, 1000);

  child->SetBounds(7, 19, 500, 500);
  ui::Transform transform;
  transform.SetScale(3.0f, 4.0f);
  child->SetTransform(transform);

  child_child->SetBounds(17, 13, 100, 100);
  transform = ui::Transform();
  transform.SetScale(5.0f, 7.0f);
  child_child->SetTransform(transform);

  // Sanity check to make sure basic transforms act as expected.
  {
    ui::Transform transform;
    transform.ConcatTranslate(1, 1);
    transform.ConcatScale(100, 55);
    transform.ConcatTranslate(110, -110);

    EXPECT_EQ(210, transform.matrix().getTranslateX());
    EXPECT_EQ(-55, transform.matrix().getTranslateY());
    EXPECT_EQ(100, transform.matrix().getScaleX());
    EXPECT_EQ(55, transform.matrix().getScaleY());
    EXPECT_EQ(0, transform.matrix().getSkewX());
    EXPECT_EQ(0, transform.matrix().getSkewY());
  }

  {
    ui::Transform transform;
    transform.SetTranslate(1, 1);
    ui::Transform t2;
    t2.SetScale(100, 55);
    ui::Transform t3;
    t3.SetTranslate(110, -110);
    transform.ConcatTransform(t2);
    transform.ConcatTransform(t3);

    EXPECT_EQ(210, transform.matrix().getTranslateX());
    EXPECT_EQ(-55, transform.matrix().getTranslateY());
    EXPECT_EQ(100, transform.matrix().getScaleX());
    EXPECT_EQ(55, transform.matrix().getScaleY());
    EXPECT_EQ(0, transform.matrix().getSkewX());
    EXPECT_EQ(0, transform.matrix().getSkewY());
  }

  // Conversions from child->top and top->child.
  {
    gfx::Point point(5, 5);
    View::ConvertPointToView(child, &top_view, &point);
    EXPECT_EQ(22, point.x());
    EXPECT_EQ(39, point.y());

    point.SetPoint(22, 39);
    View::ConvertPointToView(&top_view, child, &point);
    EXPECT_EQ(5, point.x());
    EXPECT_EQ(5, point.y());
  }

  // Conversions from child_child->top and top->child_child.
  {
    gfx::Point point(5, 5);
    View::ConvertPointToView(child_child, &top_view, &point);
    EXPECT_EQ(133, point.x());
    EXPECT_EQ(211, point.y());

    point.SetPoint(133, 211);
    View::ConvertPointToView(&top_view, child_child, &point);
    EXPECT_EQ(5, point.x());
    EXPECT_EQ(5, point.y());
  }

  // Conversions from child_child->child and child->child_child
  {
    gfx::Point point(5, 5);
    View::ConvertPointToView(child_child, child, &point);
    EXPECT_EQ(42, point.x());
    EXPECT_EQ(48, point.y());

    point.SetPoint(42, 48);
    View::ConvertPointToView(child, child_child, &point);
    EXPECT_EQ(5, point.x());
    EXPECT_EQ(5, point.y());
  }

  // Conversions from top_view to child with a value that should be negative.
  // This ensures we don't round up with negative numbers.
  {
    gfx::Point point(6, 18);
    View::ConvertPointToView(&top_view, child, &point);
    EXPECT_EQ(-1, point.x());
    EXPECT_EQ(-1, point.y());
  }
}

TEST_F(ViewTest, Contains) {
  TestView v1;
  TestView* v2 = new TestView();
  TestView* v3 = new TestView();

  v1.AddChildView(v2);
  v2->AddChildView(v3);

  EXPECT_FALSE(v1.Contains(NULL));
  EXPECT_TRUE(v1.Contains(&v1));
  EXPECT_TRUE(v1.Contains(v2));
  EXPECT_TRUE(v1.Contains(v3));
  EXPECT_FALSE(v2->Contains(NULL));
  EXPECT_TRUE(v2->Contains(v2));
  EXPECT_FALSE(v2->Contains(&v1));
  EXPECT_TRUE(v2->Contains(v3));
  EXPECT_FALSE(v3->Contains(NULL));
  EXPECT_TRUE(v3->Contains(v3));
  EXPECT_FALSE(v3->Contains(&v1));
  EXPECT_FALSE(v3->Contains(v2));
}

}  // namespace views
