// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VIEWS_WIDGET_NATIVE_WIDGET_DELEGATE_H_
#define VIEWS_WIDGET_NATIVE_WIDGET_DELEGATE_H_
#pragma once

namespace gfx {
class Canvas;
class Size;
}

namespace views {
namespace internal {

////////////////////////////////////////////////////////////////////////////////
// NativeWidgetDelegate
//
//  An interface implemented by the object that handles events sent by a
//  NativeWidget implementation.
//
class NativeWidgetDelegate {
 public:
  virtual ~NativeWidgetDelegate() {}

  // Called when native focus moves from one native view to another.
  virtual void OnNativeFocus(gfx::NativeView focused_view) = 0;
  virtual void OnNativeBlur(gfx::NativeView focused_view) = 0;

  // Called when the native widget is created.
  virtual void OnNativeWidgetCreated() = 0;

  // Called when the NativeWidget changed size to |new_size|.
  virtual void OnSizeChanged(const gfx::Size& new_size) = 0;

  // Returns true if the delegate has a FocusManager.
  virtual bool HasFocusManager() const = 0;

  // Paints the widget using acceleration. If the widget is not using
  // accelerated painting this returns false and does nothing.
  virtual bool OnNativeWidgetPaintAccelerated(
      const gfx::Rect& dirty_region) = 0;

  // Paints the rootview in the canvas. This will also refresh the compositor
  // tree if necessary when accelerated painting is enabled.
  virtual void OnNativeWidgetPaint(gfx::Canvas* canvas) = 0;

  // Mouse and key event handlers.
  virtual bool OnKeyEvent(const KeyEvent& event) = 0;
  virtual bool OnMouseEvent(const MouseEvent& event) = 0;
  virtual void OnMouseCaptureLost() = 0;

  //
  virtual Widget* AsWidget() = 0;
  virtual const Widget* AsWidget() const = 0;
};

}  // namespace internal
}  // namespace views

#endif  // VIEWS_WIDGET_NATIVE_WIDGET_DELEGATE_H_
