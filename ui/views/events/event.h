// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_EVENTS_EVENT_H_
#define UI_VIEWS_EVENTS_EVENT_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/time.h"
#include "ui/base/events.h"
#include "ui/base/gestures/gesture_types.h"
#include "ui/base/keycodes/keyboard_codes.h"
#include "ui/gfx/point.h"
#include "ui/views/views_export.h"

namespace ui {
class OSExchangeData;
}

#if defined(USE_AURA)
namespace aura {
class Event;
}
#endif

namespace views {

#if defined(USE_AURA)
typedef aura::Event* NativeEvent;
#else
typedef base::NativeEvent NativeEvent;
#endif

class View;

namespace internal {
class RootView;
}

////////////////////////////////////////////////////////////////////////////////
//
// Event class
//
// An event encapsulates an input event that can be propagated into view
// hierarchies. An event has a type, some flags and a time stamp.
//
// Each major event type has a corresponding Event subclass.
//
// Events are immutable but support copy
//
////////////////////////////////////////////////////////////////////////////////
class VIEWS_EXPORT Event {
 public:
  const NativeEvent& native_event() const { return native_event_; }
  ui::EventType type() const { return type_; }
  const base::Time& time_stamp() const { return time_stamp_; }

  // Required for Gesture testing purposes.
  void set_time_stamp(base::Time time_stamp) { time_stamp_ = time_stamp; }

  int flags() const { return flags_; }
  void set_flags(int flags) { flags_ = flags; }

  // The following methods return true if the respective keys were pressed at
  // the time the event was created.
  bool IsShiftDown() const { return (flags_ & ui::EF_SHIFT_DOWN) != 0; }
  bool IsControlDown() const { return (flags_ & ui::EF_CONTROL_DOWN) != 0; }
  bool IsCapsLockDown() const { return (flags_ & ui::EF_CAPS_LOCK_DOWN) != 0; }
  bool IsAltDown() const { return (flags_ & ui::EF_ALT_DOWN) != 0; }

  bool IsMouseEvent() const {
    return type_ == ui::ET_MOUSE_PRESSED ||
           type_ == ui::ET_MOUSE_DRAGGED ||
           type_ == ui::ET_MOUSE_RELEASED ||
           type_ == ui::ET_MOUSE_MOVED ||
           type_ == ui::ET_MOUSE_ENTERED ||
           type_ == ui::ET_MOUSE_EXITED ||
           type_ == ui::ET_MOUSEWHEEL;
  }

  bool IsTouchEvent() const {
    return type_ == ui::ET_TOUCH_RELEASED ||
           type_ == ui::ET_TOUCH_PRESSED ||
           type_ == ui::ET_TOUCH_MOVED ||
           type_ == ui::ET_TOUCH_STATIONARY ||
           type_ == ui::ET_TOUCH_CANCELLED;
  }

  bool IsScrollGestureEvent() const {
    return type_ == ui::ET_GESTURE_SCROLL_BEGIN ||
           type_ == ui::ET_GESTURE_SCROLL_UPDATE ||
           type_ == ui::ET_GESTURE_SCROLL_END;
  }

 protected:
  Event(ui::EventType type, int flags);
  Event(const NativeEvent& native_event, ui::EventType type, int flags);
  Event(const Event& model)
      : native_event_(model.native_event()),
        type_(model.type()),
        time_stamp_(model.time_stamp()),
        flags_(model.flags()) {
  }

  void set_type(ui::EventType type) { type_ = type; }

 private:
  void operator=(const Event&);

  NativeEvent native_event_;
  ui::EventType type_;
  base::Time time_stamp_;
  int flags_;
};

////////////////////////////////////////////////////////////////////////////////
//
// LocatedEvent class
//
// A generic event that is used for any events that is located at a specific
// position in the screen.
//
////////////////////////////////////////////////////////////////////////////////
class VIEWS_EXPORT LocatedEvent : public Event {
 public:
  int x() const { return location_.x(); }
  int y() const { return location_.y(); }
  const gfx::Point& location() const { return location_; }

 protected:
  explicit LocatedEvent(const NativeEvent& native_event);

  // TODO(msw): Kill this legacy constructor when we update uses.
  // Simple initialization from cracked metadata.
  LocatedEvent(ui::EventType type, const gfx::Point& location, int flags);

  // Create a new LocatedEvent which is identical to the provided model.
  // If source / target views are provided, the model location will be converted
  // from |source| coordinate system to |target| coordinate system.
  LocatedEvent(const LocatedEvent& model, View* source, View* target);

  // This constructor is to allow converting the location of an event from the
  // widget's coordinate system to the RootView's coordinate system.
  LocatedEvent(const LocatedEvent& model, View* root);

  gfx::Point location_;
};

////////////////////////////////////////////////////////////////////////////////
//
// MouseEvent class
//
// A mouse event is used for any input event related to the mouse.
//
////////////////////////////////////////////////////////////////////////////////
class VIEWS_EXPORT MouseEvent : public LocatedEvent {
 public:
  explicit MouseEvent(const NativeEvent& native_event);
  // Create a new MouseEvent which is identical to the provided model.
  // If source / target views are provided, the model location will be converted
  // from |source| coordinate system to |target| coordinate system.
  MouseEvent(const MouseEvent& model, View* source, View* target);

  // TODO(msw): Kill this legacy constructor when we update uses.
  // Create a new mouse event
  MouseEvent(ui::EventType type, int x, int y, int flags)
      : LocatedEvent(type, gfx::Point(x, y), flags) {
  }

  // Conveniences to quickly test what button is down
  bool IsOnlyLeftMouseButton() const {
    return (flags() & ui::EF_LEFT_MOUSE_BUTTON) &&
      !(flags() & (ui::EF_MIDDLE_MOUSE_BUTTON | ui::EF_RIGHT_MOUSE_BUTTON));
  }

  bool IsLeftMouseButton() const {
    return (flags() & ui::EF_LEFT_MOUSE_BUTTON) != 0;
  }

  bool IsOnlyMiddleMouseButton() const {
    return (flags() & ui::EF_MIDDLE_MOUSE_BUTTON) &&
      !(flags() & (ui::EF_LEFT_MOUSE_BUTTON | ui::EF_RIGHT_MOUSE_BUTTON));
  }

  bool IsMiddleMouseButton() const {
    return (flags() & ui::EF_MIDDLE_MOUSE_BUTTON) != 0;
  }

  bool IsOnlyRightMouseButton() const {
    return (flags() & ui::EF_RIGHT_MOUSE_BUTTON) &&
      !(flags() & (ui::EF_LEFT_MOUSE_BUTTON | ui::EF_MIDDLE_MOUSE_BUTTON));
  }

  bool IsRightMouseButton() const {
    return (flags() & ui::EF_RIGHT_MOUSE_BUTTON) != 0;
  }

 protected:
  MouseEvent(const MouseEvent& model, View* root)
      : LocatedEvent(model, root) {
  }

 private:
  friend class internal::RootView;

  DISALLOW_COPY_AND_ASSIGN(MouseEvent);
};

////////////////////////////////////////////////////////////////////////////////
//
// TouchEvent class
//
// A touch event is generated by touch screen and advanced track
// pad devices. There is a deliberate direct correspondence between
// TouchEvent and PlatformTouchPoint.
//
////////////////////////////////////////////////////////////////////////////////
class VIEWS_EXPORT TouchEvent : public LocatedEvent,
                                public ui::TouchEvent {
 public:
  explicit TouchEvent(const NativeEvent& native_event);

  // Create a new touch event.
  TouchEvent(ui::EventType type,
             int x,
             int y,
             int flags,
             int touch_id,
             float radius_x,
             float radius_y,
             float angle,
             float force);

  // Create a new TouchEvent which is identical to the provided model.
  // If source / target views are provided, the model location will be converted
  // from |source| coordinate system to |target| coordinate system.
  TouchEvent(const TouchEvent& model, View* source, View* target);

  virtual ~TouchEvent();

  int identity() const { return touch_id_; }

  float radius_x() const { return radius_x_; }
  float radius_y() const { return radius_y_; }
  float rotation_angle() const { return rotation_angle_; }
  float force() const { return force_; }

  // Overridden from ui::TouchEvent.
  virtual ui::EventType GetEventType() const OVERRIDE;
  virtual gfx::Point GetLocation() const OVERRIDE;
  virtual int GetTouchId() const OVERRIDE;
  virtual int GetEventFlags() const OVERRIDE;
  virtual base::TimeDelta GetTimestamp() const OVERRIDE;
  virtual float RadiusX() const OVERRIDE;
  virtual float RadiusY() const OVERRIDE;
  virtual float RotationAngle() const OVERRIDE;
  virtual float Force() const OVERRIDE;

 private:
  friend class internal::RootView;

  TouchEvent(const TouchEvent& model, View* root);

  // The identity (typically finger) of the touch starting at 0 and incrementing
  // for each separable additional touch that the hardware can detect.
  const int touch_id_;

  // Radius of the X (major) axis of the touch ellipse. 1.0 if unknown.
  const float radius_x_;

  // Radius of the Y (minor) axis of the touch ellipse. 1.0 if unknown.
  const float radius_y_;

  // Angle of the major axis away from the X axis. Default 0.0.
  const float rotation_angle_;

  // Force (pressure) of the touch. Normalized to be [0, 1]. Default to be 0.0.
  const float force_;

  DISALLOW_COPY_AND_ASSIGN(TouchEvent);
};

////////////////////////////////////////////////////////////////////////////////
// KeyEvent class
//
//  KeyEvent encapsulates keyboard input events - key press and release.
//
////////////////////////////////////////////////////////////////////////////////
class VIEWS_EXPORT KeyEvent : public Event {
 public:
  explicit KeyEvent(const NativeEvent& native_event);

  // Creates a new KeyEvent synthetically (i.e. not in response to an input
  // event from the host environment). This is typically only used in testing as
  // some metadata obtainable from the underlying native event is not present.
  // It's also used by input methods to fabricate keyboard events.
  KeyEvent(ui::EventType type, ui::KeyboardCode key_code, int event_flags);

  ui::KeyboardCode key_code() const { return key_code_; }

  // These setters allow an I18N virtual keyboard to fabricate a keyboard event
  // which does not have a corresponding ui::KeyboardCode (example: U+00E1 Latin
  // small letter A with acute, U+0410 Cyrillic capital letter A.)
  // GetCharacter() and GetUnmodifiedCharacter() return the character.
  void set_character(uint16 character) { character_ = character; }
  void set_unmodified_character(uint16 unmodified_character) {
    unmodified_character_ = unmodified_character;
  }

  // Gets the character generated by this key event. It only supports Unicode
  // BMP characters.
  uint16 GetCharacter() const;

  // Gets the character generated by this key event ignoring concurrently-held
  // modifiers (except shift).
  uint16 GetUnmodifiedCharacter() const;

 private:
  ui::KeyboardCode key_code_;

  uint16 character_;
  uint16 unmodified_character_;

  DISALLOW_COPY_AND_ASSIGN(KeyEvent);
};

class ScrollEvent;

////////////////////////////////////////////////////////////////////////////////
//
// MouseWheelEvent class
//
// A MouseWheelEvent is used to propagate mouse wheel user events.
// Note: e.GetOffset() > 0 means scroll up / left.
//
////////////////////////////////////////////////////////////////////////////////
class VIEWS_EXPORT MouseWheelEvent : public MouseEvent {
 public:
  // See |offset| for details.
  static const int kWheelDelta;

  explicit MouseWheelEvent(const NativeEvent& native_event);
  explicit MouseWheelEvent(const ScrollEvent& scroll_event);

  // The amount to scroll. This is in multiples of kWheelDelta.
  int offset() const { return offset_; }

 private:
  friend class internal::RootView;

  MouseWheelEvent(const MouseWheelEvent& model, View* root)
      : MouseEvent(model, root),
        offset_(model.offset_) {
  }

  int offset_;

  DISALLOW_COPY_AND_ASSIGN(MouseWheelEvent);
};

////////////////////////////////////////////////////////////////////////////////
//
// DropTargetEvent class
//
// A DropTargetEvent is sent to the view the mouse is over during a drag and
// drop operation.
//
////////////////////////////////////////////////////////////////////////////////
class VIEWS_EXPORT DropTargetEvent : public LocatedEvent {
 public:
  DropTargetEvent(const ui::OSExchangeData& data,
                  int x,
                  int y,
                  int source_operations)
      : LocatedEvent(ui::ET_DROP_TARGET_EVENT, gfx::Point(x, y), 0),
        data_(data),
        source_operations_(source_operations) {
    // TODO(msw): Hook up key state flags for CTRL + drag and drop, etc.
  }

  const ui::OSExchangeData& data() const { return data_; }
  int source_operations() const { return source_operations_; }

 private:
  // Data associated with the drag/drop session.
  const ui::OSExchangeData& data_;

  // Bitmask of supported ui::DragDropTypes::DragOperation by the source.
  int source_operations_;

  DISALLOW_COPY_AND_ASSIGN(DropTargetEvent);
};

class VIEWS_EXPORT ScrollEvent : public MouseEvent {
 public:
  explicit ScrollEvent(const NativeEvent& native_event);

  float x_offset() const { return x_offset_; }
  float y_offset() const { return y_offset_; }

 private:
  friend class internal::RootView;

  ScrollEvent(const ScrollEvent& model, View* root)
      : MouseEvent(model, root),
        x_offset_(model.x_offset()),
        y_offset_(model.y_offset()) {
  }

  float x_offset_;
  float y_offset_;

  DISALLOW_COPY_AND_ASSIGN(ScrollEvent);
};

////////////////////////////////////////////////////////////////////////////////
// GestureEvent class
//
////////////////////////////////////////////////////////////////////////////////
class VIEWS_EXPORT GestureEvent : public LocatedEvent,
                                  public ui::GestureEvent {
 public:
  explicit GestureEvent(const NativeEvent& native_event);

  // Create a new GestureEvent which is identical to the provided model.
  // If source / target views are provided, the model location will be converted
  // from |source| coordinate system to |target| coordinate system.
  GestureEvent(const GestureEvent& model, View* source, View* target);

  virtual ~GestureEvent();

  const ui::GestureEventDetails& details() const { return details_; }

 protected:
  GestureEvent(ui::EventType type, int x, int y, int flags);

 private:
  friend class internal::RootView;

  GestureEvent(const GestureEvent& model, View* root);

  // Overridden from ui::GestureEvent.
  virtual int GetLowestTouchId() const OVERRIDE;

  ui::GestureEventDetails details_;

  DISALLOW_COPY_AND_ASSIGN(GestureEvent);
};

class VIEWS_EXPORT GestureEventForTest : public GestureEvent {
 public:
  GestureEventForTest(ui::EventType type, int x, int y, int flags);

 private:
  DISALLOW_COPY_AND_ASSIGN(GestureEventForTest);
};

#if defined(OS_WIN)
int GetModifiersFromKeyState();
#endif

}  // namespace views

#endif  // UI_VIEWS_EVENTS_EVENT_H_
