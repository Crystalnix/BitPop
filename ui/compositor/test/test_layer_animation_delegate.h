// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_GFX_COMPOSITOR_TEST_TEST_LAYER_ANIMATION_DELEGATE_H_
#define UI_GFX_COMPOSITOR_TEST_TEST_LAYER_ANIMATION_DELEGATE_H_

#include "base/compiler_specific.h"
#include "ui/compositor/layer_animation_delegate.h"
#include "ui/gfx/rect.h"
#include "ui/gfx/transform.h"

namespace ui {

class TestLayerAnimationDelegate : public LayerAnimationDelegate {
 public:
  TestLayerAnimationDelegate();
  explicit TestLayerAnimationDelegate(const LayerAnimationDelegate& other);
  virtual ~TestLayerAnimationDelegate();

  // Implementation of LayerAnimationDelegate
  virtual void SetBoundsFromAnimation(const gfx::Rect& bounds) OVERRIDE;
  virtual void SetTransformFromAnimation(const Transform& transform) OVERRIDE;
  virtual void SetOpacityFromAnimation(float opacity) OVERRIDE;
  virtual void SetVisibilityFromAnimation(bool visibility) OVERRIDE;
  virtual void SetBrightnessFromAnimation(float brightness) OVERRIDE;
  virtual void SetGrayscaleFromAnimation(float grayscale) OVERRIDE;
  virtual void ScheduleDrawForAnimation() OVERRIDE;
  virtual const gfx::Rect& GetBoundsForAnimation() const OVERRIDE;
  virtual const Transform& GetTransformForAnimation() const OVERRIDE;
  virtual float GetOpacityForAnimation() const OVERRIDE;
  virtual bool GetVisibilityForAnimation() const OVERRIDE;
  virtual float GetBrightnessForAnimation() const OVERRIDE;
  virtual float GetGrayscaleForAnimation() const OVERRIDE;

 private:
  gfx::Rect bounds_;
  Transform transform_;
  float opacity_;
  bool visibility_;
  float brightness_;
  float grayscale_;

  // Allow copy and assign.
};

}  // namespace ui

#endif  // UI_GFX_COMPOSITOR_TEST_TEST_LAYER_ANIMATION_DELEGATE_H_
