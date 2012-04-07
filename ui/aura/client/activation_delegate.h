// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_AURA_CLIENT_ACTIVATION_DELEGATE_H_
#define UI_AURA_CLIENT_ACTIVATION_DELEGATE_H_
#pragma once

#include "ui/aura/aura_export.h"

namespace aura {
class Event;
class Window;
namespace client {

// A property key to store the activation delegate for a window. The type of the
// value is |aura::ActivationDelegate*|.
AURA_EXPORT extern const char kActivationDelegateKey[];

// An interface implemented by an object that configures and responds to changes
// to a window's activation state.
class AURA_EXPORT ActivationDelegate {
 public:
  // Returns true if the window should be activated. |event| is either the mouse
  // event supplied if the activation is the result of a mouse, or the touch
  // event if the activation is the result of a touch, or NULL if activation is
  // attempted for another reason.
  virtual bool ShouldActivate(Event* event) = 0;

  // Sent when the window is activated.
  virtual void OnActivated() = 0;

  // Sent when the window loses active status.
  virtual void OnLostActive() = 0;

 protected:
  virtual ~ActivationDelegate() {}
};

// Sets/Gets the ActivationDelegate on the Window. No ownership changes.
AURA_EXPORT void SetActivationDelegate(Window* window,
                                       ActivationDelegate* delegate);
AURA_EXPORT ActivationDelegate* GetActivationDelegate(Window* window);

}  // namespace client
}  // namespace aura

#endif  // UI_AURA_CLIENT_ACTIVATION_DELEGATE_H_
