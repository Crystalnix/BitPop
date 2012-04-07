// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_RENDER_WIDGET_HOST_VIEW_AURA_H_
#define CONTENT_BROWSER_RENDERER_HOST_RENDER_WIDGET_HOST_VIEW_AURA_H_
#pragma once

#include <map>

#include "content/browser/renderer_host/render_widget_host_view.h"
#include "content/common/content_export.h"
#include "ui/aura/client/activation_delegate.h"
#include "ui/aura/window_delegate.h"
#include "ui/base/ime/text_input_client.h"
#include "ui/gfx/rect.h"
#include "ui/gfx/compositor/compositor_observer.h"
#include "webkit/glue/webcursor.h"

#if defined(UI_COMPOSITOR_IMAGE_TRANSPORT)
#include "base/callback.h"
#include "base/memory/ref_counted.h"
#endif

namespace gfx {
class Canvas;
}

namespace ui {
class InputMethod;
}

namespace WebKit {
class WebTouchEvent;
}

#if defined(UI_COMPOSITOR_IMAGE_TRANSPORT)
class AcceleratedSurfaceContainerLinux;
#endif

class CONTENT_EXPORT RenderWidgetHostViewAura
    : NON_EXPORTED_BASE(public RenderWidgetHostView),
#if defined(UI_COMPOSITOR_IMAGE_TRANSPORT)
      public ui::CompositorObserver,
#endif
      public ui::TextInputClient,
      public aura::WindowDelegate,
      public aura::client::ActivationDelegate {
 public:
  virtual ~RenderWidgetHostViewAura();

  // Overridden from RenderWidgetHostView:
  virtual void InitAsChild(gfx::NativeView parent_host_view) OVERRIDE;
  virtual void InitAsPopup(RenderWidgetHostView* parent_host_view,
                           const gfx::Rect& pos) OVERRIDE;
  virtual void InitAsFullscreen(
      RenderWidgetHostView* reference_host_view) OVERRIDE;
  virtual RenderWidgetHost* GetRenderWidgetHost() const OVERRIDE;
  virtual void DidBecomeSelected() OVERRIDE;
  virtual void WasHidden() OVERRIDE;
  virtual void SetSize(const gfx::Size& size) OVERRIDE;
  virtual void SetBounds(const gfx::Rect& rect) OVERRIDE;
  virtual gfx::NativeView GetNativeView() const OVERRIDE;
  virtual gfx::NativeViewId GetNativeViewId() const OVERRIDE;
  virtual gfx::NativeViewAccessible GetNativeViewAccessible() OVERRIDE;
  virtual void MovePluginWindows(
      const std::vector<webkit::npapi::WebPluginGeometry>& moves) OVERRIDE;
  virtual void Focus() OVERRIDE;
  virtual void Blur() OVERRIDE;
  virtual bool HasFocus() const OVERRIDE;
  virtual void Show() OVERRIDE;
  virtual void Hide() OVERRIDE;
  virtual bool IsShowing() OVERRIDE;
  virtual gfx::Rect GetViewBounds() const OVERRIDE;
  virtual void UpdateCursor(const WebCursor& cursor) OVERRIDE;
  virtual void SetIsLoading(bool is_loading) OVERRIDE;
  virtual void TextInputStateChanged(ui::TextInputType type,
                                     bool can_compose_inline) OVERRIDE;
  virtual void ImeCancelComposition() OVERRIDE;
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
  virtual void OnAcceleratedCompositingStateChange() OVERRIDE;
  virtual void AcceleratedSurfaceBuffersSwapped(
      const GpuHostMsg_AcceleratedSurfaceBuffersSwapped_Params& params,
      int gpu_host_id) OVERRIDE;
  virtual void AcceleratedSurfacePostSubBuffer(
      const GpuHostMsg_AcceleratedSurfacePostSubBuffer_Params& params,
      int gpu_host_id) OVERRIDE;
#if defined(UI_COMPOSITOR_IMAGE_TRANSPORT)
  virtual void AcceleratedSurfaceNew(
      int32 width,
      int32 height,
      uint64* surface_id,
      TransportDIB::Handle* surface_handle) OVERRIDE;
  virtual void AcceleratedSurfaceRelease(uint64 surface_id) OVERRIDE;
#endif
  virtual void SetBackground(const SkBitmap& background) OVERRIDE;
  virtual void GetScreenInfo(WebKit::WebScreenInfo* results) OVERRIDE;
  virtual gfx::Rect GetRootWindowBounds() OVERRIDE;
  virtual void UnhandledWheelEvent(
      const WebKit::WebMouseWheelEvent& event) OVERRIDE;
  virtual void ProcessTouchAck(bool processed) OVERRIDE;
  virtual void SetHasHorizontalScrollbar(
      bool has_horizontal_scrollbar) OVERRIDE;
  virtual void SetScrollOffsetPinning(
      bool is_pinned_to_left, bool is_pinned_to_right) OVERRIDE;
  virtual gfx::PluginWindowHandle GetCompositingSurface() OVERRIDE;
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
  virtual void OnFocus() OVERRIDE;
  virtual void OnBlur() OVERRIDE;
  virtual bool OnKeyEvent(aura::KeyEvent* event) OVERRIDE;
  virtual gfx::NativeCursor GetCursor(const gfx::Point& point) OVERRIDE;
  virtual int GetNonClientComponent(const gfx::Point& point) const OVERRIDE;
  virtual bool OnMouseEvent(aura::MouseEvent* event) OVERRIDE;
  virtual ui::TouchStatus OnTouchEvent(aura::TouchEvent* event) OVERRIDE;
  virtual ui::GestureStatus OnGestureEvent(aura::GestureEvent* event) OVERRIDE;
  virtual bool CanFocus() OVERRIDE;
  virtual void OnCaptureLost() OVERRIDE;
  virtual void OnPaint(gfx::Canvas* canvas) OVERRIDE;
  virtual void OnWindowDestroying() OVERRIDE;
  virtual void OnWindowDestroyed() OVERRIDE;
  virtual void OnWindowVisibilityChanged(bool visible) OVERRIDE;

  // Overridden from aura::client::ActivationDelegate:
  virtual bool ShouldActivate(aura::Event* event) OVERRIDE;
  virtual void OnActivated() OVERRIDE;
  virtual void OnLostActive() OVERRIDE;

 protected:
  friend class RenderWidgetHostView;

  // Should construct only via RenderWidgetHostView::CreateViewForWidget.
  explicit RenderWidgetHostViewAura(RenderWidgetHost* host);

 private:
#if defined(UI_COMPOSITOR_IMAGE_TRANSPORT)
  // Overridden from ui::CompositorObserver:
  virtual void OnCompositingEnded(ui::Compositor* compositor) OVERRIDE;
#endif

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

  // The model object.
  RenderWidgetHost* host_;

  aura::Window* window_;

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

  // Indicates if there is onging composition text.
  bool has_composition_text_;

  // Current tooltip text.
  string16 tooltip_;

#if defined(UI_COMPOSITOR_IMAGE_TRANSPORT)
  std::vector< base::Callback<void(void)> > on_compositing_ended_callbacks_;

  std::map<uint64, scoped_refptr<AcceleratedSurfaceContainerLinux> >
      accelerated_surface_containers_;

  gfx::PluginWindowHandle current_surface_;
#endif

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

  DISALLOW_COPY_AND_ASSIGN(RenderWidgetHostViewAura);
};

#endif  // CONTENT_BROWSER_RENDERER_HOST_RENDER_WIDGET_HOST_VIEW_AURA_H_
