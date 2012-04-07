// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_WM_PROPERTY_UTIL_H_
#define ASH_WM_PROPERTY_UTIL_H_
#pragma once

#include "ash/ash_export.h"

namespace aura {
class Window;
}

namespace gfx {
class Rect;
}

namespace ash {

// Sets the restore bounds property on |window|. Deletes existing bounds value
// if exists.
ASH_EXPORT void SetRestoreBounds(aura::Window* window, const gfx::Rect& bounds);

// Same as SetRestoreBounds(), but does nothing if the restore bounds have
// already been set. The bounds used are the bounds of the window.
ASH_EXPORT void SetRestoreBoundsIfNotSet(aura::Window* window);

// Returns the restore bounds property on |window|. NULL if the
// restore bounds property does not exist for |window|. |window|
// owns the bounds object.
ASH_EXPORT const gfx::Rect* GetRestoreBounds(aura::Window* window);

// Deletes and clears the restore bounds property on |window|.
ASH_EXPORT void ClearRestoreBounds(aura::Window* window);

}

#endif  // ASH_WM_PROPERTY_UTIL_H_
