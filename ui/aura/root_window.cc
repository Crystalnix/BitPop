// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/aura/root_window.h"

#include <string>
#include <vector>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/message_loop.h"
#include "base/string_number_conversions.h"
#include "base/string_split.h"
#include "ui/aura/aura_switches.h"
#include "ui/aura/client/activation_client.h"
#include "ui/aura/root_window_host.h"
#include "ui/aura/root_window_observer.h"
#include "ui/aura/event.h"
#include "ui/aura/event_filter.h"
#include "ui/aura/focus_manager.h"
#include "ui/aura/gestures/gesture_recognizer.h"
#include "ui/aura/screen_aura.h"
#include "ui/aura/window.h"
#include "ui/aura/window_delegate.h"
#include "ui/base/hit_test.h"
#include "ui/gfx/compositor/compositor.h"
#include "ui/gfx/compositor/layer.h"
#include "ui/gfx/compositor/layer_animator.h"

using std::string;
using std::vector;

namespace aura {

namespace {

// Default bounds for the host window.
static const int kDefaultHostWindowX = 200;
static const int kDefaultHostWindowY = 200;
static const int kDefaultHostWindowWidth = 1280;
static const int kDefaultHostWindowHeight = 1024;

// Returns true if |target| has a non-client (frame) component at |location|,
// in window coordinates.
bool IsNonClientLocation(Window* target, const gfx::Point& location) {
  if (!target->delegate())
    return false;
  int hit_test_code = target->delegate()->GetNonClientComponent(location);
  return hit_test_code != HTCLIENT && hit_test_code != HTNOWHERE;
}

typedef std::vector<EventFilter*> EventFilters;

void GetEventFiltersToNotify(Window* target, EventFilters* filters) {
  while (target) {
    if (target->event_filter())
      filters->push_back(target->event_filter());
    target = target->parent();
  }
}

}  // namespace

RootWindow* RootWindow::instance_ = NULL;
bool RootWindow::use_fullscreen_host_window_ = false;

////////////////////////////////////////////////////////////////////////////////
// RootWindow, public:

// static
RootWindow* RootWindow::GetInstance() {
  if (!instance_) {
    instance_ = new RootWindow;
    instance_->Init();
  }
  return instance_;
}

// static
void RootWindow::DeleteInstance() {
  delete instance_;
  instance_ = NULL;
}

void RootWindow::ShowRootWindow() {
  host_->Show();
}

void RootWindow::SetHostSize(const gfx::Size& size) {
  host_->SetSize(size);
  // Requery the location to constrain it within the new root window size.
  last_mouse_location_ = host_->QueryMouseLocation();
  synthesize_mouse_move_ = false;
}

gfx::Size RootWindow::GetHostSize() const {
  gfx::Rect rect(host_->GetSize());
  layer()->transform().TransformRect(&rect);
  return rect.size();
}

void RootWindow::SetCursor(gfx::NativeCursor cursor) {
  last_cursor_ = cursor;
  // A lot of code seems to depend on NULL cursors actually showing an arrow,
  // so just pass everything along to the host.
  host_->SetCursor(cursor);
}

void RootWindow::ShowCursor(bool show) {
  host_->ShowCursor(show);
}

void RootWindow::MoveCursorTo(const gfx::Point& location) {
  host_->MoveCursorTo(location);
}

bool RootWindow::ConfineCursorToWindow() {
  // We would like to be able to confine the cursor to that window. However,
  // currently, we do not have such functionality in X. So we just confine
  // to the root window. This is ok because this option is currently only
  // being used in fullscreen mode, so root_window bounds = window bounds.
  return host_->ConfineCursorToRootWindow();
}

void RootWindow::Run() {
  ShowRootWindow();
  MessageLoopForUI::current()->Run();
}

void RootWindow::Draw() {
  compositor_->Draw(false);
}

bool RootWindow::DispatchMouseEvent(MouseEvent* event) {
  static const int kMouseButtonFlagMask =
      ui::EF_LEFT_MOUSE_BUTTON |
      ui::EF_MIDDLE_MOUSE_BUTTON |
      ui::EF_RIGHT_MOUSE_BUTTON;

  event->UpdateForRootTransform(layer()->transform());

  last_mouse_location_ = event->location();
  synthesize_mouse_move_ = false;

  Window* target =
      mouse_pressed_handler_ ? mouse_pressed_handler_ : capture_window_;
  if (!target)
    target = GetEventHandlerForPoint(event->location());
  switch (event->type()) {
    case ui::ET_MOUSE_MOVED:
      HandleMouseMoved(*event, target);
      break;
    case ui::ET_MOUSE_PRESSED:
      if (!mouse_pressed_handler_)
        mouse_pressed_handler_ = target;
      mouse_button_flags_ = event->flags() & kMouseButtonFlagMask;
      break;
    case ui::ET_MOUSE_RELEASED:
      mouse_pressed_handler_ = NULL;
      mouse_button_flags_ = event->flags() & kMouseButtonFlagMask;
      break;
    default:
      break;
  }
  if (target && target->delegate()) {
    int flags = event->flags();
    gfx::Point location_in_window = event->location();
    Window::ConvertPointToWindow(this, target, &location_in_window);
    if (IsNonClientLocation(target, location_in_window))
      flags |= ui::EF_IS_NON_CLIENT;
    MouseEvent translated_event(*event, this, target, event->type(), flags);
    return ProcessMouseEvent(target, &translated_event);
  }
  return false;
}

bool RootWindow::DispatchKeyEvent(KeyEvent* event) {
  KeyEvent translated_event(*event);
  return ProcessKeyEvent(focused_window_, &translated_event);
}

bool RootWindow::DispatchScrollEvent(ScrollEvent* event) {
  event->UpdateForRootTransform(layer()->transform());

  last_mouse_location_ = event->location();
  synthesize_mouse_move_ = false;

  Window* target =
      mouse_pressed_handler_ ? mouse_pressed_handler_ : capture_window_;
  if (!target)
    target = GetEventHandlerForPoint(event->location());

  if (target && target->delegate()) {
    int flags = event->flags();
    gfx::Point location_in_window = event->location();
    Window::ConvertPointToWindow(this, target, &location_in_window);
    if (IsNonClientLocation(target, location_in_window))
      flags |= ui::EF_IS_NON_CLIENT;
    ScrollEvent translated_event(*event, this, target, event->type(), flags);
    return ProcessMouseEvent(target, &translated_event);
  }
  return false;
}

bool RootWindow::DispatchTouchEvent(TouchEvent* event) {
  event->UpdateForRootTransform(layer()->transform());
  bool handled = false;
  Window* target =
      touch_event_handler_ ? touch_event_handler_ : capture_window_;
  if (!target)
    target = GetEventHandlerForPoint(event->location());

  ui::TouchStatus status = ui::TOUCH_STATUS_UNKNOWN;
  if (target) {
    TouchEvent translated_event(*event, this, target);
    status = ProcessTouchEvent(target, &translated_event);
    if (status == ui::TOUCH_STATUS_START)
      touch_event_handler_ = target;
    else if (status == ui::TOUCH_STATUS_END ||
             status == ui::TOUCH_STATUS_CANCEL)
      touch_event_handler_ = NULL;
    handled = status != ui::TOUCH_STATUS_UNKNOWN;

    if (status == ui::TOUCH_STATUS_QUEUED)
      gesture_recognizer_->QueueTouchEventForGesture(target, *event);
  }

  // Get the list of GestureEvents from GestureRecognizer.
  scoped_ptr<GestureRecognizer::Gestures> gestures;
  gestures.reset(gesture_recognizer_->ProcessTouchEventForGesture(*event,
        status));
  if (ProcessGestures(gestures.get()))
    handled = true;

  return handled;
}

bool RootWindow::DispatchGestureEvent(GestureEvent* event) {
  Window* target = gesture_handler_ ? gesture_handler_ : capture_window_;
  if (!target)
    target = GetEventHandlerForPoint(event->location());
  if (target) {
    GestureEvent translated_event(*event, this, target);
    ui::GestureStatus status = ProcessGestureEvent(target, &translated_event);
    return status != ui::GESTURE_STATUS_UNKNOWN;
  }

  return false;
}

void RootWindow::OnHostResized(const gfx::Size& size) {
  // The compositor should have the same size as the native root window host.
  compositor_->WidgetSizeChanged(size);

  // The layer, and all the observers should be notified of the
  // transformed size of the root window.
  gfx::Rect bounds(size);
  layer()->transform().TransformRect(&bounds);
  SetBounds(gfx::Rect(bounds.size()));
  FOR_EACH_OBSERVER(RootWindowObserver, observers_,
                    OnRootWindowResized(bounds.size()));
}

void RootWindow::OnNativeScreenResized(const gfx::Size& size) {
  if (use_fullscreen_host_window_)
    SetHostSize(size);
}

void RootWindow::OnWindowInitialized(Window* window) {
  FOR_EACH_OBSERVER(RootWindowObserver, observers_,
                    OnWindowInitialized(window));
  if (window->IsVisible() && window->ContainsPointInRoot(last_mouse_location_))
    PostMouseMoveEventAfterWindowChange();
}

void RootWindow::OnWindowDestroying(Window* window) {
  // Update the focused window state if the window was focused.
  if (focused_window_ == window) {
    Window* transient_parent = focused_window_->transient_parent();
    if (transient_parent) {
      // Has to be removed from the transient parent before focusing, otherwise
      // |window| will be focused again.
      transient_parent->RemoveTransientChild(window);
      SetFocusedWindow(transient_parent);
    } else {
      SetFocusedWindow(focused_window_->parent());
    }
  }

  // When a window is being destroyed it's likely that the WindowDelegate won't
  // want events, so we reset the mouse_pressed_handler_ and capture_window_ and
  // don't sent it release/capture lost events.
  if (mouse_pressed_handler_ == window)
    mouse_pressed_handler_ = NULL;
  if (mouse_moved_handler_ == window)
    mouse_moved_handler_ = NULL;
  if (capture_window_ == window)
    capture_window_ = NULL;
  if (touch_event_handler_ == window)
    touch_event_handler_ = NULL;
  if (gesture_handler_ == window)
    gesture_handler_ = NULL;

  gesture_recognizer_->FlushTouchQueue(window);

  if (window->IsVisible() &&
      window->ContainsPointInRoot(last_mouse_location_)) {
    PostMouseMoveEventAfterWindowChange();
  }
}

void RootWindow::OnWindowBoundsChanged(Window* window,
                                       bool contained_mouse_point) {
  if (contained_mouse_point ||
      (window->IsVisible() &&
       window->ContainsPointInRoot(last_mouse_location_))) {
    PostMouseMoveEventAfterWindowChange();
  }
}

void RootWindow::OnWindowVisibilityChanged(Window* window, bool is_visible) {
  if (window->ContainsPointInRoot(last_mouse_location_))
    PostMouseMoveEventAfterWindowChange();
}

void RootWindow::OnWindowTransformed(Window* window, bool contained_mouse) {
  if (contained_mouse ||
      (window->IsVisible() &&
       window->ContainsPointInRoot(last_mouse_location_))) {
    PostMouseMoveEventAfterWindowChange();
  }
}

#if !defined(OS_MACOSX)
MessageLoop::Dispatcher* RootWindow::GetDispatcher() {
  return host_.get();
}
#endif  // !defined(OS_MACOSX)

void RootWindow::AddRootWindowObserver(RootWindowObserver* observer) {
  observers_.AddObserver(observer);
}

void RootWindow::RemoveRootWindowObserver(RootWindowObserver* observer) {
  observers_.RemoveObserver(observer);
}

bool RootWindow::IsMouseButtonDown() const {
  return mouse_button_flags_ != 0;
}

void RootWindow::PostNativeEvent(const base::NativeEvent& native_event) {
#if !defined(OS_MACOSX)
  host_->PostNativeEvent(native_event);
#endif
}

void RootWindow::ConvertPointToNativeScreen(gfx::Point* point) const {
  gfx::Point location = host_->GetLocationOnNativeScreen();
  point->Offset(location.x(), location.y());
}

void RootWindow::SetCapture(Window* window) {
  if (capture_window_ == window)
    return;

  if (capture_window_ && capture_window_->delegate())
    capture_window_->delegate()->OnCaptureLost();
  capture_window_ = window;

  if (capture_window_) {
    // Make all subsequent mouse events and touch go to the capture window. We
    // shouldn't need to send an event here as OnCaptureLost should take care of
    // that.
    if (touch_event_handler_)
      touch_event_handler_ = capture_window_;
    if (mouse_moved_handler_ || mouse_button_flags_ != 0)
      mouse_moved_handler_ = capture_window_;
    if (gesture_handler_)
      gesture_handler_ = capture_window_;
  } else {
    // When capture is lost, we must reset the event handlers.
    touch_event_handler_ = NULL;
    mouse_moved_handler_ = NULL;
    gesture_handler_ = NULL;

    host_->UnConfineCursor();
  }
  mouse_pressed_handler_ = NULL;
}

void RootWindow::ReleaseCapture(Window* window) {
  if (capture_window_ != window)
    return;
  SetCapture(NULL);
}

void RootWindow::AdvanceQueuedTouchEvent(Window* window, bool processed) {
  scoped_ptr<GestureRecognizer::Gestures> gestures;
  gestures.reset(gesture_recognizer_->AdvanceTouchQueue(window, processed));
  ProcessGestures(gestures.get());
}

void RootWindow::SetGestureRecognizerForTesting(GestureRecognizer* gr) {
  gesture_recognizer_.reset(gr);
}

void RootWindow::SetTransform(const ui::Transform& transform) {
  Window::SetTransform(transform);

  // If the layer is not animating, then we need to update the host size
  // immediately.
  if (!layer()->GetAnimator()->is_animating())
    OnHostResized(host_->GetSize());
}

#if !defined(NDEBUG)
void RootWindow::ToggleFullScreen() {
  host_->ToggleFullScreen();
}
#endif

////////////////////////////////////////////////////////////////////////////////
// RootWindow, private:

RootWindow::RootWindow()
    : Window(NULL),
      host_(aura::RootWindowHost::Create(GetInitialHostWindowBounds())),
      ALLOW_THIS_IN_INITIALIZER_LIST(schedule_paint_factory_(this)),
      ALLOW_THIS_IN_INITIALIZER_LIST(event_factory_(this)),
      mouse_button_flags_(0),
      last_cursor_(kCursorNull),
      screen_(new ScreenAura),
      capture_window_(NULL),
      mouse_pressed_handler_(NULL),
      mouse_moved_handler_(NULL),
      focused_window_(NULL),
      touch_event_handler_(NULL),
      gesture_handler_(NULL),
      gesture_recognizer_(GestureRecognizer::Create()),
      synthesize_mouse_move_(false) {
  SetName("RootWindow");
  gfx::Screen::SetInstance(screen_);
  last_mouse_location_ = host_->QueryMouseLocation();

  ui::Compositor::Initialize(false);
  compositor_ = new ui::Compositor(this, host_->GetAcceleratedWidget(),
      host_->GetSize());
  DCHECK(compositor_.get());
}

RootWindow::~RootWindow() {
  // Make sure to destroy the compositor before terminating so that state is
  // cleared and we don't hit asserts.
  compositor_ = NULL;

  // Tear down in reverse.  Frees any references held by the host.
  host_.reset(NULL);

  // An observer may have been added by an animation on the RootWindow.
  layer()->GetAnimator()->RemoveObserver(this);
  ui::Compositor::Terminate();
  if (instance_ == this)
    instance_ = NULL;
}

void RootWindow::HandleMouseMoved(const MouseEvent& event, Window* target) {
  if (target == mouse_moved_handler_)
    return;

  // Send an exited event.
  if (mouse_moved_handler_ && mouse_moved_handler_->delegate()) {
    MouseEvent translated_event(event, this, mouse_moved_handler_,
                                ui::ET_MOUSE_EXITED, event.flags());
    ProcessMouseEvent(mouse_moved_handler_, &translated_event);
  }
  mouse_moved_handler_ = target;
  // Send an entered event.
  if (mouse_moved_handler_ && mouse_moved_handler_->delegate()) {
    MouseEvent translated_event(event, this, mouse_moved_handler_,
                                ui::ET_MOUSE_ENTERED, event.flags());
    ProcessMouseEvent(mouse_moved_handler_, &translated_event);
  }
}

bool RootWindow::ProcessMouseEvent(Window* target, MouseEvent* event) {
  if (!target->IsVisible())
    return false;

  EventFilters filters;
  GetEventFiltersToNotify(target->parent(), &filters);
  for (EventFilters::const_reverse_iterator it = filters.rbegin();
       it != filters.rend(); ++it) {
    if ((*it)->PreHandleMouseEvent(target, event))
      return true;
  }

  return target->delegate()->OnMouseEvent(event);
}

bool RootWindow::ProcessKeyEvent(Window* target, KeyEvent* event) {
  EventFilters filters;

  if (!target) {
    // When no window is focused, send the key event to |this| so event filters
    // for the window could check if the key is a global shortcut like Alt+Tab.
    target = this;
    GetEventFiltersToNotify(this, &filters);
  } else {
    if (!target->IsVisible())
      return false;
    GetEventFiltersToNotify(target->parent(), &filters);
  }

  for (EventFilters::const_reverse_iterator it = filters.rbegin();
       it != filters.rend(); ++it) {
    if ((*it)->PreHandleKeyEvent(target, event))
      return true;
  }

  if (!target->delegate())
    return false;
  return target->delegate()->OnKeyEvent(event);
}

ui::TouchStatus RootWindow::ProcessTouchEvent(Window* target,
                                              TouchEvent* event) {
  if (!target->IsVisible())
    return ui::TOUCH_STATUS_UNKNOWN;

  EventFilters filters;
  GetEventFiltersToNotify(target->parent(), &filters);
  for (EventFilters::const_reverse_iterator it = filters.rbegin();
       it != filters.rend(); ++it) {
    ui::TouchStatus status = (*it)->PreHandleTouchEvent(target, event);
    if (status != ui::TOUCH_STATUS_UNKNOWN)
      return status;
  }

  return target->delegate()->OnTouchEvent(event);
}

ui::GestureStatus RootWindow::ProcessGestureEvent(Window* target,
                                                  GestureEvent* event) {
  if (!target->IsVisible())
    return ui::GESTURE_STATUS_UNKNOWN;

  EventFilters filters;
  GetEventFiltersToNotify(target->parent(), &filters);
  ui::GestureStatus status = ui::GESTURE_STATUS_UNKNOWN;
  for (EventFilters::const_reverse_iterator it = filters.rbegin();
       it != filters.rend(); ++it) {
    status = (*it)->PreHandleGestureEvent(target, event);
    if (status != ui::GESTURE_STATUS_UNKNOWN)
      return status;
  }

  status = target->delegate()->OnGestureEvent(event);
  if (status == ui::GESTURE_STATUS_UNKNOWN) {
    // The gesture was unprocessed. Generate corresponding mouse events here
    // (e.g. tap to click).
    switch (event->type()) {
      case ui::ET_GESTURE_TAP: {
        // Tap should be processed as a click. So generate the following
        // sequence of mouse events: MOUSE_ENTERED, MOUSE_PRESSED,
        // MOUSE_RELEASED and MOUSE_EXITED.
        ui::EventType types[] = { ui::ET_MOUSE_ENTERED,
                                  ui::ET_MOUSE_PRESSED,
                                  ui::ET_MOUSE_RELEASED,
                                  ui::ET_MOUSE_EXITED,
                                  ui::ET_UNKNOWN
                                };
        gesture_handler_ = target;
        for (ui::EventType* type = types; *type != ui::ET_UNKNOWN; ++type) {
          MouseEvent synth(
              *type, event->location(), event->root_location(), event->flags());
          if (gesture_handler_->delegate()->OnMouseEvent(&synth))
            status = ui::GESTURE_STATUS_SYNTH_MOUSE;
          // The window that was receiving the gestures may have closed/hidden
          // itself in response to one of the synthetic events. Stop sending
          // subsequent synthetic events if that happens.
          if (!gesture_handler_)
            break;
        }
        gesture_handler_ = NULL;
        break;
      }
      default:
        break;
    }
  }

  return status;
}

bool RootWindow::ProcessGestures(GestureRecognizer::Gestures* gestures) {
  if (!gestures)
    return false;
  bool handled = false;
  for (unsigned int i = 0; i < gestures->size(); i++) {
    GestureEvent* gesture = gestures->at(i).get();
    if (DispatchGestureEvent(gesture) != ui::GESTURE_STATUS_UNKNOWN)
      handled = true;
  }
  return handled;
}

void RootWindow::ScheduleDraw() {
  if (!schedule_paint_factory_.HasWeakPtrs()) {
    MessageLoop::current()->PostTask(
        FROM_HERE,
        base::Bind(&RootWindow::Draw, schedule_paint_factory_.GetWeakPtr()));
  }
}

bool RootWindow::CanFocus() const {
  return IsVisible();
}

bool RootWindow::CanReceiveEvents() const {
  return IsVisible();
}

internal::FocusManager* RootWindow::GetFocusManager() {
  return this;
}

RootWindow* RootWindow::GetRootWindow() {
  return this;
}

void RootWindow::OnWindowDetachingFromRootWindow(Window* detached) {
  DCHECK(capture_window_ != this);

  // If the ancestor of the capture window is detached,
  // release the capture.
  if (detached->Contains(capture_window_) && detached != this)
    ReleaseCapture(capture_window_);

  // If the ancestor of the focused window is detached,
  // release the focus.
  if (detached->Contains(focused_window_))
    SetFocusedWindow(NULL);

  // If the ancestor of any event handler windows are detached, release the
  // pointer to those windows.
  if (detached->Contains(mouse_pressed_handler_))
    mouse_pressed_handler_ = NULL;
  if (detached->Contains(mouse_moved_handler_))
    mouse_moved_handler_ = NULL;
  if (detached->Contains(touch_event_handler_))
    touch_event_handler_ = NULL;

  if (detached->IsVisible() &&
      detached->ContainsPointInRoot(last_mouse_location_)) {
    PostMouseMoveEventAfterWindowChange();
  }
}

void RootWindow::OnWindowAttachedToRootWindow(Window* attached) {
  if (attached->IsVisible() &&
      attached->ContainsPointInRoot(last_mouse_location_))
    PostMouseMoveEventAfterWindowChange();
}

void RootWindow::OnLayerAnimationEnded(
    const ui::LayerAnimationSequence* animation) {
  OnHostResized(host_->GetSize());
}

void RootWindow::OnLayerAnimationScheduled(
    const ui::LayerAnimationSequence* animation) {
}

void RootWindow::OnLayerAnimationAborted(
    const ui::LayerAnimationSequence* animation) {
}

void RootWindow::SetFocusedWindow(Window* focused_window) {
  if (focused_window == focused_window_)
    return;
  if (focused_window && !focused_window->CanFocus())
    return;
  // The NULL-check of |focused_window| is essential here before asking the
  // activation client, since it is valid to clear the focus by calling
  // SetFocusedWindow() to NULL.
  if (focused_window && client::GetActivationClient() &&
      !client::GetActivationClient()->CanFocusWindow(focused_window)) {
    return;
  }

  if (focused_window_ && focused_window_->delegate())
    focused_window_->delegate()->OnBlur();
  focused_window_ = focused_window;
  if (focused_window_ && focused_window_->delegate())
    focused_window_->delegate()->OnFocus();
  if (focused_window_) {
    FOR_EACH_OBSERVER(RootWindowObserver, observers_,
                      OnWindowFocused(focused_window_));
  }
}

Window* RootWindow::GetFocusedWindow() {
  return focused_window_;
}

bool RootWindow::IsFocusedWindow(const Window* window) const {
  return focused_window_ == window;
}

void RootWindow::Init() {
  Window::Init(ui::Layer::LAYER_NOT_DRAWN);
  SetBounds(gfx::Rect(host_->GetSize()));
  Show();
  compositor()->SetRootLayer(layer());
  host_->SetRootWindow(this);
}

gfx::Rect RootWindow::GetInitialHostWindowBounds() const {
  gfx::Rect bounds(kDefaultHostWindowX, kDefaultHostWindowY,
                   kDefaultHostWindowWidth, kDefaultHostWindowHeight);

  const string size_str = CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
      switches::kAuraHostWindowSize);
  vector<string> parts;
  base::SplitString(size_str, 'x', &parts);
  int parsed_width = 0, parsed_height = 0;
  if (parts.size() == 2 &&
      base::StringToInt(parts[0], &parsed_width) && parsed_width > 0 &&
      base::StringToInt(parts[1], &parsed_height) && parsed_height > 0) {
    bounds.set_size(gfx::Size(parsed_width, parsed_height));
  } else if (use_fullscreen_host_window_) {
    bounds = gfx::Rect(RootWindowHost::GetNativeScreenSize());
  }

  return bounds;
}

void RootWindow::PostMouseMoveEventAfterWindowChange() {
  if (synthesize_mouse_move_)
    return;
  synthesize_mouse_move_ = true;
  MessageLoop::current()->PostTask(
      FROM_HERE,
      base::Bind(&RootWindow::SynthesizeMouseMoveEvent,
                 event_factory_.GetWeakPtr()));
}

void RootWindow::SynthesizeMouseMoveEvent() {
  if (!synthesize_mouse_move_)
    return;
  synthesize_mouse_move_ = false;
  gfx::Point orig_mouse_location = last_mouse_location_;
  layer()->transform().TransformPoint(orig_mouse_location);

  // TODO(derat|oshima): Don't use mouse_button_flags_ as it's
  // is currently broken. See/ crbug.com/107931.
  MouseEvent event(ui::ET_MOUSE_MOVED,
                   orig_mouse_location,
                   orig_mouse_location,
                   0);
  DispatchMouseEvent(&event);
}

}  // namespace aura
