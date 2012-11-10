// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/display/multi_display_manager.h"

#include <string>
#include <vector>

#include "ash/display/display_controller.h"
#include "base/command_line.h"
#include "base/stl_util.h"
#include "base/string_split.h"
#include "ui/aura/aura_switches.h"
#include "ui/aura/env.h"
#include "ui/aura/root_window.h"
#include "ui/aura/root_window_host.h"
#include "ui/aura/window_property.h"
#include "ui/gfx/display.h"
#include "ui/gfx/rect.h"

DECLARE_WINDOW_PROPERTY_TYPE(int);

namespace ash {
namespace internal {
namespace {

gfx::Display& GetInvalidDisplay() {
  static gfx::Display* invalid_display = new gfx::Display();
  return *invalid_display;
}

}  // namespace

using aura::RootWindow;
using aura::Window;
using std::string;
using std::vector;

DEFINE_WINDOW_PROPERTY_KEY(int, kDisplayIdKey, -1);

MultiDisplayManager::MultiDisplayManager() {
  Init();
}

MultiDisplayManager::~MultiDisplayManager() {
}

// static
void MultiDisplayManager::AddRemoveDisplay() {
  MultiDisplayManager* manager = static_cast<MultiDisplayManager*>(
      aura::Env::GetInstance()->display_manager());
  manager->AddRemoveDisplayImpl();
}

void MultiDisplayManager::CycleDisplay() {
  MultiDisplayManager* manager = static_cast<MultiDisplayManager*>(
      aura::Env::GetInstance()->display_manager());
  manager->CycleDisplayImpl();
}

void MultiDisplayManager::ToggleDisplayScale() {
  MultiDisplayManager* manager = static_cast<MultiDisplayManager*>(
      aura::Env::GetInstance()->display_manager());
  manager->ScaleDisplayImpl();
}

bool MultiDisplayManager::UpdateWorkAreaOfDisplayNearestWindow(
    const aura::Window* window,
    const gfx::Insets& insets) {
  const RootWindow* root = window->GetRootWindow();
  gfx::Display& display = FindDisplayForRootWindow(root);
  gfx::Rect old_work_area = display.work_area();
  display.UpdateWorkAreaFromInsets(insets);
  return old_work_area != display.work_area();
}

void MultiDisplayManager::OnNativeDisplaysChanged(
    const std::vector<gfx::Display>& new_displays) {
  size_t min = std::min(displays_.size(), new_displays.size());

  // For m19, we only care about 1st display as primary, and
  // don't differentiate the rest of displays as all secondary
  // displays have the same content. ID for primary display stays the same
  // because we never remove it, we don't update IDs for other displays
  // , for now, because they're the same.
  // TODO(oshima): Fix this so that we can differentiate outputs
  // and keep a content on one display stays on the same display
  // when a display is added or removed.
  for (size_t i = 0; i < min; ++i) {
    gfx::Display& current_display = displays_[i];
    const gfx::Display& new_display = new_displays[i];
    if (current_display.bounds_in_pixel() != new_display.bounds_in_pixel() ||
        current_display.device_scale_factor() !=
        new_display.device_scale_factor()) {
      current_display.SetScaleAndBounds(new_display.device_scale_factor(),
                                        new_display.bounds_in_pixel());
      NotifyBoundsChanged(current_display);
    }
  }

  if (displays_.size() < new_displays.size()) {
    // New displays added
    for (size_t i = min; i < new_displays.size(); ++i) {
      const gfx::Display& new_display = new_displays[i];
      displays_.push_back(gfx::Display(new_display.id()));
      gfx::Display& display = displays_.back();
      // Force the primary display's ID to be 0.
      if (i == 0)
        display.set_id(0);
      display.SetScaleAndBounds(new_display.device_scale_factor(),
                                new_display.bounds_in_pixel());
      NotifyDisplayAdded(display);
    }
  } else {
    // Displays are removed. We keep the display for the primary
    // display (at index 0) because it needs the display information
    // even if it doesn't exit.
    while (displays_.size() > new_displays.size() && displays_.size() > 1) {
      Displays::reverse_iterator iter = displays_.rbegin();
      NotifyDisplayRemoved(*iter);
      displays_.erase(iter.base() - 1);
    }
  }
}

RootWindow* MultiDisplayManager::CreateRootWindowForDisplay(
    const gfx::Display& display) {
  RootWindow* root_window = new RootWindow(display.bounds_in_pixel());
  // No need to remove RootWindowObserver because
  // the DisplayManager object outlives RootWindow objects.
  root_window->AddRootWindowObserver(this);
  root_window->SetProperty(kDisplayIdKey, display.id());
  root_window->Init();
  return root_window;
}

gfx::Display* MultiDisplayManager::GetDisplayAt(size_t index) {
  return index < displays_.size() ? &displays_[index] : NULL;
}

size_t MultiDisplayManager::GetNumDisplays() const {
  return displays_.size();
}

const gfx::Display& MultiDisplayManager::GetDisplayNearestWindow(
    const Window* window) const {
  if (!window)
    return displays_[0];
  const RootWindow* root = window->GetRootWindow();
  MultiDisplayManager* manager = const_cast<MultiDisplayManager*>(this);
  return root ? manager->FindDisplayForRootWindow(root) : GetInvalidDisplay();
}

const gfx::Display& MultiDisplayManager::GetDisplayNearestPoint(
    const gfx::Point& point) const {
  if (!DisplayController::IsExtendedDesktopEnabled())
    return displays_[0];
  for (std::vector<gfx::Display>::const_iterator iter = displays_.begin();
       iter != displays_.end(); ++iter) {
    const gfx::Display& display = *iter;
    if (display.bounds().Contains(point))
      return display;
  }
  // Fallback to the primary display if there is no root display containing
  // the |point|.
  return displays_[0];
}

const gfx::Display& MultiDisplayManager::GetDisplayMatching(
    const gfx::Rect& rect) const {
  if (!DisplayController::IsExtendedDesktopEnabled())
    return displays_[0];
  if (rect.IsEmpty())
    return GetDisplayNearestPoint(rect.origin());

  int max = 0;
  const gfx::Display* matching = 0;
  for (std::vector<gfx::Display>::const_iterator iter = displays_.begin();
       iter != displays_.end(); ++iter) {
    const gfx::Display& display = *iter;
    gfx::Rect intersect = display.bounds().Intersect(rect);
    int area = intersect.width() * intersect.height();
    if (area > max) {
      max = area;
      matching = &(*iter);
    }
  }
  // Fallback to the primary display if there is no matching display.
  return matching ? *matching : displays_[0];
}

void MultiDisplayManager::OnRootWindowResized(const aura::RootWindow* root,
                                              const gfx::Size& old_size) {
  if (!use_fullscreen_host_window()) {
    gfx::Display& display = FindDisplayForRootWindow(root);
    display.SetSize(root->GetHostSize());
    NotifyBoundsChanged(display);
  }
}

void MultiDisplayManager::Init() {
  // TODO(oshima): Move this logic to DisplayChangeObserver.
  const string size_str = CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
      switches::kAuraHostWindowSize);
  vector<string> parts;
  base::SplitString(size_str, ',', &parts);
  for (vector<string>::const_iterator iter = parts.begin();
       iter != parts.end(); ++iter) {
    AddDisplayFromSpec(*iter);
  }
  if (displays_.empty())
    AddDisplayFromSpec(std::string() /* default */);
  // Force the 1st display to be the primary display (id == 0).
  displays_[0].set_id(0);
}

void MultiDisplayManager::AddRemoveDisplayImpl() {
  std::vector<gfx::Display> new_displays;
  if (displays_.size() > 1) {
    // Remove if there is more than one display.
    int count = displays_.size() - 1;
    for (Displays::const_iterator iter = displays_.begin(); count-- > 0; ++iter)
      new_displays.push_back(*iter);
  } else {
    // Add if there is only one display.
    new_displays.push_back(displays_[0]);
    new_displays.push_back(CreateDisplayFromSpec("50+50-1280x768"));
  }
  if (new_displays.size())
    OnNativeDisplaysChanged(new_displays);
}

void MultiDisplayManager::CycleDisplayImpl() {
  if (displays_.size() > 1) {
    std::vector<gfx::Display> new_displays;
    for (Displays::const_iterator iter = displays_.begin() + 1;
         iter != displays_.end(); ++iter) {
      gfx::Display display = *iter;
      new_displays.push_back(display);
    }
    new_displays.push_back(displays_.front());
    OnNativeDisplaysChanged(new_displays);
  }
}

void MultiDisplayManager::ScaleDisplayImpl() {
  if (displays_.size() > 0) {
    std::vector<gfx::Display> new_displays;
    for (Displays::const_iterator iter = displays_.begin();
         iter != displays_.end(); ++iter) {
      gfx::Display display = *iter;
      float factor = display.device_scale_factor() == 1.0f ? 2.0f : 1.0f;
      display.SetScaleAndBounds(
          factor, gfx::Rect(display.bounds_in_pixel().origin(),
                            display.size().Scale(factor)));
      new_displays.push_back(display);
    }
    OnNativeDisplaysChanged(new_displays);
  }
}

gfx::Display& MultiDisplayManager::FindDisplayForRootWindow(
    const aura::RootWindow* root_window) {
  int id = root_window->GetProperty(kDisplayIdKey);
  for (Displays::iterator iter = displays_.begin();
       iter != displays_.end(); ++iter) {
    if ((*iter).id() == id)
      return *iter;
  }
  DLOG(FATAL) << "Could not find display by id:" << id;
  return GetInvalidDisplay();
}

void MultiDisplayManager::AddDisplayFromSpec(const std::string& spec) {
  gfx::Display display = CreateDisplayFromSpec(spec);

  if (DisplayController::IsExtendedDesktopEnabled()) {
    const gfx::Insets insets = display.GetWorkAreaInsets();
    const gfx::Rect& native_bounds = display.bounds_in_pixel();
    display.SetScaleAndBounds(display.device_scale_factor(), native_bounds);
    display.UpdateWorkAreaFromInsets(insets);
  }
  displays_.push_back(display);
}

}  // namespace internal
}  // namespace ash
