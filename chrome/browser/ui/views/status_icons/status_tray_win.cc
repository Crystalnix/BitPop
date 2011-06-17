// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/status_icons/status_tray_win.h"

#include "base/win/wrapped_window_proc.h"
#include "chrome/browser/ui/views/status_icons/status_icon_win.h"
#include "chrome/common/chrome_constants.h"
#include "ui/base/win/hwnd_util.h"

static const UINT kStatusIconMessage = WM_APP + 1;

StatusTrayWin::StatusTrayWin()
    : next_icon_id_(1) {
  // Register our window class
  HINSTANCE hinst = GetModuleHandle(NULL);
  WNDCLASSEX wc = {0};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = base::win::WrappedWindowProc<StatusTrayWin::WndProcStatic>;
  wc.hInstance = hinst;
  wc.lpszClassName = chrome::kStatusTrayWindowClass;
  ATOM clazz = RegisterClassEx(&wc);
  DCHECK(clazz);

  // If the taskbar is re-created after we start up, we have to rebuild all of
  // our icons.
  taskbar_created_message_ = RegisterWindowMessage(TEXT("TaskbarCreated"));

  // Create an offscreen window for handling messages for the status icons. We
  // create a hidden WS_POPUP window instead of an HWND_MESSAGE window, because
  // only top-level windows such as popups can receive broadcast messages like
  // "TaskbarCreated".
  window_ = CreateWindow(chrome::kStatusTrayWindowClass,
                         0, WS_POPUP, 0, 0, 0, 0, 0, 0, hinst, 0);
  ui::CheckWindowCreated(window_);
  ui::SetWindowUserData(window_, this);
}

LRESULT CALLBACK StatusTrayWin::WndProcStatic(HWND hwnd,
                                              UINT message,
                                              WPARAM wparam,
                                              LPARAM lparam) {
  StatusTrayWin* msg_wnd = reinterpret_cast<StatusTrayWin*>(
      GetWindowLongPtr(hwnd, GWLP_USERDATA));
  if (msg_wnd)
    return msg_wnd->WndProc(hwnd, message, wparam, lparam);
  else
    return ::DefWindowProc(hwnd, message, wparam, lparam);
}

LRESULT CALLBACK StatusTrayWin::WndProc(HWND hwnd,
                                        UINT message,
                                        WPARAM wparam,
                                        LPARAM lparam) {
  if (message == taskbar_created_message_) {
    // We need to reset all of our icons because the taskbar went away.
    for (StatusIconList::const_iterator iter = status_icons().begin();
         iter != status_icons().end();
         ++iter) {
      StatusIconWin* win_icon = static_cast<StatusIconWin*>(*iter);
      win_icon->ResetIcon();
    }
    return TRUE;
  } else if (message == kStatusIconMessage) {
    switch (lparam) {
      case WM_LBUTTONDOWN:
      case WM_RBUTTONDOWN:
      case WM_CONTEXTMENU:
        // Walk our icons, find which one was clicked on, and invoke its
        // HandleClickEvent() method.
        for (StatusIconList::const_iterator iter = status_icons().begin();
             iter != status_icons().end();
             ++iter) {
          StatusIconWin* win_icon = static_cast<StatusIconWin*>(*iter);
          if (win_icon->icon_id() == wparam) {
            POINT p;
            GetCursorPos(&p);
            win_icon->HandleClickEvent(p.x, p.y, lparam == WM_LBUTTONDOWN);
          }
        }
        return TRUE;
    }
  }
  return ::DefWindowProc(hwnd, message, wparam, lparam);
}

StatusTrayWin::~StatusTrayWin() {
  if (window_)
    DestroyWindow(window_);
  UnregisterClass(chrome::kStatusTrayWindowClass, GetModuleHandle(NULL));
}

StatusIcon* StatusTrayWin::CreatePlatformStatusIcon() {
  return new StatusIconWin(next_icon_id_++, window_, kStatusIconMessage);
}

StatusTray* StatusTray::Create() {
  return new StatusTrayWin();
}
