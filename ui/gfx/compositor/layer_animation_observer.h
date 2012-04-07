// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_GFX_COMPOSITOR_LAYER_ANIMATION_OBSERVER_H_
#define UI_GFX_COMPOSITOR_LAYER_ANIMATION_OBSERVER_H_
#pragma once

#include <set>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "ui/gfx/compositor/compositor_export.h"

namespace ui {

class LayerAnimationSequence;
class ScopedLayerAnimationSettings;

// LayerAnimationObservers are notified when animations complete.
class COMPOSITOR_EXPORT LayerAnimationObserver  {
 public:
  // Called when the |sequence| ends. Not called if |sequence| is aborted.
  virtual void OnLayerAnimationEnded(
      const LayerAnimationSequence* sequence) = 0;

  // Called if |sequence| is aborted for any reason. Should never do anything
  // that may cause another animation to be started.
  virtual void OnLayerAnimationAborted(
      const LayerAnimationSequence* sequence) = 0;

  // Called when the animation is scheduled.
  virtual void OnLayerAnimationScheduled(
      const LayerAnimationSequence* sequence) = 0;

  // If the animator is destroyed during an animation, the animations are
  // aborted. The resulting NotifyAborted notifications will NOT be sent to
  // this observer if this function returns false. NOTE: IF YOU OVERRIDE THIS
  // FUNCTION TO RETURN TRUE, YOU MUST REMEMBER TO REMOVE YOURSELF AS AN
  // OBSERVER WHEN YOU ARE DESTROYED.
  virtual bool RequiresNotificationWhenAnimatorDestroyed() const;

 protected:
  LayerAnimationObserver();
  virtual ~LayerAnimationObserver();

 private:
  friend class LayerAnimationSequence;

  // Called when |this| is added to |sequence|'s observer list.
  void AttachedToSequence(LayerAnimationSequence* sequence);

  // Called when |this| is removed to |sequence|'s observer list.
  void DetachedFromSequence(LayerAnimationSequence* sequence);

  std::set<LayerAnimationSequence*> attached_sequences_;
};

// An implicit animation observer is intended to be used in conjunction with a
// ScopedLayerAnimationSettings object in order to receive a notification when
// all implicit animations complete.
class COMPOSITOR_EXPORT ImplicitAnimationObserver
    : public LayerAnimationObserver {
 public:
  ImplicitAnimationObserver();
  virtual ~ImplicitAnimationObserver();

  virtual void OnImplicitAnimationsCompleted() = 0;

 private:
  friend class ScopedLayerAnimationSettings;

  // OnImplicitAnimationsCompleted is not fired unless the observer is active.
  bool active() const { return active_; }
  void SetActive(bool active);

  // LayerAnimationObserver implementation
  virtual void OnLayerAnimationEnded(
      const LayerAnimationSequence* sequence) OVERRIDE;
  virtual void OnLayerAnimationAborted(
      const LayerAnimationSequence* sequence) OVERRIDE;
  virtual void OnLayerAnimationScheduled(
      const LayerAnimationSequence* sequence) OVERRIDE;

  void CheckCompleted();

  bool active_;

  // This tracks the number of scheduled animations that have yet to complete.
  // If this value is zero, and the observer is active, then
  // OnImplicitAnimationsCompleted is fired.
  size_t animation_count_;
};

}  // namespace ui

#endif  // UI_GFX_COMPOSITOR_LAYER_ANIMATION_OBSERVER_H_
