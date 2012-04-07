// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_LAUNCHER_LAUNCHER_H_
#define ASH_LAUNCHER_LAUNCHER_H_
#pragma once

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "ash/ash_export.h"

namespace aura {
class Window;
}

namespace views {
class Widget;
}

namespace ash {

class LauncherModel;

class ASH_EXPORT Launcher {
 public:
  explicit Launcher(aura::Window* window_container);
  ~Launcher();

  // Sets the width of the status area.
  void SetStatusWidth(int width);
  int GetStatusWidth();

  LauncherModel* model() { return model_.get(); }
  views::Widget* widget() { return widget_; }

  aura::Window* window_container() { return window_container_; }

 private:
  class DelegateView;

  // If necessary asks the delegate if an entry should be created in the
  // launcher for |window|. This only asks the delegate once for a window.
  void MaybeAdd(aura::Window* window);

  scoped_ptr<LauncherModel> model_;

  // Widget hosting the view.  May be hidden if we're not using a launcher,
  // e.g. Aura compact window mode.
  views::Widget* widget_;

  aura::Window* window_container_;

  // Contents view of the widget. Houses the LauncherView.
  DelegateView* delegate_view_;

  DISALLOW_COPY_AND_ASSIGN(Launcher);
};

}  // namespace ash

#endif  // ASH_LAUNCHER_LAUNCHER_H_
