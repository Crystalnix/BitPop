// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_RENDER_WIDGET_HOST_VIEW_AURA_H_
#define CONTENT_BROWSER_RENDERER_HOST_RENDER_WIDGET_HOST_VIEW_AURA_H_

#include <map>
#include <vector>

#include "base/callback.h"
#include "base/memory/linked_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "content/browser/renderer_host/image_transport_factory.h"
#include "content/browser/renderer_host/render_widget_host_view_base.h"
#include "content/common/content_export.h"
#include "ui/aura/client/activation_delegate.h"
#include "ui/aura/window_delegate.h"
#include "ui/base/ime/text_input_client.h"
#include "ui/compositor/compositor_observer.h"
#include "ui/gfx/rect.h"
#include "webkit/glue/webcursor.h"

namespace aura {
class CompositorLock;
}

namespace gfx {
class Canvas;
}

namespace ui {
class InputMethod;
class Texture;
}

namespace WebKit {
class WebTouchEvent;
}

namespace content {
class RenderWidgetHostImpl;
class RenderWidgetHostView;

// RenderWidgetHostView class hierarchy described in render_widget_host_view.h.
class RenderWidgetHostViewAura
    : public RenderWidgetHostViewBase,
      public ui::CompositorObserver,
      public ui::TextInputClient,
      public aura::WindowDelegate,
      public aura::client::ActivationDelegate,
      public ImageTransportFactoryObserver,
      public base::SupportsWeakPtr<RenderWidgetHostViewAura> {
 public:
  // RenderWidgetHostView implementation.
  virtual void InitAsChild(gfx::NativeView parent_view) OVERRIDE;
  virtual RenderWidgetHost* GetRenderWidgetHost() const OVERRIDE;
  virtual void SetSize(const gfx::Size& size) OVERRIDE;
  virtual void SetBounds(const gfx::Rect& rect) OVERRIDE;
  virtual gfx::NativeView GetNativeView() const OVERRIDE;
  virtual gfx::NativeViewId GetNativeViewId() const OVERRIDE;
  virtual gfx::NativeViewAccessible GetNativeViewAccessible() OVERRIDE;
  virtual bool HasFocus() const OVERRIDE;
  virtual bool IsSurfaceAvailableForCopy() const OVERRIDE;
  virtual void Show() OVERRIDE;
  virtual void Hide() OVERRIDE;
  virtual bool IsShowing() OVERRIDE;
  virtual gfx::Rect GetViewBounds() const OVERRIDE;
  virtual void SetBackground(const SkBitmap& background) OVERRIDE;

  // Overridden from RenderWidgetHostViewPort:
  virtual void InitAsPopup(RenderWidgetHostView* parent_host_view,
                           const gfx::Rect& pos) OVERRIDE;
  virtual void InitAsFullscreen(
      RenderWidgetHostView* reference_host_view) OVERRIDE;
  virtual void WasShown() OVERRIDE;
  virtual void WasHidden() OVERRIDE;
  virtual void MovePluginWindows(
      const std::vector<webkit::npapi::WebPluginGeometry>& moves) OVERRIDE;
  virtual void Focus() OVERRIDE;
  virtual void Blur() OVERRIDE;
  virtual void UpdateCursor(const WebCursor& cursor) OVERRIDE;
  virtual void SetIsLoading(bool is_loading) OVERRIDE;
  virtual void TextInputStateChanged(ui::TextInputType type,
                                     bool can_compose_inline) OVERRIDE;
  virtual void ImeCancelComposition() OVERRIDE;
  virtual void ImeCompositionRangeChanged(
      const ui::Range& range,
      const std::vector<gfx::Rect>& character_bounds) OVERRIDE;
  virtual void DidUpdateBackingStore(
      const gfx::Rect& scroll_rect, int scroll_dx, int scroll_dy,
      const std::vector<gfx::Rect>& copy_rects) OVERRIDE;
  virtual void RenderViewGone(base::TerminationStatus status,
                              int error_code) OVERRIDE;
  virtual void Destroy() OVERRIDE;
  virtual void SetTooltipText(const string16& tooltip_text) OVERRIDE;
  virtual void SelectionBoundsChanged(const gfx::Rect& start_rect,
                                      const gfx::Rect& end_rect) OVERRIDE;
  virtual BackingStore* AllocBackingStore(const gfx::Size& size) OVERRIDE;
  virtual void CopyFromCompositingSurface(
      const gfx::Rect& src_subrect,
      const gfx::Size& dst_size,
      const base::Callback<void(bool)>& callback,
      skia::PlatformCanvas* output) OVERRIDE;
  virtual void OnAcceleratedCompositingStateChange() OVERRIDE;
  virtual void AcceleratedSurfaceBuffersSwapped(
      const GpuHostMsg_AcceleratedSurfaceBuffersSwapped_Params& params_in_pixel,
      int gpu_host_id) OVERRIDE;
  virtual void AcceleratedSurfacePostSubBuffer(
      const GpuHostMsg_AcceleratedSurfacePostSubBuffer_Params& params_in_pixel,
      int gpu_host_id) OVERRIDE;
  virtual void AcceleratedSurfaceSuspend() OVERRIDE;
  virtual bool HasAcceleratedSurface(const gfx::Size& desired_size) OVERRIDE;
  virtual void AcceleratedSurfaceNew(
      int32 width_in_pixel,
      int32 height_in_pixel,
      uint64 surface_id) OVERRIDE;
  virtual void AcceleratedSurfaceRelease(uint64 surface_id) OVERRIDE;
  virtual void GetScreenInfo(WebKit::WebScreenInfo* results) OVERRIDE;
  virtual gfx::Rect GetBoundsInRootWindow() OVERRIDE;
  virtual void ProcessTouchAck(WebKit::WebInputEvent::Type type,
                               bool processed) OVERRIDE;
  virtual void SetHasHorizontalScrollbar(
      bool has_horizontal_scrollbar) OVERRIDE;
  virtual void SetScrollOffsetPinning(
      bool is_pinned_to_left, bool is_pinned_to_right) OVERRIDE;
  virtual gfx::GLSurfaceHandle GetCompositingSurface() OVERRIDE;
  virtual bool LockMouse() OVERRIDE;
  virtual void UnlockMouse() OVERRIDE;

  // Overridden from ui::TextInputClient:
  virtual void SetCompositionText(
      const ui::CompositionText& composition) OVERRIDE;
  virtual void ConfirmCompositionText() OVERRIDE;
  virtual void ClearCompositionText() OVERRIDE;
  virtual void InsertText(const string16& text) OVERRIDE;
  virtual void InsertChar(char16 ch, int flags) OVERRIDE;
  virtual ui::TextInputType GetTextInputType() const OVERRIDE;
  virtual bool CanComposeInline() const OVERRIDE;
  virtual gfx::Rect GetCaretBounds() OVERRIDE;
  virtual bool GetCompositionCharacterBounds(uint32 index,
                                             gfx::Rect* rect) OVERRIDE;
  virtual bool HasCompositionText() OVERRIDE;
  virtual bool GetTextRange(ui::Range* range) OVERRIDE;
  virtual bool GetCompositionTextRange(ui::Range* range) OVERRIDE;
  virtual bool GetSelectionRange(ui::Range* range) OVERRIDE;
  virtual bool SetSelectionRange(const ui::Range& range) OVERRIDE;
  virtual bool DeleteRange(const ui::Range& range) OVERRIDE;
  virtual bool GetTextFromRange(const ui::Range& range,
                                string16* text) OVERRIDE;
  virtual void OnInputMethodChanged() OVERRIDE;
  virtual bool ChangeTextDirectionAndLayoutAlignment(
      base::i18n::TextDirection direction) OVERRIDE;

  // Overridden from aura::WindowDelegate:
  virtual gfx::Size GetMinimumSize() const OVERRIDE;
  virtual void OnBoundsChanged(const gfx::Rect& old_bounds,
                               const gfx::Rect& new_bounds) OVERRIDE;
  virtual void OnFocus(aura::Window* old_focused_window) OVERRIDE;
  virtual void OnBlur() OVERRIDE;
  virtual bool OnKeyEvent(aura::KeyEvent* event) OVERRIDE;
  virtual gfx::NativeCursor GetCursor(const gfx::Point& point) OVERRIDE;
  virtual int GetNonClientComponent(const gfx::Point& point) const OVERRIDE;
  virtual bool ShouldDescendIntoChildForEventHandling(
      aura::Window* child,
      const gfx::Point& location) OVERRIDE;
  virtual bool OnMouseEvent(aura::MouseEvent* event) OVERRIDE;
  virtual ui::TouchStatus OnTouchEvent(aura::TouchEvent* event) OVERRIDE;
  virtual ui::GestureStatus OnGestureEvent(aura::GestureEvent* event) OVERRIDE;
  virtual bool CanFocus() OVERRIDE;
  virtual void OnCaptureLost() OVERRIDE;
  virtual void OnPaint(gfx::Canvas* canvas) OVERRIDE;
  virtual void OnDeviceScaleFactorChanged(float device_scale_factor) OVERRIDE;
  virtual void OnWindowDestroying() OVERRIDE;
  virtual void OnWindowDestroyed() OVERRIDE;
  virtual void OnWindowTargetVisibilityChanged(bool visible) OVERRIDE;
  virtual bool HasHitTestMask() const OVERRIDE;
  virtual void GetHitTestMask(gfx::Path* mask) const OVERRIDE;

  // Overridden from aura::client::ActivationDelegate:
  virtual bool ShouldActivate(const aura::Event* event) OVERRIDE;
  virtual void OnActivated() OVERRIDE;
  virtual void OnLostActive() OVERRIDE;

 protected:
  friend class RenderWidgetHostView;

  // Should construct only via RenderWidgetHostView::CreateViewForWidget.
  explicit RenderWidgetHostViewAura(RenderWidgetHost* host);

 private:
  class WindowObserver;
  friend class WindowObserver;

  // Overridden from ui::CompositorObserver:
  virtual void OnCompositingDidCommit(ui::Compositor* compositor) OVERRIDE;
  virtual void OnCompositingWillStart(ui::Compositor* compositor) OVERRIDE;
  virtual void OnCompositingStarted(ui::Compositor* compositor) OVERRIDE;
  virtual void OnCompositingEnded(ui::Compositor* compositor) OVERRIDE;
  virtual void OnCompositingAborted(ui::Compositor* compositor) OVERRIDE;

  // Overridden from ImageTransportFactoryObserver:
  virtual void OnLostResources(ui::Compositor* compositor) OVERRIDE;

  virtual ~RenderWidgetHostViewAura();

  void UpdateCursorIfOverSelf();
  void UpdateExternalTexture();
  ui::InputMethod* GetInputMethod() const;

  // Returns whether the widget needs an input grab to work properly.
  bool NeedsInputGrab();

  // Confirm existing composition text in the webpage and ask the input method
  // to cancel its ongoing composition session.
  void FinishImeCompositionSession();

  // This method computes movementX/Y and keeps track of mouse location for
  // mouse lock on all mouse move events.
  void ModifyEventMovementAndCoords(WebKit::WebMouseEvent* event);

  // If |clip| is non-empty and and doesn't contain |rect| or |clip| is empty
  // SchedulePaint() is invoked for |rect|.
  void SchedulePaintIfNotInClip(const gfx::Rect& rect, const gfx::Rect& clip);

  // Helper method to determine if, in mouse locked mode, the cursor should be
  // moved to center.
  bool ShouldMoveToCenter();

  // Run the compositing callbacks.
  void RunCompositingDidCommitCallbacks(ui::Compositor* compositor);
  void RunCompositingWillStartCallbacks(ui::Compositor* compositor);

  // Insert a sync point into the compositor's command stream and acknowledge
  // that we have presented the accelerated surface buffer.
  static void InsertSyncPointAndACK(int32 route_id,
                                    int gpu_host_id,
                                    ui::Compositor* compositor);

  // Called when window_ is removed from the window tree.
  void RemovingFromRootWindow();

  // After clearing |current_surface_|, and waiting for the compositor to finish
  // using it, call this to inform the gpu process.
  void SetSurfaceNotInUseByCompositor(ui::Compositor* compositor);

  // This is called every time |current_surface_| usage changes (by thumbnailer,
  // compositor draws, and tab visibility). Every time usage of current surface
  // changes between "may be used" and "certain to not be used" by the ui, we
  // inform the gpu process.
  void AdjustSurfaceProtection();

  // Called after async thumbnailer task completes.  Used to call
  // AdjustSurfaceProtection.
  void CopyFromCompositingSurfaceFinished(base::Callback<void(bool)> callback,
                                          bool result);

  ui::Compositor* GetCompositor();

  // Detaches |this| from the input method object.
  void DetachFromInputMethod();

  // Converts |rect| from window coordinate to screen coordinate.
  gfx::Rect ConvertRectToScreen(const gfx::Rect& rect);

  // The model object.
  RenderWidgetHostImpl* host_;

  aura::Window* window_;

  scoped_ptr<WindowObserver> window_observer_;

  // Are we in the process of closing?  Tracked so fullscreen views can avoid
  // sending a second shutdown request to the host when they lose the focus
  // after requesting shutdown for another reason (e.g. Escape key).
  bool in_shutdown_;

  // Is this a fullscreen view?
  bool is_fullscreen_;

  // Our parent host view, if this is a popup.  NULL otherwise.
  RenderWidgetHostViewAura* popup_parent_host_view_;

  // Our child popup host. NULL if we do not have a child popup.
  RenderWidgetHostViewAura* popup_child_host_view_;

  // True when content is being loaded. Used to show an hourglass cursor.
  bool is_loading_;

  // The cursor for the page. This is passed up from the renderer.
  WebCursor current_cursor_;

  // The touch-event. Its touch-points are updated as necessary. A new
  // touch-point is added from an ET_TOUCH_PRESSED event, and a touch-point is
  // removed from the list on an ET_TOUCH_RELEASED event.
  WebKit::WebTouchEvent touch_event_;

  // The current text input type.
  ui::TextInputType text_input_type_;
  bool can_compose_inline_;

  // Rectangles before and after the selection.
  gfx::Rect selection_start_rect_;
  gfx::Rect selection_end_rect_;

  // The current composition character bounds.
  std::vector<gfx::Rect> composition_character_bounds_;

  // Indicates if there is onging composition text.
  bool has_composition_text_;

  // Current tooltip text.
  string16 tooltip_;

  std::vector< base::Callback<void(ui::Compositor*)> >
      on_compositing_did_commit_callbacks_;

  std::vector< base::Callback<void(ui::Compositor*)> >
      on_compositing_will_start_callbacks_;

  std::map<uint64, scoped_refptr<ui::Texture> >
      image_transport_clients_;

  uint64 current_surface_;

  // Protected means that the |current_surface_| may be in use by ui and cannot
  // be safely discarded. Things to consider are thumbnailer, compositor draw,
  // and tab visibility.
  bool current_surface_is_protected_;
  bool current_surface_in_use_by_compositor_;

  std::vector<base::Callback<void(bool)> > pending_thumbnail_tasks_;

  // This id increments every time surface_is_protected changes. We tag IPC
  // messages which rely on protection state with this id to stay in sync.
  uint32 protection_state_id_;

  int32 surface_route_id_;

  gfx::GLSurfaceHandle shared_surface_handle_;

  // If non-NULL we're in OnPaint() and this is the supplied canvas.
  gfx::Canvas* paint_canvas_;

  // Used to record the last position of the mouse.
  // While the mouse is locked, they store the last known position just as mouse
  // lock was entered.
  // Relative to the upper-left corner of the view.
  gfx::Point unlocked_mouse_position_;
  // Relative to the upper-left corner of the screen.
  gfx::Point unlocked_global_mouse_position_;
  // Last cursor position relative to screen. Used to compute movementX/Y.
  gfx::Point global_mouse_position_;
  // In mouse locked mode, we syntheticaly move the mouse cursor to the center
  // of the window when it reaches the window borders to avoid it going outside.
  // This flag is used to differentiate between these synthetic mouse move
  // events vs. normal mouse move events.
  bool synthetic_move_sent_;

  // Signals that the accelerated compositing has been turned on or off.
  // This is used to signal to turn off the external texture as soon as the
  // software backing store is updated.
  bool accelerated_compositing_state_changed_;

  // Used to prevent further resizes while a resize is pending.
  class ResizeLock;
  // These locks are the ones waiting for a texture of the right size to come
  // back from the renderer/GPU process.
  std::vector<linked_ptr<ResizeLock> > resize_locks_;
  // These locks are the ones waiting for a frame to be drawn.
  std::vector<linked_ptr<ResizeLock> > locks_pending_draw_;

  // This lock is for waiting for a front surface to become available to draw.
  scoped_refptr<aura::CompositorLock> released_front_lock_;

  DISALLOW_COPY_AND_ASSIGN(RenderWidgetHostViewAura);
};

}  // namespace content

#endif  // CONTENT_BROWSER_RENDERER_HOST_RENDER_WIDGET_HOST_VIEW_AURA_H_
