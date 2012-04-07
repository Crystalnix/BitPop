// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/gamepad/platform_data_fetcher_win.h"

#include "base/debug/trace_event.h"
#include "content/common/gamepad_messages.h"
#include "content/common/gamepad_hardware_buffer.h"

#include <delayimp.h>

#pragma comment(lib, "delayimp.lib")
#pragma comment(lib, "xinput.lib")

namespace content {

using namespace WebKit;

namespace {

// See http://goo.gl/5VSJR. These are not available in all versions of the
// header, but they can be returned from the driver, so we define our own
// versions here.
static const BYTE kDeviceSubTypeGamepad = 1;
static const BYTE kDeviceSubTypeWheel = 2;
static const BYTE kDeviceSubTypeArcadeStick = 3;
static const BYTE kDeviceSubTypeFlightStick = 4;
static const BYTE kDeviceSubTypeDancePad = 5;
static const BYTE kDeviceSubTypeGuitar = 6;
static const BYTE kDeviceSubTypeGuitarAlternate = 7;
static const BYTE kDeviceSubTypeDrumKit = 8;
static const BYTE kDeviceSubTypeGuitarBass = 11;
static const BYTE kDeviceSubTypeArcadePad = 19;

const WebUChar* const GamepadSubTypeName(BYTE sub_type) {
  switch (sub_type) {
    case kDeviceSubTypeGamepad: return L"GAMEPAD";
    case kDeviceSubTypeWheel: return L"WHEEL";
    case kDeviceSubTypeArcadeStick: return L"ARCADE_STICK";
    case kDeviceSubTypeFlightStick: return L"FLIGHT_STICK";
    case kDeviceSubTypeDancePad: return L"DANCE_PAD";
    case kDeviceSubTypeGuitar: return L"GUITAR";
    case kDeviceSubTypeGuitarAlternate: return L"GUITAR_ALTERNATE";
    case kDeviceSubTypeDrumKit: return L"DRUM_KIT";
    case kDeviceSubTypeGuitarBass: return L"GUITAR_BASS";
    case kDeviceSubTypeArcadePad: return L"ARCADE_PAD";
    default: return L"<UNKNOWN>";
  }
}

// Trap only the exceptions that DELAYLOAD can throw, otherwise rethrow.
// See http://msdn.microsoft.com/en-us/library/1c9e046h(v=VS.90).aspx.
LONG WINAPI DelayLoadDllExceptionFilter(PEXCEPTION_POINTERS pExcPointers) {
 LONG disposition = EXCEPTION_EXECUTE_HANDLER;
 switch (pExcPointers->ExceptionRecord->ExceptionCode) {
   case VcppException(ERROR_SEVERITY_ERROR, ERROR_MOD_NOT_FOUND):
   case VcppException(ERROR_SEVERITY_ERROR, ERROR_PROC_NOT_FOUND):
      break;
   default:
      // Exception is not related to delay loading.
      disposition = EXCEPTION_CONTINUE_SEARCH;
      break;
   }
   return disposition;
}

bool EnableXInput() {
  // We have specified DELAYLOAD for xinput1_3.dll. If the DLL is not
  // installed (XP w/o DirectX redist installed), we disable functionality.
  __try {
    ;//XInputEnable(true);
  } __except(DelayLoadDllExceptionFilter(GetExceptionInformation())) {
    return false;
  }
  return true;
}

}

GamepadPlatformDataFetcherWin::GamepadPlatformDataFetcherWin()
    : xinput_available_(EnableXInput()) {
}

void GamepadPlatformDataFetcherWin::GetGamepadData(WebGamepads* pads,
                                        bool devices_changed_hint) {
  TRACE_EVENT0("GAMEPAD", "GetGamepadData");

  // If there's no XInput DLL on the system, early out so that we don't
  // call any other XInput functions.
  if (!xinput_available_) {
    pads->length = 0;
    return;
  }

  pads->length = WebGamepads::itemsLengthCap;

  // If we got notification that system devices have been updated, then
  // run GetCapabilities to update the connected status and the device
  // identifier. It can be slow to do to both GetCapabilities and
  // GetState on unconnected devices, so we want to avoid a 2-5ms pause
  // here by only doing this when the devices are updated (despite
  // documentation claiming it's OK to call it any time).
  if (devices_changed_hint) {
    for (unsigned i = 0; i < WebGamepads::itemsLengthCap; ++i) {
      WebGamepad& pad = pads->items[i];
      TRACE_EVENT1("GAMEPAD", "GetCapabilities", "id", i);
      XINPUT_CAPABILITIES caps;
      DWORD res = XInputGetCapabilities(i, XINPUT_FLAG_GAMEPAD, &caps);
      if (res == ERROR_DEVICE_NOT_CONNECTED) {
        pad.connected = false;
      } else {
        pad.connected = true;
        base::swprintf(pad.id,
                       WebGamepad::idLengthCap,
                       L"Xbox 360 Controller (XInput STANDARD %ls)",
                       GamepadSubTypeName(caps.SubType));
      }
    }
  }

  // We've updated the connection state if necessary, now update the actual
  // data for the devices that are connected.
  for (unsigned i = 0; i < WebGamepads::itemsLengthCap; ++i) {
    WebGamepad& pad = pads->items[i];

    // We rely on device_changed and GetCapabilities to tell us that
    // something's been connected, but we will mark as disconnected if
    // GetState returns that we've lost the pad.
    if (!pad.connected)
      continue;

    XINPUT_STATE state;
    memset(&state, 0, sizeof(XINPUT_STATE));
    TRACE_EVENT_BEGIN1("GAMEPAD", "XInputGetState", "id", i);
    DWORD dwResult = XInputGetState(i, &state);
    TRACE_EVENT_END1("GAMEPAD", "XInputGetState", "id", i);

    if (dwResult == ERROR_SUCCESS) {
      pad.timestamp = state.dwPacketNumber;
      pad.buttonsLength = 0;
#define ADD(b) pad.buttons[pad.buttonsLength++] = \
    ((state.Gamepad.wButtons & (b)) ? 1.0 : 0.0);
      ADD(XINPUT_GAMEPAD_A);
      ADD(XINPUT_GAMEPAD_B);
      ADD(XINPUT_GAMEPAD_X);
      ADD(XINPUT_GAMEPAD_Y);
      ADD(XINPUT_GAMEPAD_LEFT_SHOULDER);
      ADD(XINPUT_GAMEPAD_RIGHT_SHOULDER);
      pad.buttons[pad.buttonsLength++] = state.Gamepad.bLeftTrigger / 255.0;
      pad.buttons[pad.buttonsLength++] = state.Gamepad.bRightTrigger / 255.0;
      ADD(XINPUT_GAMEPAD_BACK);
      ADD(XINPUT_GAMEPAD_START);
      ADD(XINPUT_GAMEPAD_LEFT_THUMB);
      ADD(XINPUT_GAMEPAD_RIGHT_THUMB);
      ADD(XINPUT_GAMEPAD_DPAD_UP);
      ADD(XINPUT_GAMEPAD_DPAD_DOWN);
      ADD(XINPUT_GAMEPAD_DPAD_LEFT);
      ADD(XINPUT_GAMEPAD_DPAD_RIGHT);
#undef ADD
      pad.axesLength = 0;
      // XInput are +up/+right, -down/-left, we want -up/-left.
      pad.axes[pad.axesLength++] = state.Gamepad.sThumbLX / 32767.0;
      pad.axes[pad.axesLength++] = -state.Gamepad.sThumbLY / 32767.0;
      pad.axes[pad.axesLength++] = state.Gamepad.sThumbRX / 32767.0;
      pad.axes[pad.axesLength++] = -state.Gamepad.sThumbRY / 32767.0;
    } else {
      pad.connected = false;
    }
  }
}

} // namespace content
