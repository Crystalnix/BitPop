// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/aura/env.h"

#include "base/command_line.h"
#include "ui/aura/client/screen_position_client.h"
#include "ui/aura/env_observer.h"
#include "ui/aura/event_filter.h"
#include "ui/aura/display_manager.h"
#include "ui/aura/root_window_host.h"
#include "ui/aura/window.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/compositor_switches.h"

#if defined(USE_X11)
#include "ui/aura/display_change_observer_x11.h"
#endif

namespace aura {

// static
Env* Env::instance_ = NULL;

////////////////////////////////////////////////////////////////////////////////
// Env, public:

Env::Env()
    : mouse_button_flags_(0),
      is_touch_down_(false),
      render_white_bg_(true),
      stacking_client_(NULL) {
}

Env::~Env() {
  ui::Compositor::Terminate();
}

// static
Env* Env::GetInstance() {
  if (!instance_) {
    instance_ = new Env;
    instance_->Init();
  }
  return instance_;
}

// static
void Env::DeleteInstance() {
  delete instance_;
  instance_ = NULL;
}

void Env::AddObserver(EnvObserver* observer) {
  observers_.AddObserver(observer);
}

void Env::RemoveObserver(EnvObserver* observer) {
  observers_.RemoveObserver(observer);
}

void Env::SetLastMouseLocation(const Window& window,
                               const gfx::Point& location_in_root) {
  last_mouse_location_ = location_in_root;
  client::ScreenPositionClient* client =
      client::GetScreenPositionClient(window.GetRootWindow());
  if (client)
    client->ConvertPointToScreen(&window, &last_mouse_location_);
}

void Env::SetDisplayManager(DisplayManager* display_manager) {
  display_manager_.reset(display_manager);
#if defined(USE_X11)
  // Update the display manager with latest info.
  display_change_observer_->NotifyDisplayChange();
#endif
}

void Env::SetEventFilter(EventFilter* event_filter) {
  event_filter_.reset(event_filter);
}

#if !defined(OS_MACOSX)
MessageLoop::Dispatcher* Env::GetDispatcher() {
  return dispatcher_.get();
}
#endif

////////////////////////////////////////////////////////////////////////////////
// Env, private:

void Env::Init() {
#if !defined(OS_MACOSX)
  dispatcher_.reset(CreateDispatcher());
#endif
#if defined(USE_X11)
  display_change_observer_.reset(new internal::DisplayChangeObserverX11);
#endif
  ui::Compositor::Initialize(
      CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kUIEnableThreadedCompositing));
}

void Env::NotifyWindowInitialized(Window* window) {
  FOR_EACH_OBSERVER(EnvObserver, observers_, OnWindowInitialized(window));
}

}  // namespace aura
