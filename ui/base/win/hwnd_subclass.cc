// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/base/win/hwnd_subclass.h"

#include <algorithm>

#include "base/logging.h"
#include "base/memory/scoped_vector.h"
#include "base/memory/singleton.h"
#include "ui/base/win/hwnd_util.h"

namespace {
const char kHWNDSubclassKey[] = "__UI_BASE_WIN_HWND_SUBCLASS_PROC__";

LRESULT CALLBACK WndProc(HWND hwnd,
                         UINT message,
                         WPARAM w_param,
                         LPARAM l_param) {
  ui::HWNDSubclass* wrapped_wnd_proc =
      reinterpret_cast<ui::HWNDSubclass*>(
          ui::ViewProp::GetValue(hwnd, kHWNDSubclassKey));
  return wrapped_wnd_proc ? wrapped_wnd_proc->OnWndProc(hwnd,
                                                        message,
                                                        w_param,
                                                        l_param)
                          : DefWindowProc(hwnd, message, w_param, l_param);
}

WNDPROC GetCurrentWndProc(HWND target) {
  return reinterpret_cast<WNDPROC>(GetWindowLong(target, GWL_WNDPROC));
}

}  // namespace

namespace ui {

// Singleton factory that creates and manages the lifetime of all
// ui::HWNDSubclass objects.
class HWNDSubclass::HWNDSubclassFactory {
 public:
  static HWNDSubclassFactory* GetInstance() {
    return Singleton<HWNDSubclassFactory,
        LeakySingletonTraits<HWNDSubclassFactory> >::get();
  }

  // Returns a non-null HWNDSubclass corresponding to the HWND |target|. Creates
  // one if none exists. Retains ownership of the returned pointer.
  HWNDSubclass* GetHwndSubclassForTarget(HWND target) {
    DCHECK(target);
    HWNDSubclass* subclass = reinterpret_cast<HWNDSubclass*>(
        ui::ViewProp::GetValue(target, kHWNDSubclassKey));
    if (!subclass) {
      subclass = new ui::HWNDSubclass(target);
      hwnd_subclasses_.push_back(subclass);
    }
    return subclass;
  }

  const ScopedVector<HWNDSubclass>& hwnd_subclasses() {
    return hwnd_subclasses_;
  }

 private:
  friend struct DefaultSingletonTraits<HWNDSubclassFactory>;

  HWNDSubclassFactory() {}

  ScopedVector<HWNDSubclass> hwnd_subclasses_;

  DISALLOW_COPY_AND_ASSIGN(HWNDSubclassFactory);
};

// static
void HWNDSubclass::AddFilterToTarget(HWND target, HWNDMessageFilter* filter) {
  HWNDSubclassFactory::GetInstance()->GetHwndSubclassForTarget(
      target)->AddFilter(filter);
}

// static
void HWNDSubclass::RemoveFilterFromAllTargets(HWNDMessageFilter* filter) {
  HWNDSubclassFactory* factory = HWNDSubclassFactory::GetInstance();
  ScopedVector<ui::HWNDSubclass>::const_iterator it;
  for (it = factory->hwnd_subclasses().begin();
      it != factory->hwnd_subclasses().end(); ++it)
    (*it)->RemoveFilter(filter);
}

// static
HWNDSubclass* HWNDSubclass::GetHwndSubclassForTarget(HWND target) {
  return HWNDSubclassFactory::GetInstance()->GetHwndSubclassForTarget(target);
}

void HWNDSubclass::AddFilter(HWNDMessageFilter* filter) {
  DCHECK(filter);
  if (std::find(filters_.begin(), filters_.end(), filter) == filters_.end())
    filters_.push_back(filter);
}

void HWNDSubclass::RemoveFilter(HWNDMessageFilter* filter) {
  std::vector<HWNDMessageFilter*>::iterator it =
      std::find(filters_.begin(), filters_.end(), filter);
  if (it != filters_.end())
    filters_.erase(it);
}

HWNDSubclass::HWNDSubclass(HWND target)
    : target_(target),
      original_wnd_proc_(GetCurrentWndProc(target)),
      ALLOW_THIS_IN_INITIALIZER_LIST(prop_(target, kHWNDSubclassKey, this)) {
  ui::SetWindowProc(target_, &WndProc);
}

HWNDSubclass::~HWNDSubclass() {
}

LRESULT HWNDSubclass::OnWndProc(HWND hwnd,
                                UINT message,
                                WPARAM w_param,
                                LPARAM l_param) {
  for (std::vector<HWNDMessageFilter*>::iterator it = filters_.begin();
      it != filters_.end(); ++it) {
    LRESULT l_result = 0;
    if ((*it)->FilterMessage(hwnd, message, w_param, l_param, &l_result))
      return l_result;
  }

  // In most cases, |original_wnd_proc_| will take care of calling
  // DefWindowProc.
  return CallWindowProc(original_wnd_proc_, hwnd, message, w_param, l_param);
}

HWNDMessageFilter::~HWNDMessageFilter() {
  HWNDSubclass::RemoveFilterFromAllTargets(this);
}

}  // namespace ui
