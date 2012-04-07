// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/system/input_device_settings.h"

#include <stdarg.h>
#include <string>
#include <vector>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/message_loop.h"
#include "base/process_util.h"
#include "base/stringprintf.h"
#include "chrome/browser/chromeos/system/runtime_environment.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace chromeos {
namespace system {

namespace {
const char kTpControl[] = "/opt/google/touchpad/tpcontrol";
const char kMouseControl[] = "/opt/google/mouse/mousecontrol";

bool ScriptExists(const std::string& script) {
  return file_util::PathExists(FilePath(script));
}

// Executes the input control script asynchronously, if it exists.
void ExecuteScriptOnFileThread(const std::vector<std::string>& argv) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
  DCHECK(!argv.empty());
  const std::string& script(argv[0]);

  // Script must exist on device.
  DCHECK(!runtime_environment::IsRunningOnChromeOS() || ScriptExists(script));

  if (!ScriptExists(script))
    return;

  base::LaunchOptions options;
  options.wait = true;
  base::LaunchProcess(CommandLine(argv), options, NULL);
}

void ExecuteScript(int argc, ...) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  std::vector<std::string> argv;
  va_list vl;
  va_start(vl, argc);
  for (int i = 0; i < argc; ++i) {
    argv.push_back(va_arg(vl, const char*));
  }
  va_end(vl);

  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      base::Bind(&ExecuteScriptOnFileThread, argv));
}

void SetPointerSensitivity(const char* script, int value) {
  DCHECK(value > 0 && value < 6);
  ExecuteScript(3, script, "sensitivity", StringPrintf("%d", value).c_str());
}

bool DeviceExists(const char* script) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
  if (!ScriptExists(script))
    return false;

  std::vector<std::string> argv;
  argv.push_back(script);
  argv.push_back("status");
  std::string output;
  // Output is empty if the device is not found.
  return base::GetAppOutput(CommandLine(argv), &output) && !output.empty();
}

}  // namespace

namespace pointer_settings {

void SetSensitivity(int value) {
  SetPointerSensitivity(kTpControl, value);
  SetPointerSensitivity(kMouseControl, value);
}

}  // namespace pointer_settings

namespace touchpad_settings {

bool TouchpadExists() {
  // We only need to do this check once, assuming no pluggable touchpad devices.
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
  static bool init = false;
  static bool exists = false;

  if (!init) {
    init = true;
    exists = DeviceExists(kTpControl);
  }
  return exists;
}

void SetTapToClick(bool enabled) {
  ExecuteScript(3, kTpControl, "taptoclick", enabled ? "on" : "off");
}

}  // namespace touchpad_settings

namespace mouse_settings {

bool MouseExists() {
  return DeviceExists(kMouseControl);
}

void SetPrimaryButtonRight(bool right) {
  ExecuteScript(3, kMouseControl, "swap_left_right", right ? "1" : "0");
}

}  // namespace mouse_settings

}  // namespace system
}  // namespace chromeos
