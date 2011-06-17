// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VIEWS_CONTROLS_SCROLLBAR_NATIVE_SCROLL_BAR_GTK_H_
#define VIEWS_CONTROLS_SCROLLBAR_NATIVE_SCROLL_BAR_GTK_H_
#pragma once

#include "views/controls/native_control_gtk.h"
#include "views/controls/scrollbar/native_scroll_bar_wrapper.h"

namespace views {

class ScrollBarContainer;

/////////////////////////////////////////////////////////////////////////////
//
// NativeScrollBarGtk
//
// A View subclass that wraps a Native gtk scrollbar control.
//
// A scrollbar is either horizontal or vertical.
//
/////////////////////////////////////////////////////////////////////////////
class NativeScrollBarGtk : public NativeControlGtk,
                           public NativeScrollBarWrapper {
 public:
  // Creates new scrollbar, either horizontal or vertical.
  explicit NativeScrollBarGtk(NativeScrollBar* native_scroll_bar);
  virtual ~NativeScrollBarGtk();

 private:
  // Overridden from View for layout purpose.
  virtual void Layout();
  virtual gfx::Size GetPreferredSize();

  // Overridden from View for keyboard UI purpose.
  virtual bool OnKeyPressed(const KeyEvent& event);
  virtual bool OnMouseWheel(const MouseWheelEvent& e);

  // Overridden from NativeControlGtk.
  virtual void CreateNativeControl();

  // Overridden from NativeScrollBarWrapper.
  virtual int GetPosition() const;
  virtual View* GetView();
  virtual void Update(int viewport_size, int content_size, int current_pos);

  // Moves the scrollbar by the given value. Negative value is allowed.
  // (moves upward)
  void MoveBy(int o);

  // Moves the scrollbar by the page (viewport) size.
  void MovePage(bool positive);

  // Moves the scrollbar by predefined step size.
  void MoveStep(bool positive);

  // Moves the scrollbar to the given position. MoveTo(0) moves it to the top.
  void MoveTo(int p);

  // Moves the scrollbar to the end.
  void MoveToBottom();

  // Invoked when the scrollbar's position is changed.
  void ValueChanged();
  static void CallValueChanged(GtkWidget* widget,
                               NativeScrollBarGtk* scroll_bar);

  // The NativeScrollBar we are bound to.
  NativeScrollBar* native_scroll_bar_;

 private:
  DISALLOW_COPY_AND_ASSIGN(NativeScrollBarGtk);
};

}  // namespace views

#endif  // #ifndef VIEWS_CONTROLS_SCROLLBAR_NATIVE_SCROLL_BAR_GTK_H_

