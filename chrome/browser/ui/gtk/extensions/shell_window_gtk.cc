// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/gtk/extensions/shell_window_gtk.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/common/extensions/extension.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "ui/base/x/active_window_watcher_x.h"
#include "ui/gfx/rect.h"

ShellWindowGtk::ShellWindowGtk(Profile* profile,
                               const extensions::Extension* extension,
                               const GURL& url,
                               const ShellWindow::CreateParams& params)
    : ShellWindow(profile, extension, url),
      state_(GDK_WINDOW_STATE_WITHDRAWN),
      is_active_(!ui::ActiveWindowWatcherX::WMSupportsActivation()),
      content_thinks_its_fullscreen_(false) {
  window_ = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));

  gfx::NativeView native_view =
      web_contents()->GetView()->GetNativeView();
  gtk_container_add(GTK_CONTAINER(window_), native_view);

  gtk_window_set_default_size(
      window_, params.bounds.width(), params.bounds.height());

  // Hide titlebar when {frame: 'none'} specified on ShellWindow.
  if (params.frame == ShellWindow::CreateParams::FRAME_NONE)
    gtk_window_set_decorated(window_, false);

  int min_width = params.minimum_size.width();
  int min_height = params.minimum_size.height();
  int max_width = params.maximum_size.width();
  int max_height = params.maximum_size.height();
  GdkGeometry hints;
  int hints_mask = 0;
  if (min_width || min_height) {
    hints.min_height = min_height;
    hints.min_width = min_width;
    hints_mask |= GDK_HINT_MIN_SIZE;
  }
  if (max_width || max_height) {
    hints.max_height = max_height ? max_height : G_MAXINT;
    hints.max_width = max_width ? max_width : G_MAXINT;
    hints_mask |= GDK_HINT_MAX_SIZE;
  }
  if (hints_mask) {
    gtk_window_set_geometry_hints(
        window_,
        GTK_WIDGET(window_),
        &hints,
        static_cast<GdkWindowHints>(hints_mask));
  }

  // TODO(mihaip): Mirror contents of <title> tag in window title
  gtk_window_set_title(window_, extension->name().c_str());

  g_signal_connect(window_, "delete-event",
                   G_CALLBACK(OnMainWindowDeleteEventThunk), this);
  g_signal_connect(window_, "configure-event",
                   G_CALLBACK(OnConfigureThunk), this);
  g_signal_connect(window_, "window-state-event",
                   G_CALLBACK(OnWindowStateThunk), this);

  ui::ActiveWindowWatcherX::AddObserver(this);
}

ShellWindowGtk::~ShellWindowGtk() {
  ui::ActiveWindowWatcherX::RemoveObserver(this);
}

bool ShellWindowGtk::IsActive() const {
  return is_active_;
}

bool ShellWindowGtk::IsMaximized() const {
  return (state_ & GDK_WINDOW_STATE_MAXIMIZED);
}

bool ShellWindowGtk::IsMinimized() const {
  return (state_ & GDK_WINDOW_STATE_ICONIFIED);
}

bool ShellWindowGtk::IsFullscreen() const {
  return false;
}

gfx::NativeWindow ShellWindowGtk::GetNativeWindow() {
  return window_;
}

gfx::Rect ShellWindowGtk::GetRestoredBounds() const {
  return restored_bounds_;
}

gfx::Rect ShellWindowGtk::GetBounds() const {
  return bounds_;
}

void ShellWindowGtk::Show() {
  gtk_window_present(window_);
}

void ShellWindowGtk::ShowInactive() {
  gtk_window_set_focus_on_map(window_, false);
  gtk_widget_show(GTK_WIDGET(window_));
}

void ShellWindowGtk::Close() {
  GtkWidget* window = GTK_WIDGET(window_);
  // To help catch bugs in any event handlers that might get fired during the
  // destruction, set window_ to NULL before any handlers will run.
  window_ = NULL;

  // OnNativeClose does a delete this so no other members should
  // be accessed after. gtk_widget_destroy is safe (and must
  // be last).
  OnNativeClose();
  gtk_widget_destroy(window);
}

void ShellWindowGtk::Activate() {
  gtk_window_present(window_);
}

void ShellWindowGtk::Deactivate() {
  gdk_window_lower(gtk_widget_get_window(GTK_WIDGET(window_)));
}

void ShellWindowGtk::Maximize() {
  gtk_window_maximize(window_);
}

void ShellWindowGtk::Minimize() {
  gtk_window_iconify(window_);
}

void ShellWindowGtk::Restore() {
  if (IsMaximized())
    gtk_window_unmaximize(window_);
  else if (IsMinimized())
    gtk_window_deiconify(window_);
}

void ShellWindowGtk::SetBounds(const gfx::Rect& bounds) {
  gtk_window_move(window_, bounds.x(), bounds.y());
  // TODO(mihaip): Do we need the same workaround as BrowserWindowGtk::
  // SetWindowSize in order to avoid triggering fullscreen mode?
  gtk_window_resize(window_, bounds.width(), bounds.height());
}

void ShellWindowGtk::SetDraggableRegion(SkRegion* region) {
  // TODO: implement
}

void ShellWindowGtk::FlashFrame(bool flash) {
  gtk_window_set_urgency_hint(window_, flash);
}

bool ShellWindowGtk::IsAlwaysOnTop() const {
  return false;
}

void ShellWindowGtk::ActiveWindowChanged(GdkWindow* active_window) {
  // Do nothing if we're in the process of closing the browser window.
  if (!window_)
    return;

  is_active_ = gtk_widget_get_window(GTK_WIDGET(window_)) == active_window;
}

// Callback for the delete event.  This event is fired when the user tries to
// close the window (e.g., clicking on the X in the window manager title bar).
gboolean ShellWindowGtk::OnMainWindowDeleteEvent(GtkWidget* widget,
                                                 GdkEvent* event) {
  Close();

  // Return true to prevent the GTK window from being destroyed.  Close will
  // destroy it for us.
  return TRUE;
}

gboolean ShellWindowGtk::OnConfigure(GtkWidget* widget,
                                     GdkEventConfigure* event) {
  // TODO(mihaip): Do we need an explicit gtk_window_get_position call like in
  // in BrowserWindowGtk::OnConfigure?
  bounds_.SetRect(event->x, event->y, event->width, event->height);
  if (!IsMaximized())
    restored_bounds_ = bounds_;

  return FALSE;
}

gboolean ShellWindowGtk::OnWindowState(GtkWidget* sender,
                                       GdkEventWindowState* event) {
  state_ = event->new_window_state;

  if (content_thinks_its_fullscreen_ &&
      !(state_ & GDK_WINDOW_STATE_FULLSCREEN)) {
    content_thinks_its_fullscreen_ = false;
    content::RenderViewHost* rvh = web_contents()->GetRenderViewHost();
    if (rvh)
      rvh->ExitFullscreen();
  }

  return FALSE;
}

void ShellWindowGtk::SetFullscreen(bool fullscreen) {
  content_thinks_its_fullscreen_ = fullscreen;
  if (fullscreen)
    gtk_window_fullscreen(window_);
  else
    gtk_window_unfullscreen(window_);
}

bool ShellWindowGtk::IsFullscreenOrPending() const {
  return content_thinks_its_fullscreen_;
}

// static
ShellWindow* ShellWindow::CreateImpl(Profile* profile,
                                     const extensions::Extension* extension,
                                     const GURL& url,
                                     const ShellWindow::CreateParams& params) {
  return new ShellWindowGtk(profile, extension, url, params);
}
