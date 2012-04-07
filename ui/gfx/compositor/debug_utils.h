// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_GFX_COMPOSITOR_DEBUG_UTILS_H_
#define UI_GFX_COMPOSITOR_DEBUG_UTILS_H_
#pragma once

#ifndef NDEBUG

#include "ui/gfx/compositor/compositor_export.h"

namespace gfx {
class Point;
}

namespace ui {

class Layer;

// Log the layer hierarchy. Mark layers which contain |mouse_location| with '*'.
COMPOSITOR_EXPORT void PrintLayerHierarchy(const Layer* layer,
                                           gfx::Point mouse_location);

} // namespace ui

#endif // NDEBUG

#endif  // UI_GFX_COMPOSITOR_DEBUG_UTILS_H_
