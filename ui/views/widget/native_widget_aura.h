// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_WIDGET_NATIVE_WIDGET_AURA_H_
#define UI_VIEWS_WIDGET_NATIVE_WIDGET_AURA_H_

#include "base/memory/scoped_vector.h"
#include "base/memory/weak_ptr.h"
#include "ui/aura/client/activation_delegate.h"
#include "ui/aura/client/drag_drop_delegate.h"
#include "ui/aura/window_delegate.h"
#include "ui/base/events.h"
#include "ui/views/views_export.h"
#include "ui/views/widget/native_widget_private.h"

namespace aura {
class Window;
}
namespace gfx {
class Font;
}

namespace views {

class DropHelper;
class NativeWidgetHelperAura;
class TooltipManagerAura;

class VIEWS_EXPORT NativeWidgetAura : public internal::NativeWidgetPrivate,
                                      public aura::WindowDelegate,
                                      public aura::client::ActivationDelegate,
                                      public aura::client::DragDropDelegate {
 public:
  explicit NativeWidgetAura(internal::NativeWidgetDelegate* delegate);

  // TODO(beng): Find a better place for this, and the similar method on
  //             NativeWidgetWin.
  static gfx::Font GetWindowTitleFont();

  // Overridden from internal::NativeWidgetPrivate:
  virtual void InitNativeWidget(const Widget::InitParams& params) OVERRIDE;
  virtual NonClientFrameView* CreateNonClientFrameView() OVERRIDE;
  virtual void UpdateFrameAfterFrameChange() OVERRIDE;
  virtual bool ShouldUseNativeFrame() const OVERRIDE;
  virtual void FrameTypeChanged() OVERRIDE;
  virtual Widget* GetWidget() OVERRIDE;
  virtual const Widget* GetWidget() const OVERRIDE;
  virtual gfx::NativeView GetNativeView() const OVERRIDE;
  virtual gfx::NativeWindow GetNativeWindow() const OVERRIDE;
  virtual Widget* GetTopLevelWidget() OVERRIDE;
  virtual const ui::Compositor* GetCompositor() const OVERRIDE;
  virtual ui::Compositor* GetCompositor() OVERRIDE;
  virtual void CalculateOffsetToAncestorWithLayer(
      gfx::Point* offset,
      ui::Layer** layer_parent) OVERRIDE;
  virtual void ViewRemoved(View* view) OVERRIDE;
  virtual void SetNativeWindowProperty(const char* name, void* value) OVERRIDE;
  virtual void* GetNativeWindowProperty(const char* name) const OVERRIDE;
  virtual TooltipManager* GetTooltipManager() const OVERRIDE;
  virtual bool IsScreenReaderActive() const OVERRIDE;
  virtual void SendNativeAccessibilityEvent(
      View* view,
      ui::AccessibilityTypes::Event event_type) OVERRIDE;
  virtual void SetCapture() OVERRIDE;
  virtual void ReleaseCapture() OVERRIDE;
  virtual bool HasCapture() const OVERRIDE;
  virtual InputMethod* CreateInputMethod() OVERRIDE;
  virtual void CenterWindow(const gfx::Size& size) OVERRIDE;
  virtual void GetWindowPlacement(
      gfx::Rect* bounds,
      ui::WindowShowState* maximized) const OVERRIDE;
  virtual void SetWindowTitle(const string16& title) OVERRIDE;
  virtual void SetWindowIcons(const gfx::ImageSkia& window_icon,
                              const gfx::ImageSkia& app_icon) OVERRIDE;
  virtual void SetAccessibleName(const string16& name) OVERRIDE;
  virtual void SetAccessibleRole(ui::AccessibilityTypes::Role role) OVERRIDE;
  virtual void SetAccessibleState(ui::AccessibilityTypes::State state) OVERRIDE;
  virtual void InitModalType(ui::ModalType modal_type) OVERRIDE;
  virtual gfx::Rect GetWindowBoundsInScreen() const OVERRIDE;
  virtual gfx::Rect GetClientAreaBoundsInScreen() const OVERRIDE;
  virtual gfx::Rect GetRestoredBounds() const OVERRIDE;
  virtual void SetBounds(const gfx::Rect& bounds) OVERRIDE;
  virtual void SetSize(const gfx::Size& size) OVERRIDE;
  virtual void StackAbove(gfx::NativeView native_view) OVERRIDE;
  virtual void StackAtTop() OVERRIDE;
  virtual void StackBelow(gfx::NativeView native_view) OVERRIDE;
  virtual void SetShape(gfx::NativeRegion shape) OVERRIDE;
  virtual void Close() OVERRIDE;
  virtual void CloseNow() OVERRIDE;
  virtual void Show() OVERRIDE;
  virtual void Hide() OVERRIDE;
  virtual void ShowMaximizedWithBounds(
      const gfx::Rect& restored_bounds) OVERRIDE;
  virtual void ShowWithWindowState(ui::WindowShowState state) OVERRIDE;
  virtual bool IsVisible() const OVERRIDE;
  virtual void Activate() OVERRIDE;
  virtual void Deactivate() OVERRIDE;
  virtual bool IsActive() const OVERRIDE;
  virtual void SetAlwaysOnTop(bool always_on_top) OVERRIDE;
  virtual void Maximize() OVERRIDE;
  virtual void Minimize() OVERRIDE;
  virtual bool IsMaximized() const OVERRIDE;
  virtual bool IsMinimized() const OVERRIDE;
  virtual void Restore() OVERRIDE;
  virtual void SetFullscreen(bool fullscreen) OVERRIDE;
  virtual bool IsFullscreen() const OVERRIDE;
  virtual void SetOpacity(unsigned char opacity) OVERRIDE;
  virtual void SetUseDragFrame(bool use_drag_frame) OVERRIDE;
  virtual void FlashFrame(bool flash_frame) OVERRIDE;
  virtual bool IsAccessibleWidget() const OVERRIDE;
  virtual void RunShellDrag(View* view,
                            const ui::OSExchangeData& data,
                            const gfx::Point& location,
                            int operation) OVERRIDE;
  virtual void SchedulePaintInRect(const gfx::Rect& rect) OVERRIDE;
  virtual void SetCursor(gfx::NativeCursor cursor) OVERRIDE;
  virtual void ClearNativeFocus() OVERRIDE;
  virtual void FocusNativeView(gfx::NativeView native_view) OVERRIDE;
  virtual gfx::Rect GetWorkAreaBoundsInScreen() const OVERRIDE;
  virtual void SetInactiveRenderingDisabled(bool value) OVERRIDE;
  virtual Widget::MoveLoopResult RunMoveLoop() OVERRIDE;
  virtual void EndMoveLoop() OVERRIDE;
  virtual void SetVisibilityChangedAnimationsEnabled(bool value) OVERRIDE;

  // Overridden from views::InputMethodDelegate:
  virtual void DispatchKeyEventPostIME(const KeyEvent& key) OVERRIDE;

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

  // Overridden from aura::client::DragDropDelegate:
  virtual void OnDragEntered(const aura::DropTargetEvent& event) OVERRIDE;
  virtual int OnDragUpdated(const aura::DropTargetEvent& event) OVERRIDE;
  virtual void OnDragExited() OVERRIDE;
  virtual int OnPerformDrop(const aura::DropTargetEvent& event) OVERRIDE;

 protected:
  virtual ~NativeWidgetAura();

  internal::NativeWidgetDelegate* delegate() { return delegate_; }

 private:
  class ActiveWindowObserver;

  void SetInitialFocus();

  internal::NativeWidgetDelegate* delegate_;

  scoped_ptr<NativeWidgetHelperAura> desktop_helper_;

  aura::Window* window_;

  // See class documentation for Widget in widget.h for a note about ownership.
  Widget::InitParams::Ownership ownership_;

  // The following factory is used for calls to close the NativeWidgetAura
  // instance.
  base::WeakPtrFactory<NativeWidgetAura> close_widget_factory_;

  // Can we be made active?
  bool can_activate_;

  // Are we in the destructor?
  bool destroying_;

  gfx::NativeCursor cursor_;

  // The saved window state for exiting full screen state.
  ui::WindowShowState saved_window_state_;

  scoped_ptr<TooltipManagerAura> tooltip_manager_;

  scoped_ptr<ActiveWindowObserver> active_window_observer_;

  scoped_ptr<DropHelper> drop_helper_;
  int last_drop_operation_;

  DISALLOW_COPY_AND_ASSIGN(NativeWidgetAura);
};

}  // namespace views

#endif  // UI_VIEWS_WIDGET_NATIVE_WIDGET_AURA_H_
