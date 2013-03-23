// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_RENDER_WIDGET_HOST_VIEW_AURA_H_
#define CONTENT_BROWSER_RENDERER_HOST_RENDER_WIDGET_HOST_VIEW_AURA_H_

#include <map>
#include <vector>

#include "base/callback.h"
#include "base/gtest_prod_util.h"
#include "base/memory/linked_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "content/browser/renderer_host/image_transport_factory.h"
#include "content/browser/renderer_host/render_widget_host_view_base.h"
#include "content/common/content_export.h"
#include "third_party/skia/include/core/SkRegion.h"
#include "ui/aura/client/activation_change_observer.h"
#include "ui/aura/client/activation_delegate.h"
#include "ui/aura/client/focus_change_observer.h"
#include "ui/aura/window_delegate.h"
#include "ui/base/ime/text_input_client.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/compositor_observer.h"
#include "ui/gfx/display_observer.h"
#include "ui/gfx/rect.h"
#include "webkit/glue/webcursor.h"

namespace aura {
class WindowTracker;
}

namespace gfx {
class Canvas;
class Display;
}

namespace ui {
class CompositorLock;
class InputMethod;
class Texture;
}

namespace content {
class RenderWidgetHostImpl;
class RenderWidgetHostView;

// RenderWidgetHostView class hierarchy described in render_widget_host_view.h.
class RenderWidgetHostViewAura
    : public RenderWidgetHostViewBase,
      public ui::CompositorObserver,
      public ui::TextInputClient,
      public gfx::DisplayObserver,
      public aura::WindowDelegate,
      public aura::client::ActivationDelegate,
      public aura::client::ActivationChangeObserver,
      public aura::client::FocusChangeObserver,
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
  virtual void ScrollOffsetChanged() OVERRIDE;

  // Overridden from RenderWidgetHostViewPort:
  virtual void InitAsPopup(RenderWidgetHostView* parent_host_view,
                           const gfx::Rect& pos) OVERRIDE;
  virtual void InitAsFullscreen(
      RenderWidgetHostView* reference_host_view) OVERRIDE;
  virtual void WasShown() OVERRIDE;
  virtual void WasHidden() OVERRIDE;
  virtual void MovePluginWindows(
      const gfx::Vector2d& scroll_offset,
      const std::vector<webkit::npapi::WebPluginGeometry>& moves) OVERRIDE;
  virtual void Focus() OVERRIDE;
  virtual void Blur() OVERRIDE;
  virtual void UpdateCursor(const WebCursor& cursor) OVERRIDE;
  virtual void SetIsLoading(bool is_loading) OVERRIDE;
  virtual void TextInputStateChanged(
      const ViewHostMsg_TextInputState_Params& params) OVERRIDE;
  virtual void ImeCancelComposition() OVERRIDE;
  virtual void ImeCompositionRangeChanged(
      const ui::Range& range,
      const std::vector<gfx::Rect>& character_bounds) OVERRIDE;
  virtual void DidUpdateBackingStore(
      const gfx::Rect& scroll_rect,
      const gfx::Vector2d& scroll_delta,
      const std::vector<gfx::Rect>& copy_rects) OVERRIDE;
  virtual void RenderViewGone(base::TerminationStatus status,
                              int error_code) OVERRIDE;
  virtual void Destroy() OVERRIDE;
  virtual void SetTooltipText(const string16& tooltip_text) OVERRIDE;
  virtual void SelectionChanged(const string16& text,
                                size_t offset,
                                const ui::Range& range) OVERRIDE;
  virtual void SelectionBoundsChanged(
      const gfx::Rect& start_rect,
      WebKit::WebTextDirection start_direction,
      const gfx::Rect& end_rect,
      WebKit::WebTextDirection end_direction) OVERRIDE;
  virtual BackingStore* AllocBackingStore(const gfx::Size& size) OVERRIDE;
  virtual void CopyFromCompositingSurface(
      const gfx::Rect& src_subrect,
      const gfx::Size& dst_size,
      const base::Callback<void(bool)>& callback,
      skia::PlatformBitmap* output) OVERRIDE;
  virtual void OnAcceleratedCompositingStateChange() OVERRIDE;
  virtual void AcceleratedSurfaceBuffersSwapped(
      const GpuHostMsg_AcceleratedSurfaceBuffersSwapped_Params& params_in_pixel,
      int gpu_host_id) OVERRIDE;
  virtual void AcceleratedSurfacePostSubBuffer(
      const GpuHostMsg_AcceleratedSurfacePostSubBuffer_Params& params_in_pixel,
      int gpu_host_id) OVERRIDE;
  virtual void AcceleratedSurfaceSuspend() OVERRIDE;
  virtual bool HasAcceleratedSurface(const gfx::Size& desired_size) OVERRIDE;
  virtual void AcceleratedSurfaceNew(uint64 surface_id,
                                     const std::string& mailbox_name) OVERRIDE;
  virtual void AcceleratedSurfaceRelease() OVERRIDE;
  virtual void GetScreenInfo(WebKit::WebScreenInfo* results) OVERRIDE;
  virtual gfx::Rect GetBoundsInRootWindow() OVERRIDE;
  virtual void ProcessAckedTouchEvent(
      const WebKit::WebTouchEvent& touch,
      InputEventAckState ack_result) OVERRIDE;
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
  virtual void ExtendSelectionAndDelete(size_t before, size_t after) OVERRIDE;

  // Overridden from gfx::DisplayObserver:
  virtual void OnDisplayBoundsChanged(const gfx::Display& display) OVERRIDE;
  virtual void OnDisplayAdded(const gfx::Display& new_display) OVERRIDE;
  virtual void OnDisplayRemoved(const gfx::Display& old_display) OVERRIDE;

  // Overridden from aura::WindowDelegate:
  virtual gfx::Size GetMinimumSize() const OVERRIDE;
  virtual gfx::Size GetMaximumSize() const OVERRIDE;
  virtual void OnBoundsChanged(const gfx::Rect& old_bounds,
                               const gfx::Rect& new_bounds) OVERRIDE;
  virtual gfx::NativeCursor GetCursor(const gfx::Point& point) OVERRIDE;
  virtual int GetNonClientComponent(const gfx::Point& point) const OVERRIDE;
  virtual bool ShouldDescendIntoChildForEventHandling(
      aura::Window* child,
      const gfx::Point& location) OVERRIDE;
  virtual bool CanFocus() OVERRIDE;
  virtual void OnCaptureLost() OVERRIDE;
  virtual void OnPaint(gfx::Canvas* canvas) OVERRIDE;
  virtual void OnDeviceScaleFactorChanged(float device_scale_factor) OVERRIDE;
  virtual void OnWindowDestroying() OVERRIDE;
  virtual void OnWindowDestroyed() OVERRIDE;
  virtual void OnWindowTargetVisibilityChanged(bool visible) OVERRIDE;
  virtual bool HasHitTestMask() const OVERRIDE;
  virtual void GetHitTestMask(gfx::Path* mask) const OVERRIDE;
  virtual scoped_refptr<ui::Texture> CopyTexture() OVERRIDE;

  // Overridden from ui::EventHandler:
  virtual void OnKeyEvent(ui::KeyEvent* event) OVERRIDE;
  virtual void OnMouseEvent(ui::MouseEvent* event) OVERRIDE;
  virtual void OnScrollEvent(ui::ScrollEvent* event) OVERRIDE;
  virtual void OnTouchEvent(ui::TouchEvent* event) OVERRIDE;
  virtual void OnGestureEvent(ui::GestureEvent* event) OVERRIDE;

  // Overridden from aura::client::ActivationDelegate:
  virtual bool ShouldActivate() const OVERRIDE;

  // Overridden from aura::client::ActivationChangeObserver:
  virtual void OnWindowActivated(aura::Window* gained_activation,
                                 aura::Window* lost_activation) OVERRIDE;

  // Overridden from aura::client::FocusChangeObserver:
  virtual void OnWindowFocused(aura::Window* gained_focus,
                               aura::Window* lost_focus) OVERRIDE;

 protected:
  friend class RenderWidgetHostView;

  // Should construct only via RenderWidgetHostView::CreateViewForWidget.
  explicit RenderWidgetHostViewAura(RenderWidgetHost* host);

 private:
  FRIEND_TEST_ALL_PREFIXES(RenderWidgetHostViewAuraTest, TouchEventState);
  FRIEND_TEST_ALL_PREFIXES(RenderWidgetHostViewAuraTest, TouchEventSyncAsync);

  class WindowObserver;
  friend class WindowObserver;

  // Overridden from ui::CompositorObserver:
  virtual void OnCompositingDidCommit(ui::Compositor* compositor) OVERRIDE;
  virtual void OnCompositingStarted(ui::Compositor* compositor) OVERRIDE;
  virtual void OnCompositingEnded(ui::Compositor* compositor) OVERRIDE;
  virtual void OnCompositingAborted(ui::Compositor* compositor) OVERRIDE;
  virtual void OnCompositingLockStateChanged(
      ui::Compositor* compositor) OVERRIDE;

  // Overridden from ImageTransportFactoryObserver:
  virtual void OnLostResources() OVERRIDE;

  virtual ~RenderWidgetHostViewAura();

  void UpdateCursorIfOverSelf();
  bool ShouldSkipFrame(const gfx::Size& size);
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
  void RunCompositingDidCommitCallbacks();

  struct BufferPresentedParams {
    BufferPresentedParams(int route_id,
                          int gpu_host_id,
                          uint64 surface_handle);
    ~BufferPresentedParams();

    int32 route_id;
    int gpu_host_id;
    uint64 surface_handle;
    scoped_refptr<ui::Texture> texture_to_produce;
  };

  // Insert a sync point into the compositor's command stream and acknowledge
  // that we have presented the accelerated surface buffer.
  static void InsertSyncPointAndACK(const BufferPresentedParams& params);

  // Called when window_ gets added to a new window tree.
  void AddingToRootWindow();

  // Called when window_ is removed from the window tree.
  void RemovingFromRootWindow();

  // Called after commit for the last reference to the texture going away
  // after it was released as the frontbuffer.
  void SetSurfaceNotInUseByCompositor(scoped_refptr<ui::Texture>);

  // Called after async thumbnailer task completes.  Used to call
  // AdjustSurfaceProtection.
  static void CopyFromCompositingSurfaceFinished(
      base::WeakPtr<RenderWidgetHostViewAura> render_widget_host_view,
      const base::Callback<void(bool)>& callback,
      bool result);

  ui::Compositor* GetCompositor();

  // Detaches |this| from the input method object.
  void DetachFromInputMethod();

  // Converts |rect| from window coordinate to screen coordinate.
  gfx::Rect ConvertRectToScreen(const gfx::Rect& rect);

  bool SwapBuffersPrepare(const gfx::Rect& surface_rect,
                          const gfx::Rect& damage_rect,
                          BufferPresentedParams* params);

  void SwapBuffersCompleted(const BufferPresentedParams& params);

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

  // The scale factor of the display the renderer is currently on.
  float device_scale_factor_;

  std::vector<base::Closure> on_compositing_did_commit_callbacks_;

  std::map<uint64, scoped_refptr<ui::Texture> > image_transport_clients_;

  // The identifier of the current frontbuffer.
  uint64 current_surface_;

  // The damage in the previously presented buffer.
  SkRegion previous_damage_;

  // Pending damage from previous frames that we skipped.
  SkRegion skipped_damage_;

  // The size of the last frame that was swapped (even if we skipped it).
  // Used to determine when the skipped_damage_ needs to be reset due to
  // size changes between front- and backbuffer.
  gfx::Size last_swapped_surface_size_;

  int pending_thumbnail_tasks_;

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
  typedef std::vector<linked_ptr<ResizeLock> > ResizeLockList;

  // These locks are the ones waiting for a texture of the right size to come
  // back from the renderer/GPU process.
  ResizeLockList resize_locks_;
  // These locks are the ones waiting for a frame to be committed.
  ResizeLockList locks_pending_commit_;

  // This lock is for waiting for a front surface to become available to draw.
  scoped_refptr<ui::CompositorLock> released_front_lock_;

  // Used to track the state of the window we're created from. Only used when
  // created fullscreen.
  scoped_ptr<aura::WindowTracker> host_tracker_;

  enum CanLockCompositorState {
    YES,
    // We locked, so at some point we'll need to kick a frame.
    YES_DID_LOCK,
    // No. A lock timed out, we need to kick a new frame before locking again.
    NO_PENDING_RENDERER_FRAME,
    // No. We've got a frame, but it hasn't been committed.
    NO_PENDING_COMMIT,
  };
  CanLockCompositorState can_lock_compositor_;

  // Frames receveived with an unregistered surface_handle.
  size_t consecutive_bad_frames_received_;

  // Whether the last frame received used an unregistered surface handle.
  bool last_frame_was_bad_;

  DISALLOW_COPY_AND_ASSIGN(RenderWidgetHostViewAura);
};

}  // namespace content

#endif  // CONTENT_BROWSER_RENDERER_HOST_RENDER_WIDGET_HOST_VIEW_AURA_H_
