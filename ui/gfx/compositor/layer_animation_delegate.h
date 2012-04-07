// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_GFX_COMPOSITOR_LAYER_ANIMATION_DELEGATE_H_
#define UI_GFX_COMPOSITOR_LAYER_ANIMATION_DELEGATE_H_
#pragma once

#include "ui/gfx/rect.h"
#include "ui/gfx/transform.h"
#include "ui/gfx/compositor/compositor_export.h"

namespace ui {

// Layer animations interact with the layers using this interface.
class COMPOSITOR_EXPORT LayerAnimationDelegate {
 public:
  virtual void SetBoundsFromAnimation(const gfx::Rect& bounds) = 0;
  virtual void SetTransformFromAnimation(const Transform& transform) = 0;
  virtual void SetOpacityFromAnimation(float opacity) = 0;
  virtual void ScheduleDrawForAnimation() = 0;
  virtual const gfx::Rect& GetBoundsForAnimation() const = 0;
  virtual const Transform& GetTransformForAnimation() const = 0;
  virtual float GetOpacityForAnimation() const = 0;

 protected:
  virtual ~LayerAnimationDelegate() {}
};

}  // namespace ui

#endif  // UI_GFX_COMPOSITOR_LAYER_ANIMATION_DELEGATE_H_
