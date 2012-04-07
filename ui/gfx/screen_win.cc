// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gfx/screen.h"

#include <windows.h>

namespace {

MONITORINFO GetMonitorInfoForMonitor(HMONITOR monitor) {
  MONITORINFO monitor_info = { 0 };
  monitor_info.cbSize = sizeof(monitor_info);
  GetMonitorInfo(monitor, &monitor_info);
  return monitor_info;
}

}  // namespace

namespace gfx {

// static
gfx::Point Screen::GetCursorScreenPoint() {
  POINT pt;
  GetCursorPos(&pt);
  return gfx::Point(pt);
}

// static
gfx::Rect Screen::GetMonitorWorkAreaNearestWindow(gfx::NativeWindow window) {
  MONITORINFO monitor_info;
  monitor_info.cbSize = sizeof(monitor_info);
  GetMonitorInfo(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST),
                 &monitor_info);
  return gfx::Rect(monitor_info.rcWork);
}

// static
gfx::Rect Screen::GetMonitorAreaNearestWindow(gfx::NativeWindow window) {
  MONITORINFO monitor_info;
  monitor_info.cbSize = sizeof(monitor_info);
  GetMonitorInfo(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST),
                 &monitor_info);
  return gfx::Rect(monitor_info.rcMonitor);
}

static gfx::Rect GetMonitorAreaOrWorkAreaNearestPoint(const gfx::Point& point,
                                                      bool work_area) {
  POINT initial_loc = { point.x(), point.y() };
  HMONITOR monitor = MonitorFromPoint(initial_loc, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi = {0};
  mi.cbSize = sizeof(mi);
  if (monitor && GetMonitorInfo(monitor, &mi))
    return gfx::Rect(work_area ? mi.rcWork : mi.rcMonitor);
  return gfx::Rect();
}

// static
gfx::Rect Screen::GetMonitorWorkAreaNearestPoint(const gfx::Point& point) {
  return GetMonitorAreaOrWorkAreaNearestPoint(point, true);
}

// static
gfx::Rect Screen::GetMonitorAreaNearestPoint(const gfx::Point& point) {
  return GetMonitorAreaOrWorkAreaNearestPoint(point, false);
}

// static
gfx::Rect Screen::GetPrimaryMonitorWorkArea() {
  return gfx::Rect(GetMonitorInfoForMonitor(MonitorFromWindow(NULL,
      MONITOR_DEFAULTTOPRIMARY)).rcWork);
}

// static
gfx::Rect Screen::GetPrimaryMonitorBounds() {
  return gfx::Rect(GetMonitorInfoForMonitor(MonitorFromWindow(NULL,
      MONITOR_DEFAULTTOPRIMARY)).rcMonitor);
}

// static
gfx::Rect Screen::GetMonitorWorkAreaMatching(const gfx::Rect& match_rect) {
  RECT other_bounds_rect = match_rect.ToRECT();
  MONITORINFO monitor_info = GetMonitorInfoForMonitor(MonitorFromRect(
      &other_bounds_rect, MONITOR_DEFAULTTONEAREST));
  return gfx::Rect(monitor_info.rcWork);
}

// static
gfx::NativeWindow Screen::GetWindowAtCursorScreenPoint() {
  POINT location;
  return GetCursorPos(&location) ? WindowFromPoint(location) : NULL;
}

// static
gfx::Size Screen::GetPrimaryMonitorSize() {
  return gfx::Size(GetSystemMetrics(SM_CXSCREEN),
                   GetSystemMetrics(SM_CYSCREEN));
}

// static
int Screen::GetNumMonitors() {
  return GetSystemMetrics(SM_CMONITORS);
}

}  // namespace gfx
