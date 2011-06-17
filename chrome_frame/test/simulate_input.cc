// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome_frame/test/simulate_input.h"

#include <atlbase.h>
#include <atlwin.h>

#include "chrome_frame/utils.h"

namespace simulate_input {

class ForegroundHelperWindow : public CWindowImpl<ForegroundHelperWindow> {
 public:
BEGIN_MSG_MAP(ForegroundHelperWindow)
  MESSAGE_HANDLER(WM_HOTKEY, OnHotKey)
END_MSG_MAP()

  ForegroundHelperWindow() : window_(NULL) {}

  HRESULT SetForeground(HWND window) {
    DCHECK(::IsWindow(window));
    window_ = window;
    if (NULL == Create(NULL, NULL, NULL, WS_POPUP))
      return AtlHresultFromLastError();

    static const int kHotKeyId = 0x0000baba;
    static const int kHotKeyWaitTimeout = 2000;

    RegisterHotKey(m_hWnd, kHotKeyId, 0, VK_F22);

    MSG msg = {0};
    PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE);

    SendMnemonic(VK_F22, NONE, false, false);
    // There are scenarios where the WM_HOTKEY is not dispatched by the
    // the corresponding foreground thread. To prevent us from indefinitely
    // waiting for the hotkey, we set a timer and exit the loop.
    SetTimer(kHotKeyId, kHotKeyWaitTimeout, NULL);

    while (GetMessage(&msg, NULL, 0, 0)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
      if (msg.message == WM_HOTKEY) {
        break;
      }
      if (msg.message == WM_TIMER) {
        SetForegroundWindow(window);
        break;
      }
    }

    UnregisterHotKey(m_hWnd, kHotKeyId);
    KillTimer(kHotKeyId);
    DestroyWindow();
    return S_OK;
  }

  LRESULT OnHotKey(UINT msg, WPARAM wp, LPARAM lp, BOOL& handled) {  // NOLINT
    SetForegroundWindow(window_);
    return 1;
  }
 private:
  HWND window_;
};

bool ForceSetForegroundWindow(HWND window) {
  if (GetForegroundWindow() == window)
    return true;
  ForegroundHelperWindow foreground_helper_window;
  HRESULT hr = foreground_helper_window.SetForeground(window);
  return SUCCEEDED(hr);
}

struct PidAndWindow {
  base::ProcessId pid;
  HWND hwnd;
};

BOOL CALLBACK FindWindowInProcessCallback(HWND hwnd, LPARAM param) {
  PidAndWindow* paw = reinterpret_cast<PidAndWindow*>(param);
  base::ProcessId pid;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid == paw->pid && IsWindowVisible(hwnd)) {
    paw->hwnd = hwnd;
    return FALSE;
  }

  return TRUE;
}

bool EnsureProcessInForeground(base::ProcessId process_id) {
  HWND hwnd = GetForegroundWindow();
  base::ProcessId current_foreground_pid = 0;
  DWORD active_thread_id = GetWindowThreadProcessId(hwnd,
      &current_foreground_pid);
  if (current_foreground_pid == process_id)
    return true;

  PidAndWindow paw = { process_id };
  EnumWindows(FindWindowInProcessCallback, reinterpret_cast<LPARAM>(&paw));
  if (!IsWindow(paw.hwnd)) {
    DLOG(ERROR) << "failed to find process window";
    return false;
  }

  bool ret = ForceSetForegroundWindow(paw.hwnd);
  LOG_IF(ERROR, !ret) << "ForceSetForegroundWindow: " << ret;

  return ret;
}

void SendScanCode(short scan_code, Modifier modifiers) {
  DCHECK(-1 != scan_code);

  // High order byte in |scan_code| is SHIFT/CTRL/ALT key state.
  modifiers = static_cast<Modifier>(modifiers | HIBYTE(scan_code));
  DCHECK(modifiers <= ALT);

  // Low order byte in |scan_code| is the actual scan code.
  SendMnemonic(LOBYTE(scan_code), modifiers, false, true);
}

void SendCharA(char c, Modifier modifiers) {
  SendScanCode(VkKeyScanA(c), modifiers);
}

void SendCharW(wchar_t c, Modifier modifiers) {
  SendScanCode(VkKeyScanW(c), modifiers);
}

// Sends a keystroke to the currently active application with optional
// modifiers set.
void SendMnemonic(WORD mnemonic_char, Modifier modifiers, bool extended,
                  bool unicode) {
  INPUT keys[4] = {0};  // Keyboard events
  int key_count = 0;  // Number of generated events

  if (modifiers & SHIFT) {
    keys[key_count].type = INPUT_KEYBOARD;
    keys[key_count].ki.wVk = VK_SHIFT;
    keys[key_count].ki.wScan = MapVirtualKey(VK_SHIFT, 0);
    key_count++;
  }

  if (modifiers & CONTROL) {
    keys[key_count].type = INPUT_KEYBOARD;
    keys[key_count].ki.wVk = VK_CONTROL;
    keys[key_count].ki.wScan = MapVirtualKey(VK_CONTROL, 0);
    key_count++;
  }

  if (modifiers & ALT) {
    keys[key_count].type = INPUT_KEYBOARD;
    keys[key_count].ki.wVk = VK_MENU;
    keys[key_count].ki.wScan = MapVirtualKey(VK_MENU, 0);
    key_count++;
  }

  keys[key_count].type = INPUT_KEYBOARD;
  keys[key_count].ki.wVk = mnemonic_char;
  keys[key_count].ki.wScan = MapVirtualKey(mnemonic_char, 0);

  if (extended)
    keys[key_count].ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
  if (unicode)
    keys[key_count].ki.dwFlags |= KEYEVENTF_UNICODE;
  key_count++;

  bool should_sleep = key_count > 1;

  // Send key downs
  for (int i = 0; i < key_count; i++) {
    SendInput(1, &keys[ i ], sizeof(keys[0]));
    keys[i].ki.dwFlags |= KEYEVENTF_KEYUP;
    if (should_sleep) {
      Sleep(10);
    }
  }

  // Now send key ups in reverse order
  for (int i = key_count; i; i--) {
    SendInput(1, &keys[ i - 1 ], sizeof(keys[0]));
    if (should_sleep) {
      Sleep(10);
    }
  }
}

void SetKeyboardFocusToWindow(HWND window) {
  SendMouseClick(window, 1, 1, LEFT);
}

void SendMouseClick(int x, int y, MouseButton button) {
  // TODO(joshia): Fix this. GetSystemMetrics(SM_CXSCREEN) will
  // retrieve screen size of the primarary monitor only. And monitors
  // arrangement could be pretty arbitrary.
  double screen_width = ::GetSystemMetrics(SM_CXSCREEN) - 1;
  double screen_height = ::GetSystemMetrics(SM_CYSCREEN) - 1;
  double location_x =  x * (65535.0f / screen_width);
  double location_y =  y * (65535.0f / screen_height);

  // Take advantage of button flag bitmask layout
  unsigned int button_flag = MOUSEEVENTF_LEFTDOWN << (button + button);

  INPUT input_info = {0};
  input_info.type = INPUT_MOUSE;
  input_info.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
  input_info.mi.dx = static_cast<LONG>(location_x);
  input_info.mi.dy = static_cast<LONG>(location_y);
  ::SendInput(1, &input_info, sizeof(INPUT));

  Sleep(10);

  input_info.mi.dwFlags = button_flag | MOUSEEVENTF_ABSOLUTE;
  ::SendInput(1, &input_info, sizeof(INPUT));

  Sleep(10);

  input_info.mi.dwFlags = (button_flag << 1) | MOUSEEVENTF_ABSOLUTE;
  ::SendInput(1, &input_info, sizeof(INPUT));
}

void SendMouseClick(HWND window, int x, int y, MouseButton button) {
  if (!IsWindow(window)) {
    NOTREACHED() << "Invalid window handle.";
    return;
  }

  HWND top_level_window = window;
  if (!IsTopLevelWindow(top_level_window)) {
    top_level_window = GetAncestor(window, GA_ROOT);
  }

  ForceSetForegroundWindow(top_level_window);

  POINT cursor_position = {x, y};
  ClientToScreen(window, &cursor_position);
  SendMouseClick(cursor_position.x, cursor_position.y, button);
}

void SendExtendedKey(WORD key, Modifier modifiers) {
  SendMnemonic(key, modifiers, true, false);
}

void SendStringW(const std::wstring& s) {
  for (size_t i = 0; i < s.length(); i++) {
    SendCharW(s[i], NONE);
    Sleep(10);
  }
}

void SendStringA(const std::string& s) {
  for (size_t i = 0; i < s.length(); i++) {
    SendCharA(s[i], NONE);
    Sleep(10);
  }
}

}  // namespace simulate_input

