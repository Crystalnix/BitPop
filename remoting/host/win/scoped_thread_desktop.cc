// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/win/scoped_thread_desktop.h"

#include "base/logging.h"

#include "remoting/host/win/desktop.h"

namespace remoting {

ScopedThreadDesktop::ScopedThreadDesktop()
    : initial_(Desktop::GetThreadDesktop()) {
}

ScopedThreadDesktop::~ScopedThreadDesktop() {
  Revert();
}

bool ScopedThreadDesktop::IsSame(const Desktop& desktop) {
  if (assigned_.get() != NULL) {
    return assigned_->IsSame(desktop);
  } else {
    return initial_->IsSame(desktop);
  }
}

void ScopedThreadDesktop::Revert() {
  if (assigned_.get() != NULL) {
    initial_->SetThreadDesktop();
    assigned_.reset();
  }
}

bool ScopedThreadDesktop::SetThreadDesktop(scoped_ptr<Desktop> desktop) {
  Revert();

  if (initial_->IsSame(*desktop))
    return false;

  if (!desktop->SetThreadDesktop())
    return false;

  assigned_ = desktop.Pass();
  return true;
}

}  // namespace remoting
