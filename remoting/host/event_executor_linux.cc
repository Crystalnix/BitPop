// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/event_executor.h"

#include <X11/Xlib.h>
#include <X11/XF86keysym.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/message_loop.h"
#include "remoting/proto/internal.pb.h"

namespace remoting {

using protocol::MouseEvent;
using protocol::KeyEvent;

namespace {

// A class to generate events on Linux.
class EventExecutorLinux : public EventExecutor {
 public:
  EventExecutorLinux(MessageLoop* message_loop, Capturer* capturer);
  virtual ~EventExecutorLinux() {};

  bool Init();

  virtual void InjectKeyEvent(const KeyEvent& event) OVERRIDE;
  virtual void InjectMouseEvent(const MouseEvent& event) OVERRIDE;

 private:
  void InjectScrollWheelClicks(int button, int count);

  MessageLoop* message_loop_;
  Capturer* capturer_;

  // X11 graphics context.
  Display* display_;
  Window root_window_;
  int width_;
  int height_;

  int test_event_base_;
  int test_error_base_;

  DISALLOW_COPY_AND_ASSIGN(EventExecutorLinux);
};

int MouseButtonToX11ButtonNumber(MouseEvent::MouseButton button) {
  switch (button) {
    case MouseEvent::BUTTON_LEFT:
      return 1;

    case MouseEvent::BUTTON_RIGHT:
      return 3;

    case MouseEvent::BUTTON_MIDDLE:
      return 2;

    case MouseEvent::BUTTON_UNDEFINED:
    default:
      return -1;
  }
}

int HorizontalScrollWheelToX11ButtonNumber(int dx) {
  return (dx > 0 ? 6 : 7);
}


int VerticalScrollWheelToX11ButtonNumber(int dy) {
  // Positive y-values are wheel scroll-up events (button 4), negative y-values
  // are wheel scroll-down events (button 5).
  return (dy > 0 ? 4 : 5);
}

// Hard-coded mapping from Virtual Key codes to X11 KeySyms.
// This mapping is only valid if both client and host are using a
// US English keyboard layout.
// Because we're passing VK codes on the wire, with no Scancode,
// "extended" flag, etc, things like distinguishing left & right
// Shift keys doesn't work.
//
// TODO(wez): Replace this with something more closely tied to what
// WebInputEventFactory does on Linux/GTK, and which respects the
// host's keyboard layout (see http://crbug.com/74550 ).
const int kUsVkeyToKeysym[256] = {
  // 0x00 - 0x07
  -1, -1, -1, -1,
  // 0x04 - 0x07
  -1, -1, -1, -1,
  // 0x08 - 0x0B
  XK_BackSpace, XK_Tab, -1, -1,
  // 0x0C - 0x0F
  XK_Clear, XK_Return, -1, -1,

  // 0x10 - 0x13
  XK_Shift_L, XK_Control_L, XK_Alt_L, XK_Pause,
  // 0x14 - 0x17
  XK_Caps_Lock, XK_Kana_Shift, -1, /* VKEY_JUNJA */ -1,
  // 0x18 - 0x1B
  /* VKEY_FINAL */ -1, XK_Kanji, -1, XK_Escape,
  // 0x1C - 0x1F
  XK_Henkan, XK_Muhenkan, /* VKEY_ACCEPT */ -1, XK_Mode_switch,

  // 0x20 - 0x23
  XK_space, XK_Prior, XK_Next, XK_End,
  // 0x24 - 0x27
  XK_Home, XK_Left, XK_Up, XK_Right,
  // 0x28 - 0x2B
  XK_Down, XK_Select, /* VK_PRINT */ -1, XK_Execute,
  // 0x2C - 0x2F
  XK_Print, XK_Insert, XK_Delete, XK_Help,

  // 0x30 - 0x33
  XK_0, XK_1, XK_2, XK_3,
  // 0x34 - 0x37
  XK_4, XK_5, XK_6, XK_7,
  // 0x38 - 0x3B
  XK_8, XK_9, -1, -1,
  // 0x3C - 0x3F
  -1, -1, -1, -1,

  // 0x40 - 0x43
  -1, XK_A, XK_B, XK_C,
  // 0x44 - 0x47
  XK_D, XK_E, XK_F, XK_G,
  // 0x48 - 0x4B
  XK_H, XK_I, XK_J, XK_K,
  // 0x4C - 0x4F
  XK_L, XK_M, XK_N, XK_O,

  // 0x50 - 0x53
  XK_P, XK_Q, XK_R, XK_S,
  // 0x54 - 0x57
  XK_T, XK_U, XK_V, XK_W,
  // 0x58 - 0x5B
  XK_X, XK_Y, XK_Z, XK_Super_L,
  // 0x5C - 0x5F
  XK_Super_R, XK_Menu, -1, /* VKEY_SLEEP */ -1,

  // 0x60 - 0x63
  XK_KP_0, XK_KP_1, XK_KP_2, XK_KP_3,
  // 0x64 - 0x67
  XK_KP_4, XK_KP_5, XK_KP_6, XK_KP_7,
  // 0x68 - 0x6B
  XK_KP_8, XK_KP_9, XK_KP_Multiply, XK_KP_Add,
  // 0x6C - 0x6F
  XK_KP_Separator, XK_KP_Subtract, XK_KP_Decimal, XK_KP_Divide,

  // 0x70 - 0x73
  XK_F1, XK_F2, XK_F3, XK_F4,
  // 0x74 - 0x77
  XK_F5, XK_F6, XK_F7, XK_F8,
  // 0x78 - 0x7B
  XK_F9, XK_F10, XK_F11, XK_F12,
  // 0x7C - 0x7F
  XK_F13, XK_F14, XK_F15, XK_F16,

  // 0x80 - 0x83
  XK_F17, XK_F18, XK_F19, XK_F20,
  // 0x84 - 0x87
  XK_F21, XK_F22, XK_F23, XK_F24,
  // 0x88 - 0x8B
  -1, -1, -1, -1,
  // 0x8C - 0x8F
  -1, -1, -1, -1,

  // 0x90 - 0x93
  XK_Num_Lock, XK_Scroll_Lock, -1, -1,
  // 0x94 - 0x97
  -1, -1, -1, -1,
  // 0x98 - 0x9B
  -1, -1, -1, -1,
  // 0x9C - 0x9F
  -1, -1, -1, -1,

  // 0xA0 - 0xA3
  XK_Num_Lock, XK_Scroll_Lock, XK_Control_L, XK_Control_R,
  // 0xA4 - 0xA7
  XK_Meta_L, XK_Meta_R, XF86XK_Back, XF86XK_Forward,
  // 0xA8 - 0xAB
  XF86XK_Refresh, XF86XK_Stop, XF86XK_Search, XF86XK_Favorites,
  // 0xAC - 0xAF
  XF86XK_HomePage, XF86XK_AudioMute, XF86XK_AudioLowerVolume,
  XF86XK_AudioRaiseVolume,

  // 0xB0 - 0xB3
  XF86XK_AudioNext, XF86XK_AudioPrev, XF86XK_AudioStop, XF86XK_AudioPause,
  // 0xB4 - 0xB7
  XF86XK_Mail, XF86XK_AudioMedia, XF86XK_Launch0, XF86XK_Launch1,
  // 0xB8 - 0xBB
  -1, -1, XK_semicolon, XK_plus,
  // 0xBC - 0xBF
  XK_comma, XK_minus, XK_period, XK_slash,

  // 0xC0 - 0xC3
  XK_grave, -1, -1, -1,
  // 0xC4 - 0xC7
  -1, -1, -1, -1,
  // 0xC8 - 0xCB
  -1, -1, -1, -1,
  // 0xCC - 0xCF
  -1, -1, -1, -1,

  // 0xD0 - 0xD3
  -1, -1, -1, -1,
  // 0xD4 - 0xD7
  -1, -1, -1, -1,
  // 0xD8 - 0xDB
  -1, -1, -1, XK_bracketleft,
  // 0xDC - 0xDF
  XK_backslash, XK_bracketright, XK_apostrophe,
  /* VKEY_OEM_8 */ -1,

  // 0xE0 - 0xE3
  -1, -1, /* VKEY_OEM_102 */ -1, -1,
  // 0xE4 - 0xE7
  -1, /* VKEY_PROCESSKEY */ -1, -1, /* VKEY_PACKET */ -1,
  // 0xE8 - 0xEB
  -1, -1, -1, -1,
  // 0xEC - 0xEF
  -1, -1, -1, -1,

  // 0xF0 - 0xF3
  -1, -1, -1, -1,
  // 0xF4 - 0xF7
  -1, -1, /* VKEY_ATTN */ -1, /* VKEY_CRSEL */ -1,
  // 0xF8 - 0xFB
  /* VKEY_EXSEL */ -1, /* VKEY_EREOF */ -1, /* VKEY_PLAY */ -1,
  /* VKEY_ZOOM */ -1,
  // 0xFC - 0xFF
  /* VKEY_NONAME */ -1, /* VKEY_PA1 */ -1, /* VKEY_OEM_CLEAR */ -1, -1
};

int ChromotocolKeycodeToX11Keysym(int32_t keycode) {
  if (keycode < 0 || keycode > 255) {
    return -1;
  }

  return kUsVkeyToKeysym[keycode];
}

EventExecutorLinux::EventExecutorLinux(MessageLoop* message_loop,
                                       Capturer* capturer)
    : message_loop_(message_loop),
      capturer_(capturer),
      display_(XOpenDisplay(NULL)),
      root_window_(BadValue),
      width_(0),
      height_(0) {
}

bool EventExecutorLinux::Init() {
  CHECK(display_);

  root_window_ = RootWindow(display_, DefaultScreen(display_));
  if (root_window_ == BadValue) {
    LOG(ERROR) << "Unable to get the root window";
    return false;
  }

  // TODO(ajwong): Do we want to check the major/minor version at all for XTest?
  int major = 0;
  int minor = 0;
  if (!XTestQueryExtension(display_, &test_event_base_, &test_error_base_,
                           &major, &minor)) {
    LOG(ERROR) << "Server does not support XTest.";
    return false;
  }

  // Grab the width and height so we can figure out if mouse moves are out of
  // range.
  XWindowAttributes root_attr;
  // TODO(ajwong): Handle resolution changes.
  if (!XGetWindowAttributes(display_, root_window_, &root_attr)) {
    LOG(ERROR) << "Unable to get window attributes";
    return false;
  }

  width_ = root_attr.width;
  height_ = root_attr.height;
  return true;
}

void EventExecutorLinux::InjectKeyEvent(const KeyEvent& event) {
  if (MessageLoop::current() != message_loop_) {
    message_loop_->PostTask(
        FROM_HERE,
        base::Bind(&EventExecutorLinux::InjectKeyEvent, base::Unretained(this),
                   event));
    return;
  }

  // TODO(ajwong): This will only work for QWERTY keyboards.
  int keysym = ChromotocolKeycodeToX11Keysym(event.keycode());

  if (keysym == -1) {
    LOG(WARNING) << "Ignoring unknown key: " << event.keycode();
    return;
  }

  // Translate the keysym into a keycode understandable by the X display.
  int keycode = XKeysymToKeycode(display_, keysym);
  if (keycode == 0) {
    LOG(WARNING) << "Ignoring undefined keysym: " << keysym
                 << " for key: " << event.keycode();
    return;
  }

  VLOG(3) << "Got pepper key: " << event.keycode()
          << " sending keysym: " << keysym
          << " to keycode: " << keycode;
  XTestFakeKeyEvent(display_, keycode, event.pressed(), CurrentTime);
  XFlush(display_);
}

void EventExecutorLinux::InjectScrollWheelClicks(int button, int count) {
  for (int i = 0; i < count; i++) {
    // Generate a button-down and a button-up to simulate a wheel click.
    XTestFakeButtonEvent(display_, button, true, CurrentTime);
    XTestFakeButtonEvent(display_, button, false, CurrentTime);
  }
  XFlush(display_);
}

void EventExecutorLinux::InjectMouseEvent(const MouseEvent& event) {
  if (MessageLoop::current() != message_loop_) {
    message_loop_->PostTask(
        FROM_HERE,
        base::Bind(&EventExecutorLinux::InjectMouseEvent,
                   base::Unretained(this), event));
    return;
  }

  if (event.has_x() && event.has_y()) {
    if (event.x() < 0 || event.y() < 0 ||
        event.x() > width_ || event.y() > height_) {
      // A misbehaving client may send these. Drop events that are out of range.
      // TODO(ajwong): How can we log this sanely? We don't want to DOS the
      // server with a misbehaving client by logging like crazy.
      return;
    }

    VLOG(3) << "Moving mouse to " << event.x()
            << "," << event.y();
    XTestFakeMotionEvent(display_, DefaultScreen(display_),
                         event.x(), event.y(),
                         CurrentTime);
    XFlush(display_);
  }

  if (event.has_button() && event.has_button_down()) {
    int button_number = MouseButtonToX11ButtonNumber(event.button());

    if (button_number < 0) {
      LOG(WARNING) << "Ignoring unknown button type: " << event.button();
      return;
    }

    VLOG(3) << "Button " << event.button()
            << " received, sending "
            << (event.button_down() ? "down " : "up ")
            << button_number;
    XTestFakeButtonEvent(display_, button_number, event.button_down(),
                         CurrentTime);
    XFlush(display_);
  }

  if (event.has_wheel_offset_y() && event.wheel_offset_y() != 0) {
    int dy = event.wheel_offset_y();
    InjectScrollWheelClicks(VerticalScrollWheelToX11ButtonNumber(dy), abs(dy));
  }
  if (event.has_wheel_offset_x() && event.wheel_offset_x() != 0) {
    int dx = event.wheel_offset_x();
    InjectScrollWheelClicks(HorizontalScrollWheelToX11ButtonNumber(dx),
                            abs(dx));
  }
}

}  // namespace

EventExecutor* EventExecutor::Create(MessageLoop* message_loop,
                                     Capturer* capturer) {
  EventExecutorLinux* executor = new EventExecutorLinux(message_loop, capturer);
  if (!executor->Init()) {
    delete executor;
    executor = NULL;
  }
  return executor;
}

}  // namespace remoting
