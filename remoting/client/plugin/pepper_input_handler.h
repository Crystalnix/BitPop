// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_CLIENT_PLUGIN_PEPPER_INPUT_HANDLER_H_
#define REMOTING_CLIENT_PLUGIN_PEPPER_INPUT_HANDLER_H_

#include "remoting/client/input_handler.h"

struct PP_InputEvent_Character;
struct PP_InputEvent_Key;
struct PP_InputEvent_Mouse;

namespace remoting {

class PepperInputHandler : public InputHandler {
 public:
  PepperInputHandler(ClientContext* context,
                     protocol::ConnectionToHost* connection,
                     ChromotingView* view);
  virtual ~PepperInputHandler();

  virtual void Initialize();

  void HandleKeyEvent(bool keydown, const PP_InputEvent_Key& event);
  void HandleCharacterEvent(const PP_InputEvent_Character& event);

  void HandleMouseMoveEvent(const PP_InputEvent_Mouse& event);
  void HandleMouseButtonEvent(bool button_down,
                              const PP_InputEvent_Mouse& event);

 private:
  DISALLOW_COPY_AND_ASSIGN(PepperInputHandler);
};

}  // namespace remoting

DISABLE_RUNNABLE_METHOD_REFCOUNT(remoting::PepperInputHandler);

#endif  // REMOTING_CLIENT_PLUGIN_PEPPER_INPUT_HANDLER_H_
