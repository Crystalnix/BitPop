// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/aura/window.h"

#include <algorithm>

#include "base/logging.h"
#include "base/stl_util.h"
#include "base/string_util.h"
#include "ui/aura/client/stacking_client.h"
#include "ui/aura/client/visibility_client.h"
#include "ui/aura/event.h"
#include "ui/aura/event_filter.h"
#include "ui/aura/layout_manager.h"
#include "ui/aura/root_window.h"
#include "ui/aura/window_delegate.h"
#include "ui/aura/window_observer.h"
#include "ui/base/animation/multi_animation.h"
#include "ui/gfx/canvas_skia.h"
#include "ui/gfx/compositor/compositor.h"
#include "ui/gfx/screen.h"

namespace aura {

namespace {

Window* GetParentForWindow(Window* window, Window* suggested_parent) {
  if (suggested_parent)
    return suggested_parent;
  if (client::GetStackingClient())
    return client::GetStackingClient()->GetDefaultParent(window);
  return RootWindow::GetInstance();
}

}  // namespace

Window::TestApi::TestApi(Window* window) : window_(window) {}

bool Window::TestApi::OwnsLayer() const {
  return !!window_->layer_owner_.get();
}

Window::Window(WindowDelegate* delegate)
    : type_(client::WINDOW_TYPE_UNKNOWN),
      delegate_(delegate),
      layer_(NULL),
      parent_(NULL),
      transient_parent_(NULL),
      visible_(false),
      id_(-1),
      transparent_(false),
      user_data_(NULL),
      stops_event_propagation_(false),
      ignore_events_(false) {
}

Window::~Window() {
  // Let the delegate know we're in the processing of destroying.
  if (delegate_)
    delegate_->OnWindowDestroying();
  FOR_EACH_OBSERVER(WindowObserver, observers_, OnWindowDestroying(this));

  // Let the root know so that it can remove any references to us.
  RootWindow* root_window = GetRootWindow();
  if (root_window)
    root_window->OnWindowDestroying(this);

  // Then destroy the children.
  while (!children_.empty()) {
    Window* child = children_[0];
    delete child;
    // Deleting the child so remove it from out children_ list.
    DCHECK(std::find(children_.begin(), children_.end(), child) ==
           children_.end());
  }

  // Removes ourselves from our transient parent (if it hasn't been done by the
  // RootWindow).
  if (transient_parent_)
    transient_parent_->RemoveTransientChild(this);

  // The window needs to be removed from the parent before calling the
  // WindowDestroyed callbacks of delegate and the observers.
  if (parent_)
    parent_->RemoveChild(this);

  // And let the delegate do any post cleanup.
  // TODO(beng): Figure out if this notification needs to happen here, or if it
  // can be moved down adjacent to the observer notification. If it has to be
  // done here, the reason why should be documented.
  if (delegate_)
    delegate_->OnWindowDestroyed();

  // Destroy transient children, only after we've removed ourselves from our
  // parent, as destroying an active transient child may otherwise attempt to
  // refocus us.
  Windows transient_children(transient_children_);
  STLDeleteElements(&transient_children);
  DCHECK(transient_children_.empty());

  FOR_EACH_OBSERVER(WindowObserver, observers_, OnWindowDestroyed(this));

  // If we have layer it will either be destroyed by layer_owner_'s dtor, or by
  // whoever acquired it. We don't have a layer if Init() wasn't invoked, which
  // can happen in tests.
  if (layer_)
    layer_->set_delegate(NULL);
  layer_ = NULL;
}

void Window::Init(ui::Layer::LayerType layer_type) {
  layer_ = new ui::Layer(layer_type);
  layer_owner_.reset(layer_);
  layer_->SetVisible(false);
  layer_->set_delegate(this);
  UpdateLayerName(name_);
  layer_->SetFillsBoundsOpaquely(!transparent_);

  RootWindow::GetInstance()->OnWindowInitialized(this);
}

void Window::SetType(client::WindowType type) {
  // Cannot change type after the window is initialized.
  DCHECK(!layer());
  type_ = type;
}

void Window::SetName(const std::string& name) {
  name_ = name;

  if (layer())
    UpdateLayerName(name_);
}

void Window::SetTransparent(bool transparent) {
  // Cannot change transparent flag after the window is initialized.
  DCHECK(!layer());
  transparent_ = transparent;
}

ui::Layer* Window::AcquireLayer() {
  return layer_owner_.release();
}

void Window::Show() {
  SetVisible(true);
}

void Window::Hide() {
  SetVisible(false);
  ReleaseCapture();
}

bool Window::IsVisible() const {
  // Layer visibility can be inconsistent with window visibility, for example
  // when a Window is hidden, we want this function to return false immediately
  // after, even though the client may decide to animate the hide effect (and
  // so the layer will be visible for some time after Hide() is called).
  return visible_ && layer_ && layer_->IsDrawn();
}

gfx::Rect Window::GetScreenBounds() const {
  gfx::Point origin = bounds().origin();
  Window::ConvertPointToWindow(parent_,
                               aura::RootWindow::GetInstance(),
                               &origin);
  return gfx::Rect(origin, bounds().size());
}

void Window::SetTransform(const ui::Transform& transform) {
  RootWindow* root_window = GetRootWindow();
  bool contained_mouse = IsVisible() && root_window &&
      ContainsPointInRoot(root_window->last_mouse_location());
  layer()->SetTransform(transform);
  if (root_window)
    root_window->OnWindowTransformed(this, contained_mouse);
}

void Window::SetLayoutManager(LayoutManager* layout_manager) {
  if (layout_manager == layout_manager_.get())
    return;
  layout_manager_.reset(layout_manager);
  if (!layout_manager)
    return;
  // If we're changing to a new layout manager, ensure it is aware of all the
  // existing child windows.
  for (Windows::const_iterator it = children_.begin();
       it != children_.end();
       ++it)
    layout_manager_->OnWindowAddedToLayout(*it);
}

void Window::SetBounds(const gfx::Rect& new_bounds) {
  if (parent_ && parent_->layout_manager())
    parent_->layout_manager()->SetChildBounds(this, new_bounds);
  else
    SetBoundsInternal(new_bounds);
}

gfx::Rect Window::GetTargetBounds() const {
  return layer_->GetTargetBounds();
}

const gfx::Rect& Window::bounds() const {
  return layer_->bounds();
}

void Window::SchedulePaintInRect(const gfx::Rect& rect) {
  layer_->SchedulePaint(rect);
  FOR_EACH_OBSERVER(
      WindowObserver, observers_, OnWindowPaintScheduled(this, rect));
}

void Window::SetExternalTexture(ui::Texture* texture) {
  layer_->SetExternalTexture(texture);
  gfx::Rect region(gfx::Point(), bounds().size());
  FOR_EACH_OBSERVER(
      WindowObserver, observers_, OnWindowPaintScheduled(this, region));
}

void Window::SetParent(Window* parent) {
  GetParentForWindow(this, parent)->AddChild(this);
}

void Window::StackChildAtTop(Window* child) {
  if (children_.size() <= 1 || child == children_.back())
    return;  // In the front already.
  StackChildAbove(child, children_.back());
}

void Window::StackChildAbove(Window* child, Window* other) {
  DCHECK_NE(child, other);
  DCHECK(child);
  DCHECK(other);
  DCHECK_EQ(this, child->parent());
  DCHECK_EQ(this, other->parent());

  const size_t child_i =
      std::find(children_.begin(), children_.end(), child) - children_.begin();
  const size_t other_i =
      std::find(children_.begin(), children_.end(), other) - children_.begin();
  if (child_i == other_i + 1)
    return;

  const size_t dest_i = child_i < other_i ? other_i : other_i + 1;
  children_.erase(children_.begin() + child_i);
  children_.insert(children_.begin() + dest_i, child);

  // See test WindowTest.StackingMadrigal for an explanation of this and the
  // check below in the transient loop.
  if (other->layer()->delegate())
    layer()->StackAbove(child->layer(), other->layer());

  // Stack any transient children that share the same parent to be in front of
  // 'child'.
  Window* last_transient = child;
  for (Windows::iterator i = child->transient_children_.begin();
       i != child->transient_children_.end(); ++i) {
    Window* transient_child = *i;
    if (transient_child->parent_ == this) {
      StackChildAbove(transient_child, last_transient);
      if (transient_child->layer()->delegate())
        last_transient = transient_child;
    }
  }

  child->OnStackingChanged();
}

void Window::AddChild(Window* child) {
  DCHECK(std::find(children_.begin(), children_.end(), child) ==
      children_.end());
  if (child->parent())
    child->parent()->RemoveChild(child);
  child->parent_ = this;

  layer_->Add(child->layer_);

  children_.push_back(child);
  if (layout_manager_.get())
    layout_manager_->OnWindowAddedToLayout(child);
  FOR_EACH_OBSERVER(WindowObserver, observers_, OnWindowAdded(child));
  child->OnParentChanged();

  RootWindow* root_window = child->GetRootWindow();
  if (root_window)
    root_window->OnWindowAttachedToRootWindow(child);
}

void Window::AddTransientChild(Window* child) {
  if (child->transient_parent_)
    child->transient_parent_->RemoveTransientChild(child);
  DCHECK(std::find(transient_children_.begin(), transient_children_.end(),
                   child) == transient_children_.end());
  transient_children_.push_back(child);
  child->transient_parent_ = this;
}

void Window::RemoveTransientChild(Window* child) {
  Windows::iterator i =
      std::find(transient_children_.begin(), transient_children_.end(), child);
  DCHECK(i != transient_children_.end());
  transient_children_.erase(i);
  if (child->transient_parent_ == this)
    child->transient_parent_ = NULL;
}

void Window::RemoveChild(Window* child) {
  Windows::iterator i = std::find(children_.begin(), children_.end(), child);
  DCHECK(i != children_.end());
  if (layout_manager_.get())
    layout_manager_->OnWillRemoveWindowFromLayout(child);
  FOR_EACH_OBSERVER(WindowObserver, observers_, OnWillRemoveWindow(child));
  RootWindow* root_window = child->GetRootWindow();
  if (root_window)
    root_window->OnWindowDetachingFromRootWindow(child);
  child->parent_ = NULL;
  // We should only remove the child's layer if the child still owns that layer.
  // Someone else may have acquired ownership of it via AcquireLayer() and may
  // expect the hierarchy to go unchanged as the Window is destroyed.
  if (child->layer_owner_.get())
    layer_->Remove(child->layer_);
  children_.erase(i);
  child->OnParentChanged();
}

bool Window::Contains(const Window* other) const {
  for (const Window* parent = other; parent; parent = parent->parent_) {
    if (parent == this)
      return true;
  }
  return false;
}

Window* Window::GetChildById(int id) {
  return const_cast<Window*>(const_cast<const Window*>(this)->GetChildById(id));
}

const Window* Window::GetChildById(int id) const {
  Windows::const_iterator i;
  for (i = children_.begin(); i != children_.end(); ++i) {
    if ((*i)->id() == id)
      return *i;
    const Window* result = (*i)->GetChildById(id);
    if (result)
      return result;
  }
  return NULL;
}

// static
void Window::ConvertPointToWindow(const Window* source,
                                  const Window* target,
                                  gfx::Point* point) {
  if (!source)
    return;
  ui::Layer::ConvertPointToLayer(source->layer(), target->layer(), point);
}

gfx::NativeCursor Window::GetCursor(const gfx::Point& point) const {
  return delegate_ ? delegate_->GetCursor(point) : gfx::kNullCursor;
}

void Window::SetEventFilter(EventFilter* event_filter) {
  event_filter_.reset(event_filter);
}

void Window::AddObserver(WindowObserver* observer) {
  observers_.AddObserver(observer);
}

void Window::RemoveObserver(WindowObserver* observer) {
  observers_.RemoveObserver(observer);
}

bool Window::ContainsPointInRoot(const gfx::Point& point_in_root) {
  Window* root_window = GetRootWindow();
  if (!root_window)
    return false;
  gfx::Point local_point(point_in_root);
  ConvertPointToWindow(root_window, this, &local_point);
  return GetTargetBounds().Contains(local_point);
}

bool Window::ContainsPoint(const gfx::Point& local_point) {
  gfx::Rect local_bounds(gfx::Point(), bounds().size());
  return local_bounds.Contains(local_point);
}

bool Window::HitTest(const gfx::Point& local_point) {
  // TODO(beng): hittest masks.
  return ContainsPoint(local_point);
}

Window* Window::GetEventHandlerForPoint(const gfx::Point& local_point) {
  return GetWindowForPoint(local_point, true, true);
}

Window* Window::GetTopWindowContainingPoint(const gfx::Point& local_point) {
  return GetWindowForPoint(local_point, false, false);
}

Window* Window::GetToplevelWindow() {
  Window* topmost_window_with_delegate = NULL;
  for (aura::Window* window = this; window != NULL; window = window->parent()) {
    if (window->delegate())
      topmost_window_with_delegate = window;
  }
  return topmost_window_with_delegate;
}

void Window::Focus() {
  DCHECK(GetFocusManager());
  GetFocusManager()->SetFocusedWindow(this);
}

void Window::Blur() {
  DCHECK(GetFocusManager());
  GetFocusManager()->SetFocusedWindow(NULL);
}

bool Window::HasFocus() const {
  const internal::FocusManager* focus_manager = GetFocusManager();
  return focus_manager ? focus_manager->IsFocusedWindow(this) : false;
}

// For a given window, we determine its focusability and ability to
// receive events by inspecting each sibling after it (i.e. drawn in
// front of it in the z-order) to see if it stops propagation of
// events that would otherwise be targeted at windows behind it.  We
// then perform this same check on every window up to the root.
bool Window::CanFocus() const {
  if (!IsVisible() || !parent_ || (delegate_ && !delegate_->CanFocus()))
    return false;
  return !IsBehindStopEventsWindow() && parent_->CanFocus();
}

bool Window::CanReceiveEvents() const {
  return parent_ && IsVisible() && !IsBehindStopEventsWindow() &&
      parent_->CanReceiveEvents();
}

internal::FocusManager* Window::GetFocusManager() {
  return const_cast<internal::FocusManager*>(
      static_cast<const Window*>(this)->GetFocusManager());
}

const internal::FocusManager* Window::GetFocusManager() const {
  return parent_ ? parent_->GetFocusManager() : NULL;
}

void Window::SetCapture() {
  if (!IsVisible())
    return;

  RootWindow* root_window = GetRootWindow();
  if (!root_window)
    return;

  root_window->SetCapture(this);
}

void Window::ReleaseCapture() {
  RootWindow* root_window = GetRootWindow();
  if (!root_window)
    return;

  root_window->ReleaseCapture(this);
}

bool Window::HasCapture() {
  RootWindow* root_window = GetRootWindow();
  return root_window && root_window->capture_window() == this;
}

void Window::SetProperty(const char* name, void* value) {
  void* old = GetProperty(name);
  if (value)
    prop_map_[name] = value;
  else
    prop_map_.erase(name);
  FOR_EACH_OBSERVER(
      WindowObserver, observers_, OnWindowPropertyChanged(this, name, old));
}

void Window::SetIntProperty(const char* name, int value) {
  SetProperty(name, reinterpret_cast<void*>(value));
}

void* Window::GetProperty(const char* name) const {
  std::map<const char*, void*>::const_iterator iter = prop_map_.find(name);
  if (iter == prop_map_.end())
    return NULL;
  return iter->second;
}

int Window::GetIntProperty(const char* name) const {
  return static_cast<int>(reinterpret_cast<intptr_t>(
      GetProperty(name)));
}

bool Window::StopsEventPropagation() const {
  if (!stops_event_propagation_ || children_.empty())
    return false;
  aura::Window::Windows::const_iterator it =
      std::find_if(children_.begin(), children_.end(),
                   std::mem_fun(&aura::Window::IsVisible));
  return it != children_.end();
}

RootWindow* Window::GetRootWindow() {
  return parent_ ? parent_->GetRootWindow() : NULL;
}

void Window::OnWindowDetachingFromRootWindow(aura::Window* window) {
}

void Window::OnWindowAttachedToRootWindow(aura::Window* window) {
}

void Window::SetBoundsInternal(const gfx::Rect& new_bounds) {
  gfx::Rect actual_new_bounds(new_bounds);

  // Ensure we don't go smaller than our minimum bounds.
  if (delegate_) {
    const gfx::Size& min_size = delegate_->GetMinimumSize();
    actual_new_bounds.set_width(
        std::max(min_size.width(), actual_new_bounds.width()));
    actual_new_bounds.set_height(
        std::max(min_size.height(), actual_new_bounds.height()));
  }
  RootWindow* root_window = GetRootWindow();

  bool contained_mouse =
      IsVisible() &&
      root_window && ContainsPointInRoot(root_window->last_mouse_location());

  const gfx::Rect old_bounds = layer_->GetTargetBounds();

  // Always need to set the layer's bounds -- even if it is to the same thing.
  // This may cause important side effects such as stopping animation.
  layer_->SetBounds(actual_new_bounds);

  // If we're not changing the effective bounds, then we can bail early and skip
  // notifying our listeners.
  if (old_bounds == actual_new_bounds)
    return;

  if (layout_manager_.get())
    layout_manager_->OnWindowResized();
  if (delegate_)
    delegate_->OnBoundsChanged(old_bounds, actual_new_bounds);
  FOR_EACH_OBSERVER(WindowObserver,
                    observers_,
                    OnWindowBoundsChanged(this, actual_new_bounds));

  if (root_window)
    root_window->OnWindowBoundsChanged(this, contained_mouse);
}

void Window::SetVisible(bool visible) {
  if (visible == layer_->visible())
    return;  // No change.

  bool was_visible = IsVisible();
  if (visible != layer_->visible()) {
    if (client::GetVisibilityClient())
      client::GetVisibilityClient()->UpdateLayerVisibility(this, visible);
    else
      layer_->SetVisible(visible);
  }
  visible_ = visible;
  bool is_visible = IsVisible();
  if (was_visible != is_visible) {
    if (is_visible)
      SchedulePaint();
    if (delegate_)
      delegate_->OnWindowVisibilityChanged(is_visible);
  }

  if (parent_ && parent_->layout_manager_.get())
    parent_->layout_manager_->OnChildWindowVisibilityChanged(this, visible);
  FOR_EACH_OBSERVER(WindowObserver, observers_,
                    OnWindowVisibilityChanged(this, visible));

  RootWindow* root_window = GetRootWindow();
  if (root_window)
    root_window->OnWindowVisibilityChanged(this, visible);
}

void Window::SchedulePaint() {
  SchedulePaintInRect(gfx::Rect(0, 0, bounds().width(), bounds().height()));
}

Window* Window::GetWindowForPoint(const gfx::Point& local_point,
                                  bool return_tightest,
                                  bool for_event_handling) {
  if (!IsVisible())
    return NULL;

  if ((for_event_handling && !HitTest(local_point)) ||
      (!for_event_handling && !ContainsPoint(local_point)))
    return NULL;

  if (!return_tightest && delegate_)
    return this;

  for (Windows::const_reverse_iterator it = children_.rbegin();
       it != children_.rend(); ++it) {
    Window* child = *it;
    if (!child->IsVisible() || (for_event_handling && child->ignore_events_))
      continue;

    gfx::Point point_in_child_coords(local_point);
    Window::ConvertPointToWindow(this, child, &point_in_child_coords);
    Window* match = child->GetWindowForPoint(point_in_child_coords,
                                             return_tightest,
                                             for_event_handling);
    if (match)
      return match;

    if (for_event_handling && child->StopsEventPropagation())
      break;
  }

  return delegate_ ? this : NULL;
}

void Window::OnParentChanged() {
  FOR_EACH_OBSERVER(
      WindowObserver, observers_, OnWindowParentChanged(this, parent_));
}

void Window::OnStackingChanged() {
  FOR_EACH_OBSERVER(WindowObserver, observers_, OnWindowStackingChanged(this));
}

void Window::OnPaintLayer(gfx::Canvas* canvas) {
  if (delegate_)
    delegate_->OnPaint(canvas);
}

void Window::UpdateLayerName(const std::string& name) {
#if !defined(NDEBUG)
  DCHECK(layer());

  std::string layer_name(name_);
  if (layer_name.empty())
    layer_name.append("Unnamed Window");

  if (id_ != -1) {
    char id_buf[10];
    base::snprintf(id_buf, sizeof(id_buf), " %d", id_);
    layer_name.append(id_buf);
  }
  layer()->set_name(layer_name);
#endif
}

bool Window::IsBehindStopEventsWindow() const {
  Windows::const_iterator i = std::find(parent_->children().begin(),
                                        parent_->children().end(),
                                        this);
  for (++i; i != parent_->children().end(); ++i) {
    if ((*i)->StopsEventPropagation())
      return true;
  }
  return false;
}

}  // namespace aura
