// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/base/animation/tween.h"

#include <math.h>

#if defined(OS_WIN)
#include <float.h>
#endif

#include "base/logging.h"
#include "ui/gfx/interpolated_transform.h"

namespace ui {

// static
double Tween::CalculateValue(Tween::Type type, double state) {
  DCHECK_GE(state, 0);
  DCHECK_LE(state, 1);

  switch (type) {
    case EASE_IN:
      return pow(state, 2);

    case EASE_IN_OUT:
      if (state < 0.5)
        return pow(state * 2, 2) / 2.0;
      return 1.0 - (pow((state - 1.0) * 2, 2) / 2.0);

    case FAST_IN_OUT:
      return (pow(state - 0.5, 3) + 0.125) / 0.25;

    case LINEAR:
      return state;

    case EASE_OUT_SNAP:
      state = 0.95 * (1.0 - pow(1.0 - state, 2));
      return state;

    case EASE_OUT:
      return 1.0 - pow(1.0 - state, 2);

    case SMOOTH_IN_OUT:
      return sin(state);

    case ZERO:
      return 0;
  }

  NOTREACHED();
  return state;
}

// static
double Tween::ValueBetween(double value, double start, double target) {
  return start + (target - start) * value;
}

// static
int Tween::ValueBetween(double value, int start, int target) {
  if (start == target)
    return start;
  double delta = static_cast<double>(target - start);
  if (delta < 0)
    delta--;
  else
    delta++;
#if defined(OS_WIN)
  return start + static_cast<int>(value * _nextafter(delta, 0));
#else
  return start + static_cast<int>(value * nextafter(delta, 0));
#endif
}

// static
gfx::Rect Tween::ValueBetween(double value,
                              const gfx::Rect& start_bounds,
                              const gfx::Rect& target_bounds) {
  return gfx::Rect(ValueBetween(value, start_bounds.x(), target_bounds.x()),
                   ValueBetween(value, start_bounds.y(), target_bounds.y()),
                   ValueBetween(value, start_bounds.width(),
                                target_bounds.width()),
                   ValueBetween(value, start_bounds.height(),
                                target_bounds.height()));
}

// static
Transform Tween::ValueBetween(double value,
                              const Transform& start_transform,
                              const Transform& end_transform) {
  if (value >= 1.0)
    return end_transform;
  if (value <= 0.0)
    return start_transform;

  Transform to_return;
  gfx::Point start_translation, end_translation;
  float start_rotation, end_rotation;
  gfx::Point3f start_scale, end_scale;
  if (InterpolatedTransform::FactorTRS(start_transform,
                                       &start_translation,
                                       &start_rotation,
                                       &start_scale) &&
      InterpolatedTransform::FactorTRS(end_transform,
                                       &end_translation,
                                       &end_rotation,
                                       &end_scale)) {
    to_return.SetScale(ValueBetween(value, start_scale.x(), end_scale.x()),
                       ValueBetween(value, start_scale.y(), end_scale.y()));
    to_return.ConcatRotate(ValueBetween(value, start_rotation, end_rotation));
    to_return.ConcatTranslate(
        ValueBetween(value, start_translation.x(), end_translation.x()),
        ValueBetween(value, start_translation.y(), end_translation.y()));
  } else {
    for (int row = 0; row < 4; ++row) {
      for (int col = 0; col < 4; ++col) {
        to_return.matrix().set(row, col,
            ValueBetween(value,
                         start_transform.matrix().get(row, col),
                         end_transform.matrix().get(row, col)));
      }
    }
  }

  return to_return;
}

}  // namespace ui
