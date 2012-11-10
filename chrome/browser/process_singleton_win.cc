// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/process_singleton.h"

#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/file_path.h"
#include "base/path_service.h"
#include "base/process_util.h"
#include "base/utf_string_conversions.h"
#include "base/win/scoped_handle.h"
#include "base/win/wrapped_window_proc.h"
#include "chrome/browser/ui/simple_message_box.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/installer/util/wmi.h"
#include "content/public/common/result_codes.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/win/hwnd_util.h"

namespace {

const char kLockfile[] = "lockfile";

// Checks the visibility of the enumerated window and signals once a visible
// window has been found.
BOOL CALLBACK BrowserWindowEnumeration(HWND window, LPARAM param) {
  bool* result = reinterpret_cast<bool*>(param);
  *result = IsWindowVisible(window) != 0;
  // Stops enumeration if a visible window has been found.
  return !*result;
}

// This function thunks to the object's version of the windowproc, taking in
// consideration that there are several messages being dispatched before
// WM_NCCREATE which we let windows handle.
LRESULT CALLBACK ThunkWndProc(HWND hwnd, UINT message,
                              WPARAM wparam, LPARAM lparam) {
  ProcessSingleton* singleton =
      reinterpret_cast<ProcessSingleton*>(ui::GetWindowUserData(hwnd));
  if (message == WM_NCCREATE) {
    CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lparam);
    singleton = reinterpret_cast<ProcessSingleton*>(cs->lpCreateParams);
    CHECK(singleton);
    ui::SetWindowUserData(hwnd, singleton);
  } else if (!singleton) {
    return ::DefWindowProc(hwnd, message, wparam, lparam);
  }
  return singleton->WndProc(hwnd, message, wparam, lparam);
}

bool ParseCommandLine(const COPYDATASTRUCT* cds,
                      CommandLine* parsed_command_line,
                      FilePath* current_directory) {
  // We should have enough room for the shortest command (min_message_size)
  // and also be a multiple of wchar_t bytes. The shortest command
  // possible is L"START\0\0" (empty current directory and command line).
  static const int min_message_size = 7;
  if (cds->cbData < min_message_size * sizeof(wchar_t) ||
      cds->cbData % sizeof(wchar_t) != 0) {
    LOG(WARNING) << "Invalid WM_COPYDATA, length = " << cds->cbData;
    return false;
  }

  // We split the string into 4 parts on NULLs.
  DCHECK(cds->lpData);
  const std::wstring msg(static_cast<wchar_t*>(cds->lpData),
                         cds->cbData / sizeof(wchar_t));
  const std::wstring::size_type first_null = msg.find_first_of(L'\0');
  if (first_null == 0 || first_null == std::wstring::npos) {
    // no NULL byte, don't know what to do
    LOG(WARNING) << "Invalid WM_COPYDATA, length = " << msg.length() <<
      ", first null = " << first_null;
    return false;
  }

  // Decode the command, which is everything until the first NULL.
  if (msg.substr(0, first_null) == L"START") {
    // Another instance is starting parse the command line & do what it would
    // have done.
    VLOG(1) << "Handling STARTUP request from another process";
    const std::wstring::size_type second_null =
        msg.find_first_of(L'\0', first_null + 1);
    if (second_null == std::wstring::npos ||
        first_null == msg.length() - 1 || second_null == msg.length()) {
      LOG(WARNING) << "Invalid format for start command, we need a string in 4 "
        "parts separated by NULLs";
      return false;
    }

    // Get current directory.
    *current_directory = FilePath(msg.substr(first_null + 1,
                                             second_null - first_null));

    const std::wstring::size_type third_null =
        msg.find_first_of(L'\0', second_null + 1);
    if (third_null == std::wstring::npos ||
        third_null == msg.length()) {
      LOG(WARNING) << "Invalid format for start command, we need a string in 4 "
        "parts separated by NULLs";
    }

    // Get command line.
    const std::wstring cmd_line =
        msg.substr(second_null + 1, third_null - second_null);
    *parsed_command_line = CommandLine::FromString(cmd_line);
    return true;
  }
  return false;
}

}  // namespace

// Microsoft's Softricity virtualization breaks the sandbox processes.
// So, if we detect the Softricity DLL we use WMI Win32_Process.Create to
// break out of the virtualization environment.
// http://code.google.com/p/chromium/issues/detail?id=43650
bool ProcessSingleton::EscapeVirtualization(const FilePath& user_data_dir) {
  if (::GetModuleHandle(L"sftldr_wow64.dll") ||
      ::GetModuleHandle(L"sftldr.dll")) {
    int process_id;
    if (!installer::WMIProcess::Launch(GetCommandLineW(), &process_id))
      return false;
    is_virtualized_ = true;
    // The new window was spawned from WMI, and won't be in the foreground.
    // So, first we sleep while the new chrome.exe instance starts (because
    // WaitForInputIdle doesn't work here). Then we poll for up to two more
    // seconds and make the window foreground if we find it (or we give up).
    HWND hwnd = 0;
    ::Sleep(90);
    for (int tries = 200; tries; --tries) {
      hwnd = FindWindowEx(HWND_MESSAGE, NULL, chrome::kMessageWindowClass,
                          user_data_dir.value().c_str());
      if (hwnd) {
        ::SetForegroundWindow(hwnd);
        break;
      }
      ::Sleep(10);
    }
    return true;
  }
  return false;
}

// Look for a Chrome instance that uses the same profile directory.
// If there isn't one, create a message window with its title set to
// the profile directory path.
ProcessSingleton::ProcessSingleton(const FilePath& user_data_dir)
    : window_(NULL), locked_(false), foreground_window_(NULL),
    is_virtualized_(false), lock_file_(INVALID_HANDLE_VALUE) {
  remote_window_ = FindWindowEx(HWND_MESSAGE, NULL,
                                chrome::kMessageWindowClass,
                                user_data_dir.value().c_str());
  if (!remote_window_ && !EscapeVirtualization(user_data_dir)) {
    // Make sure we will be the one and only process creating the window.
    // We use a named Mutex since we are protecting against multi-process
    // access. As documented, it's clearer to NOT request ownership on creation
    // since it isn't guaranteed we will get it. It is better to create it
    // without ownership and explicitly get the ownership afterward.
    std::wstring mutex_name(L"Local\\ChromeProcessSingletonStartup!");
    base::win::ScopedHandle only_me(
        CreateMutex(NULL, FALSE, mutex_name.c_str()));
    DCHECK(only_me.Get() != NULL) << "GetLastError = " << GetLastError();

    // This is how we acquire the mutex (as opposed to the initial ownership).
    DWORD result = WaitForSingleObject(only_me, INFINITE);
    DCHECK(result == WAIT_OBJECT_0) << "Result = " << result <<
        "GetLastError = " << GetLastError();

    // We now own the mutex so we are the only process that can create the
    // window at this time, but we must still check if someone created it
    // between the time where we looked for it above and the time the mutex
    // was given to us.
    remote_window_ = FindWindowEx(HWND_MESSAGE, NULL,
                                  chrome::kMessageWindowClass,
                                  user_data_dir.value().c_str());
    if (!remote_window_) {
      // We have to make sure there is no Chrome instance running on another
      // machine that uses the same profile.
      FilePath lock_file_path = user_data_dir.AppendASCII(kLockfile);
      lock_file_ = CreateFile(lock_file_path.value().c_str(),
                              GENERIC_WRITE,
                              FILE_SHARE_READ,
                              NULL,
                              CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE,
                              NULL);
      DWORD error = GetLastError();
      LOG_IF(WARNING, lock_file_ != INVALID_HANDLE_VALUE &&
          error == ERROR_ALREADY_EXISTS) << "Lock file exists but is writable.";
      LOG_IF(ERROR, lock_file_ == INVALID_HANDLE_VALUE)
          << "Lock file can not be created! Error code: " << error;

      if (lock_file_ != INVALID_HANDLE_VALUE) {
        HINSTANCE hinst = base::GetModuleFromAddress(&ThunkWndProc);

        WNDCLASSEX wc = {0};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = base::win::WrappedWindowProc<ThunkWndProc>;
        wc.hInstance = hinst;
        wc.lpszClassName = chrome::kMessageWindowClass;
        ATOM clazz = ::RegisterClassEx(&wc);
        DCHECK(clazz);

        // Set the window's title to the path of our user data directory so
        // other Chrome instances can decide if they should forward to us.
        window_ = ::CreateWindow(MAKEINTATOM(clazz),
                                 user_data_dir.value().c_str(),
                                 0, 0, 0, 0, 0, HWND_MESSAGE, 0, hinst, this);
        CHECK(window_);
      }
    }
    BOOL success = ReleaseMutex(only_me);
    DCHECK(success) << "GetLastError = " << GetLastError();
  }
}

ProcessSingleton::~ProcessSingleton() {
  // We need to unregister the window as late as possible so that we can detect
  // another instance of chrome running. Otherwise we may end up writing out
  // data while a new chrome is starting up.
  if (window_) {
    ::DestroyWindow(window_);
    ::UnregisterClass(chrome::kMessageWindowClass,
                      base::GetModuleFromAddress(&ThunkWndProc));
  }
  if (lock_file_ != INVALID_HANDLE_VALUE)
    CloseHandle(lock_file_);
}

ProcessSingleton::NotifyResult ProcessSingleton::NotifyOtherProcess() {
  if (is_virtualized_)
    return PROCESS_NOTIFIED;  // We already spawned the process in this case.
  if (lock_file_ == INVALID_HANDLE_VALUE && !remote_window_)
    return LOCK_ERROR;
  else if (!remote_window_)
    return PROCESS_NONE;

  // Found another window, send our command line to it
  // format is "START\0<<<current directory>>>\0<<<commandline>>>".
  std::wstring to_send(L"START\0", 6);  // want the NULL in the string.
  FilePath cur_dir;
  if (!PathService::Get(base::DIR_CURRENT, &cur_dir))
    return PROCESS_NONE;
  to_send.append(cur_dir.value());
  to_send.append(L"\0", 1);  // Null separator.
  to_send.append(GetCommandLineW());
  to_send.append(L"\0", 1);  // Null separator.

  // Allow the current running browser window making itself the foreground
  // window (otherwise it will just flash in the taskbar).
  DWORD process_id = 0;
  DWORD thread_id = GetWindowThreadProcessId(remote_window_, &process_id);
  // It is possible that the process owning this window may have died by now.
  if (!thread_id || !process_id) {
    remote_window_ = NULL;
    return PROCESS_NONE;
  }

  AllowSetForegroundWindow(process_id);

  COPYDATASTRUCT cds;
  cds.dwData = 0;
  cds.cbData = static_cast<DWORD>((to_send.length() + 1) * sizeof(wchar_t));
  cds.lpData = const_cast<wchar_t*>(to_send.c_str());
  DWORD_PTR result = 0;
  if (SendMessageTimeout(remote_window_,
                         WM_COPYDATA,
                         NULL,
                         reinterpret_cast<LPARAM>(&cds),
                         SMTO_ABORTIFHUNG,
                         kTimeoutInSeconds * 1000,
                         &result)) {
    // It is possible that the process owning this window may have died by now.
    if (!result) {
      remote_window_ = NULL;
      return PROCESS_NONE;
    }
    return PROCESS_NOTIFIED;
  }

  // It is possible that the process owning this window may have died by now.
  if (!IsWindow(remote_window_)) {
    remote_window_ = NULL;
    return PROCESS_NONE;
  }

  // The window is hung. Scan for every window to find a visible one.
  bool visible_window = false;
  EnumThreadWindows(thread_id,
                    &BrowserWindowEnumeration,
                    reinterpret_cast<LPARAM>(&visible_window));

  // If there is a visible browser window, ask the user before killing it.
  if (visible_window && chrome::ShowMessageBox(NULL,
      l10n_util::GetStringUTF16(IDS_PRODUCT_NAME),
      l10n_util::GetStringUTF16(IDS_BROWSER_HUNGBROWSER_MESSAGE),
      chrome::MESSAGE_BOX_TYPE_QUESTION) == chrome::MESSAGE_BOX_RESULT_NO) {
    // The user denied. Quit silently.
    return PROCESS_NOTIFIED;
  }

  // Time to take action. Kill the browser process.
  base::KillProcessById(process_id, content::RESULT_CODE_HUNG, true);
  remote_window_ = NULL;
  return PROCESS_NONE;
}

ProcessSingleton::NotifyResult ProcessSingleton::NotifyOtherProcessOrCreate(
    const NotificationCallback& notification_callback) {
  NotifyResult result = NotifyOtherProcess();
  if (result != PROCESS_NONE)
    return result;
  return Create(notification_callback) ? PROCESS_NONE : PROFILE_IN_USE;
}

// On Windows, there is no need to call Create() since the message
// window is created in the constructor but to avoid having more
// platform specific code in browser_main.cc we tolerate calls to
// Create().
bool ProcessSingleton::Create(
    const NotificationCallback& notification_callback) {
  DCHECK(!remote_window_);
  DCHECK(notification_callback_.is_null());

  if (window_ != NULL)
    notification_callback_ = notification_callback;

  return window_ != NULL;
}

void ProcessSingleton::Cleanup() {
}

LRESULT ProcessSingleton::OnCopyData(HWND hwnd, const COPYDATASTRUCT* cds) {
  // If locked, it means we are not ready to process this message because
  // we are probably in a first run critical phase.
  if (locked_) {
#if defined(USE_AURA)
    NOTIMPLEMENTED();
#else
    // Attempt to place ourselves in the foreground / flash the task bar.
    if (foreground_window_ != NULL && IsWindow(foreground_window_)) {
      SetForegroundWindow(foreground_window_);
    } else {
      // Read the command line and store it. It will be replayed when the
      // ProcessSingleton becomes unlocked.
      CommandLine parsed_command_line(CommandLine::NO_PROGRAM);
      FilePath current_directory;
      if (ParseCommandLine(cds, &parsed_command_line, &current_directory))
        saved_startup_messages_.push_back(
            std::make_pair(parsed_command_line.argv(), current_directory));
    }
#endif
    return TRUE;
  }

  CommandLine parsed_command_line(CommandLine::NO_PROGRAM);
  FilePath current_directory;
  if (!ParseCommandLine(cds, &parsed_command_line, &current_directory))
    return TRUE;
  return notification_callback_.Run(parsed_command_line, current_directory) ?
      TRUE : FALSE;
}

LRESULT ProcessSingleton::WndProc(HWND hwnd, UINT message,
                                  WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_COPYDATA:
      return OnCopyData(reinterpret_cast<HWND>(wparam),
                        reinterpret_cast<COPYDATASTRUCT*>(lparam));
    default:
      break;
  }

  return ::DefWindowProc(hwnd, message, wparam, lparam);
}
