// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_WIDGET_WIDGET_DELEGATE_H_
#define UI_VIEWS_WIDGET_WIDGET_DELEGATE_H_
#pragma once

#include <string>

#include "base/memory/scoped_ptr.h"
#include "ui/base/accessibility/accessibility_types.h"
#include "ui/base/ui_base_types.h"
#include "ui/views/view.h"

class SkBitmap;

namespace gfx {
class Rect;
}

namespace views {
class BubbleDelegateView;
class ClientView;
class DialogDelegate;
class NonClientFrameView;
class View;
class Widget;

// WidgetDelegate interface
// Handles events on Widgets in context-specific ways.
class VIEWS_EXPORT WidgetDelegate {
 public:
  WidgetDelegate();

  // Called whenever the widget's position changes.
  virtual void OnWidgetMove();

  // Called with the display changes (color depth or resolution).
  virtual void OnDisplayChanged();

  // Called when the work area (the desktop area minus task bars,
  // menu bars, etc.) changes in size.
  virtual void OnWorkAreaChanged();

  // Returns the view that should have the focus when the widget is shown.  If
  // NULL no view is focused.
  virtual View* GetInitiallyFocusedView();

  // Moved from WindowDelegate: ------------------------------------------------
  // TODO(beng): sort

  virtual BubbleDelegateView* AsBubbleDelegate();
  virtual DialogDelegate* AsDialogDelegate();

  // Returns true if the window can ever be resized.
  virtual bool CanResize() const;

  // Returns true if the window can ever be maximized.
  virtual bool CanMaximize() const;

  // Returns true if the window can be activated.
  virtual bool CanActivate() const;

  // Returns the modal type that applies to the widget. Default is
  // ui::MODAL_TYPE_NONE (not modal).
  virtual ui::ModalType GetModalType() const;

  virtual ui::AccessibilityTypes::Role GetAccessibleWindowRole() const;

  virtual ui::AccessibilityTypes::State GetAccessibleWindowState() const;

  // Returns the title to be read with screen readers.
  virtual string16 GetAccessibleWindowTitle() const;

  // Returns the text to be displayed in the window title.
  virtual string16 GetWindowTitle() const;

  // Returns true if the window should show a title in the title bar.
  virtual bool ShouldShowWindowTitle() const;

  // Returns true if the window's client view wants a client edge.
  virtual bool ShouldShowClientEdge() const;

  // Returns the app icon for the window. On Windows, this is the ICON_BIG used
  // in Alt-Tab list and Win7's taskbar.
  virtual SkBitmap GetWindowAppIcon();

  // Returns the icon to be displayed in the window.
  virtual SkBitmap GetWindowIcon();

  // Returns true if a window icon should be shown.
  virtual bool ShouldShowWindowIcon() const;

  // Execute a command in the window's controller. Returns true if the command
  // was handled, false if it was not.
  virtual bool ExecuteWindowsCommand(int command_id);

  // Returns the window's name identifier. Used to identify this window for
  // state restoration.
  virtual std::string GetWindowName() const;

  // Saves the window's bounds and "show" state. By default this uses the
  // process' local state keyed by window name (See GetWindowName above). This
  // behavior can be overridden to provide additional functionality.
  virtual void SaveWindowPlacement(const gfx::Rect& bounds,
                                   ui::WindowShowState show_state);

  // Retrieves the window's bounds and "show" states.
  // This behavior can be overridden to provide additional functionality.
  virtual bool GetSavedWindowPlacement(gfx::Rect* bounds,
                                       ui::WindowShowState* show_state) const;

  // Returns true if the window's size should be restored. If this is false,
  // only the window's origin is restored and the window is given its
  // preferred size.
  // Default is true.
  virtual bool ShouldRestoreWindowSize() const;

  // Called when the window closes. The delegate MUST NOT delete itself during
  // this call, since it can be called afterwards. See DeleteDelegate().
  virtual void WindowClosing() {}

  // Called when the window is destroyed. No events must be sent or received
  // after this point. The delegate can use this opportunity to delete itself at
  // this time if necessary.
  virtual void DeleteDelegate() {}

  // Called when the user begins/ends to change the bounds of the window.
  virtual void OnWindowBeginUserBoundsChange() {}
  virtual void OnWindowEndUserBoundsChange() {}

  // Returns the Widget associated with this delegate.
  virtual Widget* GetWidget() = 0;
  virtual const Widget* GetWidget() const = 0;

  // Returns the View that is contained within this Widget.
  virtual View* GetContentsView();

  // Called by the Widget to create the Client View used to host the contents
  // of the widget.
  virtual ClientView* CreateClientView(Widget* widget);

  // Called by the Widget to create the NonClient Frame View for this widget.
  // Return NULL to use the default one.
  virtual NonClientFrameView* CreateNonClientFrameView();

  // Returns true if the window can be notified with the work area change.
  // Otherwise, the work area change for the top window will be processed by
  // the default window manager. In some cases, like panel, we would like to
  // manage the positions by ourselves.
  virtual bool WillProcessWorkAreaChange() const;

 protected:
  virtual ~WidgetDelegate() {}

 private:
  View* default_contents_view_;

  DISALLOW_COPY_AND_ASSIGN(WidgetDelegate);
};

// A WidgetDelegate implementation that is-a View. Used to override GetWidget()
// to call View's GetWidget() for the common case where a WidgetDelegate
// implementation is-a View.
class VIEWS_EXPORT WidgetDelegateView : public WidgetDelegate, public View {
 public:
  WidgetDelegateView();
  virtual ~WidgetDelegateView();

  // Overridden from WidgetDelegate:
  virtual Widget* GetWidget() OVERRIDE;
  virtual const Widget* GetWidget() const OVERRIDE;

 private:
  DISALLOW_COPY_AND_ASSIGN(WidgetDelegateView);
};

}  // namespace views

#endif  // UI_VIEWS_WIDGET_WIDGET_DELEGATE_H_
