// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gfx/screen.h"

#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "base/logging.h"

namespace {

bool GetScreenWorkArea(gfx::Rect* out_rect) {
  gboolean ok;
  guchar* raw_data = NULL;
  gint data_len = 0;
  ok = gdk_property_get(gdk_get_default_root_window(),  // a gdk window
                        gdk_atom_intern("_NET_WORKAREA", FALSE),  // property
                        gdk_atom_intern("CARDINAL", FALSE),  // property type
                        0,  // byte offset into property
                        0xff,  // property length to retrieve
                        false,  // delete property after retrieval?
                        NULL,  // returned property type
                        NULL,  // returned data format
                        &data_len,  // returned data len
                        &raw_data);  // returned data
  if (!ok)
    return false;

  // We expect to get four longs back: x, y, width, height.
  if (data_len < static_cast<gint>(4 * sizeof(glong))) {
    NOTREACHED();
    g_free(raw_data);
    return false;
  }

  glong* data = reinterpret_cast<glong*>(raw_data);
  gint x = data[0];
  gint y = data[1];
  gint width = data[2];
  gint height = data[3];
  g_free(raw_data);

  out_rect->SetRect(x, y, width, height);
  return true;
}

}  // namespace

namespace gfx {

// static
gfx::Point Screen::GetCursorScreenPoint() {
  gint x, y;
  gdk_display_get_pointer(gdk_display_get_default(), NULL, &x, &y, NULL);
  return gfx::Point(x, y);
}

// static
gfx::Rect Screen::GetMonitorWorkAreaNearestWindow(gfx::NativeView view) {
  // Do not use the _NET_WORKAREA here, this is supposed to be an area on a
  // specific monitor, and _NET_WORKAREA is a hint from the WM that generally
  // spans across all monitors.  This would make the work area larger than the
  // monitor.
  // TODO(danakj) This is a work-around as there is no standard way to get this
  // area, but it is a rect that we should be computing.  The standard means
  // to compute this rect would be to watch all windows with
  // _NET_WM_STRUT(_PARTIAL) hints, and subtract their space from the physical
  // area of the monitor to construct a work area.
  return GetMonitorAreaNearestWindow(view);
}

// static
gfx::Rect Screen::GetMonitorAreaNearestWindow(gfx::NativeView view) {
  GdkScreen* screen = gdk_screen_get_default();
  gint monitor_num = 0;
  if (view) {
    GtkWidget* top_level = gtk_widget_get_toplevel(view);
    DCHECK(GTK_IS_WINDOW(top_level));
    GtkWindow* window = GTK_WINDOW(top_level);
    screen = gtk_window_get_screen(window);
    monitor_num = gdk_screen_get_monitor_at_window(
        screen,
        gtk_widget_get_window(top_level));
  }
  GdkRectangle bounds;
  gdk_screen_get_monitor_geometry(screen, monitor_num, &bounds);
  return gfx::Rect(bounds);
}

// static
gfx::Rect Screen::GetMonitorWorkAreaNearestPoint(const gfx::Point& point) {
  // TODO(jamiewalch): Restrict this to the work area of the monitor.
  return GetMonitorAreaNearestPoint(point);
}

// static
gfx::Rect Screen::GetMonitorAreaNearestPoint(const gfx::Point& point) {
  GdkScreen* screen = gdk_screen_get_default();
  gint monitor = gdk_screen_get_monitor_at_point(screen, point.x(), point.y());
  GdkRectangle bounds;
  gdk_screen_get_monitor_geometry(screen, monitor, &bounds);
  return gfx::Rect(bounds);
}

// static
gfx::Rect Screen::GetPrimaryMonitorWorkArea() {
  gfx::Rect rect;
  if (GetScreenWorkArea(&rect))
    return rect.Intersect(GetPrimaryMonitorBounds());

  // Return the best we've got.
  return GetPrimaryMonitorBounds();
}

// static
gfx::Rect Screen::GetPrimaryMonitorBounds() {
  GdkScreen* screen = gdk_screen_get_default();
  GdkRectangle rect;
  gdk_screen_get_monitor_geometry(screen, 0, &rect);
  return gfx::Rect(rect);
}

// static
gfx::Rect Screen::GetMonitorWorkAreaMatching(const gfx::Rect& match_rect) {
  // TODO(thestig) Implement multi-monitor support.
  return GetPrimaryMonitorWorkArea();
}

// static
gfx::NativeWindow Screen::GetWindowAtCursorScreenPoint() {
  GdkWindow* window = gdk_window_at_pointer(NULL, NULL);
  if (!window)
    return NULL;

  gpointer data = NULL;
  gdk_window_get_user_data(window, &data);
  GtkWidget* widget = reinterpret_cast<GtkWidget*>(data);
  if (!widget)
    return NULL;
  widget = gtk_widget_get_toplevel(widget);
  return GTK_IS_WINDOW(widget) ? GTK_WINDOW(widget) : NULL;
}

// static
gfx::Size Screen::GetPrimaryMonitorSize() {
  GdkScreen* screen = gdk_screen_get_default();
  return gfx::Size(gdk_screen_get_width(screen), gdk_screen_get_height(screen));
}

// static
int Screen::GetNumMonitors() {
  // This query is kinda bogus for Linux -- do we want number of X screens?
  // The number of monitors Xinerama has?  We'll just use whatever GDK uses.
  GdkScreen* screen = gdk_screen_get_default();
  return gdk_screen_get_n_monitors(screen);
}

}  // namespace gfx
