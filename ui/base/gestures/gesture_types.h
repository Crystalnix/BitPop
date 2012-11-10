// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_BASE_GESTURES_GESTURE_TYPES_H_
#define UI_BASE_GESTURES_GESTURE_TYPES_H_

#include "base/logging.h"
#include "base/time.h"
#include "ui/base/events.h"
#include "ui/gfx/rect.h"

namespace ui {

struct UI_EXPORT GestureEventDetails {
 public:
  GestureEventDetails(EventType type, float delta_x, float delta_y);

  EventType type() const { return type_; }

  int touch_points() const { return touch_points_; }
  void set_touch_points(int touch_points) { touch_points_ = touch_points; }

  const gfx::Rect& bounding_box() const { return bounding_box_; }
  void set_bounding_box(const gfx::Rect& box) { bounding_box_ = box; }

  float scroll_x() const {
    CHECK_EQ(ui::ET_GESTURE_SCROLL_UPDATE, type_);
    return data.scroll.x;
  }
  float scroll_y() const {
    CHECK_EQ(ui::ET_GESTURE_SCROLL_UPDATE, type_);
    return data.scroll.y;
  }

  float velocity_x() const {
    CHECK_EQ(ui::ET_SCROLL_FLING_START, type_);
    return data.velocity.x;
  }
  float velocity_y() const {
    CHECK_EQ(ui::ET_SCROLL_FLING_START, type_);
    return data.velocity.y;
  }

  int touch_id() const {
    CHECK_EQ(ui::ET_GESTURE_LONG_PRESS, type_);
    return data.touch_id;
  }

  float scale() const {
    CHECK_EQ(ui::ET_GESTURE_PINCH_UPDATE, type_);
    return data.scale;
  }

  bool swipe_left() const {
    CHECK_EQ(ui::ET_GESTURE_MULTIFINGER_SWIPE, type_);
    return data.swipe.left;
  }
  bool swipe_right() const {
    CHECK_EQ(ui::ET_GESTURE_MULTIFINGER_SWIPE, type_);
    return data.swipe.right;
  }
  bool swipe_up() const {
    CHECK_EQ(ui::ET_GESTURE_MULTIFINGER_SWIPE, type_);
    return data.swipe.up;
  }
  bool swipe_down() const {
    CHECK_EQ(ui::ET_GESTURE_MULTIFINGER_SWIPE, type_);
    return data.swipe.down;
  }

  int tap_count() const {
    CHECK_EQ(ui::ET_GESTURE_TAP, type_);
    return data.tap_count;
  }

 private:
  ui::EventType type_;
  union {
    struct {  // SCROLL delta.
      float x;
      float y;
    } scroll;

    float scale;  // PINCH scale.

    struct {  // FLING velocity.
      float x;
      float y;
    } velocity;

    int touch_id;  // LONG_PRESS touch-id.

    struct {  // SWIPE direction.
      bool left;
      bool right;
      bool up;
      bool down;
    } swipe;

    int tap_count;  // TAP repeat count.

    struct {
      float delta_x;
      float delta_y;
    } generic;
  } data;

  int touch_points_;  // Number of active touch points in the gesture.

  // Bounding box is an axis-aligned rectangle that contains all the
  // enclosing rectangles of the touch-points in the gesture.
  gfx::Rect bounding_box_;
};

// An abstract type to represent touch-events. The gesture-recognizer uses this
// interface to communicate with the touch-events.
class UI_EXPORT TouchEvent {
 public:
  virtual ~TouchEvent() {}

  virtual EventType GetEventType() const = 0;
  virtual gfx::Point GetLocation() const = 0;
  virtual int GetTouchId() const = 0;
  virtual int GetEventFlags() const = 0;
  virtual base::TimeDelta GetTimestamp() const = 0;
  virtual float RadiusX() const = 0;
  virtual float RadiusY() const = 0;
  virtual float RotationAngle() const = 0;
  virtual float Force() const = 0;
};

// An abstract type to represent gesture-events.
class UI_EXPORT GestureEvent {
 public:
  virtual ~GestureEvent() {}

  // A gesture event can have multiple touches. This function should return the
  // lowest ID of the touches in this gesture.
  virtual int GetLowestTouchId() const = 0;

  // A helper function used in several (all) derived classes.
  // Returns lowest set bit, or -1 if no bits are set.
  static int LowestBit(unsigned int bitfield) {
    int i = -1;
    // Find the index of the least significant 1 bit
    while (bitfield && (!((1 << ++i) & bitfield)));
    return i;
  }
};

// An abstract type for consumers of gesture-events created by the
// gesture-recognizer.
class UI_EXPORT GestureConsumer {
 public:
  GestureConsumer()
      : ignores_events_(false) {
  }

  explicit GestureConsumer(bool ignores_events)
      : ignores_events_(ignores_events) {
  }

  virtual ~GestureConsumer() {}

  // TODO: this is a hack! GestureRecognizer should never expose the internal
  // marker object that implements this.
  bool ignores_events() { return ignores_events_; }

 private:
  const bool ignores_events_;
};

// GestureEventHelper creates implementation-specific gesture events and
// can dispatch them.
class UI_EXPORT GestureEventHelper {
 public:
  virtual ~GestureEventHelper() {
  }

  // |flags| is ui::EventFlags. The meaning of |param_first| and |param_second|
  // depends on the specific gesture type (|type|).
  virtual GestureEvent* CreateGestureEvent(const GestureEventDetails& details,
                                           const gfx::Point& location,
                                           int flags,
                                           base::Time time,
                                           unsigned int touch_id_bitfield) = 0;

  virtual TouchEvent* CreateTouchEvent(EventType type,
                                       const gfx::Point& location,
                                       int touch_id,
                                       base::TimeDelta time_stamp) = 0;

  virtual bool DispatchLongPressGestureEvent(GestureEvent* event) = 0;
  virtual bool DispatchCancelTouchEvent(TouchEvent* event) = 0;
};

}  // namespace ui

#endif  // UI_BASE_GESTURES_GESTURE_TYPES_H_
