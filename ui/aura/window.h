// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_AURA_WINDOW_H_
#define UI_AURA_WINDOW_H_
#pragma once

#include <map>
#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "base/observer_list.h"
#include "base/string16.h"
#include "ui/base/events.h"
#include "ui/aura/aura_export.h"
#include "ui/aura/client/window_types.h"
#include "ui/gfx/compositor/layer.h"
#include "ui/gfx/compositor/layer_animator.h"
#include "ui/gfx/compositor/layer_delegate.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gfx/rect.h"

class SkCanvas;

namespace ui {
class Layer;
class Texture;
class Transform;
}

namespace aura {

class EventFilter;
class LayoutManager;
class RootWindow;
class WindowDelegate;
class WindowObserver;

namespace internal {
class FocusManager;
}

// Aura window implementation. Interesting events are sent to the
// WindowDelegate.
// TODO(beng): resolve ownership.
class AURA_EXPORT Window : public ui::LayerDelegate {
 public:
  typedef std::vector<Window*> Windows;

  class AURA_EXPORT TestApi {
   public:
    explicit TestApi(Window* window);

    bool OwnsLayer() const;

   private:
    TestApi();

    Window* window_;

    DISALLOW_COPY_AND_ASSIGN(TestApi);
  };

  explicit Window(WindowDelegate* delegate);
  virtual ~Window();

  void Init(ui::Layer::LayerType layer_type);

  // A type is used to identify a class of Windows and customize behavior such
  // as event handling and parenting.  This field should only be consumed by the
  // shell -- Aura itself shouldn't contain type-specific logic.
  client::WindowType type() const { return type_; }
  void SetType(client::WindowType type);

  int id() const { return id_; }
  void set_id(int id) { id_ = id; }

  const std::string& name() const { return name_; }
  void SetName(const std::string& name);

  const string16 title() const { return title_; }
  void set_title(const string16& title) { title_ = title; }

  bool transparent() const { return transparent_; }
  void SetTransparent(bool transparent);

  ui::Layer* layer() { return layer_; }
  const ui::Layer* layer() const { return layer_; }

  // Releases the Window's owning reference to its layer, and returns it.
  // This is used when you need to animate the presentation of the Window just
  // prior to destroying it. The window can be destroyed soon after calling this
  // function, and the caller is then responsible for disposing of the layer
  // once any animation completes. Note that layer() will remain valid until the
  // end of ~Window().
  ui::Layer* AcquireLayer();

  WindowDelegate* delegate() { return delegate_; }

  const gfx::Rect& bounds() const;

  Window* parent() { return parent_; }
  const Window* parent() const { return parent_; }

  // The Window does not own this object.
  void set_user_data(void* user_data) { user_data_ = user_data; }
  void* user_data() const { return user_data_; }

  // Changes the visibility of the window.
  void Show();
  void Hide();
  // Returns true if this window and all its ancestors are visible.
  bool IsVisible() const;

  // Returns the window's bounds in screen coordinates.
  gfx::Rect GetScreenBounds() const;

  // Returns true if this window is active.
  bool IsActive() const;

  virtual void SetTransform(const ui::Transform& transform);

  // Assigns a LayoutManager to size and place child windows.
  // The Window takes ownership of the LayoutManager.
  void SetLayoutManager(LayoutManager* layout_manager);
  LayoutManager* layout_manager() { return layout_manager_.get(); }

  // Changes the bounds of the window. If present, the window's parent's
  // LayoutManager may adjust the bounds.
  void SetBounds(const gfx::Rect& new_bounds);

  // Returns the target bounds of the window. If the window's layer is
  // not animating, it simply returns the current bounds.
  gfx::Rect GetTargetBounds() const;

  // Marks the a portion of window as needing to be painted.
  void SchedulePaintInRect(const gfx::Rect& rect);

  // Assigns a new external texture to the window's layer.
  void SetExternalTexture(ui::Texture* texture);

  // Sets the parent window of the window. If NULL, the window is parented to
  // the root window.
  void SetParent(Window* parent);

  // Stacks the specified child of this Window at the front of the z-order.
  void StackChildAtTop(Window* child);

  // Stacks |child| above |other|.  Does nothing if |child| is already above
  // |other|.
  void StackChildAbove(Window* child, Window* other);

  // Tree operations.
  // TODO(beng): Child windows are currently not owned by the hierarchy. We
  //             should change this.
  void AddChild(Window* child);
  void RemoveChild(Window* child);

  const Windows& children() const { return children_; }

  // Returns true if this Window contains |other| somewhere in its children.
  bool Contains(const Window* other) const;

  // Adds or removes |child| as a transient child of this window. Transient
  // children get the following behavior:
  // . The transient parent destroys any transient children when it is
  //   destroyed. This means a transient child is destroyed if either its parent
  //   or transient parent is destroyed.
  // . If a transient child and its transient parent share the same parent, then
  //   transient children are always ordered above the trasient parent.
  // Transient windows are typically used for popups and menus.
  void AddTransientChild(Window* child);
  void RemoveTransientChild(Window* child);

  const Windows& transient_children() const { return transient_children_; }

  Window* transient_parent() { return transient_parent_; }
  const Window* transient_parent() const { return transient_parent_; }

  // Retrieves the first-level child with the specified id, or NULL if no first-
  // level child is found matching |id|.
  Window* GetChildById(int id);
  const Window* GetChildById(int id) const;

  // Converts |point| from |source|'s coordinates to |target|'s. If |source| is
  // NULL, the function returns without modifying |point|. |target| cannot be
  // NULL.
  static void ConvertPointToWindow(const Window* source,
                                   const Window* target,
                                   gfx::Point* point);

  // Returns the cursor for the specified point, in window coordinates.
  gfx::NativeCursor GetCursor(const gfx::Point& point) const;

  // Window takes ownership of the EventFilter.
  void SetEventFilter(EventFilter* event_filter);
  EventFilter* event_filter() { return event_filter_.get(); }

  // Add/remove observer.
  void AddObserver(WindowObserver* observer);
  void RemoveObserver(WindowObserver* observer);

  // When set to true, this Window will stop propagation of all events targeted
  // at Windows below it in the z-order, but only if this Window has children.
  // This is used to implement lock-screen type functionality where we do not
  // want events to be sent to running logged-in windows when the lock screen is
  // displayed.
  void set_stops_event_propagation(bool stops_event_propagation) {
    stops_event_propagation_ = stops_event_propagation;
  }

  void set_ignore_events(bool ignore_events) { ignore_events_ = ignore_events; }

  // Returns true if the |point_in_root| in root window's coordinate falls
  // within this window's bounds. Returns false if the window is detached
  // from root window.
  bool ContainsPointInRoot(const gfx::Point& point_in_root);

  // Returns true if relative-to-this-Window's-origin |local_point| falls
  // within this Window's bounds.
  bool ContainsPoint(const gfx::Point& local_point);

  // Returns true if the mouse pointer at relative-to-this-Window's-origin
  // |local_point| can trigger an event for this Window.
  // TODO(beng): A Window can supply a hit-test mask to cause some portions of
  // itself to not trigger events, causing the events to fall through to the
  // Window behind.
  bool HitTest(const gfx::Point& local_point);

  // Returns the Window that most closely encloses |local_point| for the
  // purposes of event targeting.
  Window* GetEventHandlerForPoint(const gfx::Point& local_point);

  // Returns the topmost Window with a delegate containing |local_point|.
  Window* GetTopWindowContainingPoint(const gfx::Point& local_point);

  // Returns this window's toplevel window (the highest-up-the-tree anscestor
  // that has a delegate set).  The toplevel window may be |this|.
  Window* GetToplevelWindow();

  // Claims or relinquishes the claim to focus.
  void Focus();
  void Blur();

  // Returns true if the Window is currently the focused window.
  bool HasFocus() const;

  // Returns true if the Window can be focused.
  virtual bool CanFocus() const;

  // Returns true if the Window can receive events.
  virtual bool CanReceiveEvents() const;

  // Returns the FocusManager for the Window, which may be attached to a parent
  // Window. Can return NULL if the Window has no FocusManager.
  virtual internal::FocusManager* GetFocusManager();
  virtual const internal::FocusManager* GetFocusManager() const;

  // Does a mouse capture on the window. This does nothing if the window isn't
  // showing (VISIBILITY_SHOWN) or isn't contained in a valid window hierarchy.
  void SetCapture();

  // Releases a mouse capture.
  void ReleaseCapture();

  // Returns true if this window has a mouse capture.
  bool HasCapture();

  // Sets the window property |value| for given |name|. Setting NULL or 0
  // removes the property. It uses |ui::ViewProp| to store the property.
  // Please see the description of |prop_map_| for more details. The caller is
  // responsible for the lifetime of any object set as a property on the Window.
  void SetProperty(const char* name, void* value);
  void SetIntProperty(const char* name, int value);

  // Returns the window property for given |name|.  Returns NULL or 0 if
  // the property does not exist.
  // TODO(oshima): Returning 0 for non existing property is problematic.
  // Fix ViewProp to be able to tell if the property exists and
  // change it to -1.
  void* GetProperty(const char* name) const;
  int GetIntProperty(const char* name) const;

  // Returns true if this window is currently stopping event
  // propagation for any windows behind it in the z-order.
  bool StopsEventPropagation() const;

 protected:
  // Returns the root window or NULL if we aren't yet attached to the root
  // window.
  virtual RootWindow* GetRootWindow();

  // Called when the |window| is being detached from the root window
  // by being removed from its parent. It is called before |parent_| is
  // set to NULL.
  virtual void OnWindowDetachingFromRootWindow(aura::Window* window);

  // Called when the |window| is attached to the root window by being added
  // to its parent.
  virtual void OnWindowAttachedToRootWindow(aura::Window* window);

 private:
  friend class LayoutManager;

  // Changes the bounds of the window without condition.
  void SetBoundsInternal(const gfx::Rect& new_bounds);

  // Updates the visible state of the layer, but does not make visible-state
  // specific changes. Called from Show()/Hide().
  void SetVisible(bool visible);

  // Schedules a paint for the Window's entire bounds.
  void SchedulePaint();

  // Gets a Window (either this one or a subwindow) containing |local_point|.
  // If |return_tightest| is true, returns the tightest-containing (i.e.
  // furthest down the hierarchy) Window containing the point; otherwise,
  // returns the loosest.  If |for_event_handling| is true, then hit-test masks
  // and StopsEventPropagation() are honored; otherwise, only bounds checks are
  // performed.
  Window* GetWindowForPoint(const gfx::Point& local_point,
                            bool return_tightest,
                            bool for_event_handling);

  // Called when this window's parent has changed.
  void OnParentChanged();

  // Called when this window's stacking order among its siblings is changed.
  void OnStackingChanged();

  // Overridden from ui::LayerDelegate:
  virtual void OnPaintLayer(gfx::Canvas* canvas) OVERRIDE;

  // Updates the layer name with a name based on the window's name and id.
  void UpdateLayerName(const std::string& name);

  // Returns true if this window is behind a window that stops event
  // propagation.
  bool IsBehindStopEventsWindow() const;

  client::WindowType type_;

  WindowDelegate* delegate_;

  // The Window will own its layer unless ownership is relinquished via a call
  // to AcquireLayer(). After that moment |layer_| will still be valid but
  // |layer_owner_| will be NULL. The reason for releasing ownership is that
  // the client may wish to animate the window's layer beyond the lifetime of
  // the window, e.g. fading it out when it is destroyed.
  scoped_ptr<ui::Layer> layer_owner_;
  ui::Layer* layer_;

  // The Window's parent.
  Window* parent_;

  // Child windows. Topmost is last.
  Windows children_;

  // Transient windows.
  Windows transient_children_;

  Window* transient_parent_;

  // The visibility state of the window as set by Show()/Hide(). This may differ
  // from the visibility of the underlying layer, which may remain visible after
  // the window is hidden (e.g. to animate its disappearance).
  bool visible_;

  int id_;
  std::string name_;

  string16 title_;

  // Whether layer is initialized as non-opaque.
  bool transparent_;

  scoped_ptr<EventFilter> event_filter_;
  scoped_ptr<LayoutManager> layout_manager_;

  void* user_data_;

  // When true, events are not sent to windows behind this one in the z-order,
  // provided this window has children. See set_stops_event_propagation().
  bool stops_event_propagation_;

  // Makes the window pass all events through to any windows behind it.
  bool ignore_events_;

  ObserverList<WindowObserver> observers_;

  // We're using ViewProp to store the property (for now) instead of
  // just using std::map because chrome is still using |ViewProp| class
  // to create and access property.
  // TODO(oshima): Consolidcate ViewProp and aura::window property
  // implementation.
  std::map<const char*, void*> prop_map_;

  DISALLOW_COPY_AND_ASSIGN(Window);
};

}  // namespace aura

#endif  // UI_AURA_WINDOW_H_
