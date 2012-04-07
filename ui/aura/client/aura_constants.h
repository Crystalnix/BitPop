// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_AURA_CLIENT_AURA_CONSTANTS_H_
#define UI_AURA_CLIENT_AURA_CONSTANTS_H_
#pragma once

#include "ui/aura/aura_export.h"

namespace aura {
namespace client {

// Alphabetical sort.

// A property key to store always-on-top flag. The type of the value is boolean.
AURA_EXPORT extern const char kAlwaysOnTopKey[];

// A property key to store whether animations are disabled for the window. Type
// of value is an int.
AURA_EXPORT extern const char kAnimationsDisabledKey[];

// A property key to store the boolean property of window modality.
AURA_EXPORT extern const char kModalKey[];

// A property key to store the restore bounds for a window. The type
// of the value is |gfx::Rect*|.
AURA_EXPORT extern const char kRestoreBoundsKey[];

// A property key to store an input method object that handles a key event. The
// type of the value is |ui::InputMethod*|.
AURA_EXPORT extern const char kRootWindowInputMethod[];

// A property key to store ui::WindowShowState for a window.
// See ui/base/ui_base_types.h for its definition.
AURA_EXPORT extern const char kShowStateKey[];

// Alphabetical sort.

}  // namespace client
}  // namespace aura

#endif  // UI_AURA_CLIENT_AURA_CONSTANTS_H_
