// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AURA_CHROME_SHELL_DELEGATE_H_
#define CHROME_BROWSER_UI_VIEWS_AURA_CHROME_SHELL_DELEGATE_H_
#pragma once

#include "ash/launcher/launcher_types.h"
#include "ash/shell_delegate.h"
#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"

class StatusAreaHostAura;
class StatusAreaView;

namespace views {
class View;
}

class ChromeShellDelegate : public ash::ShellDelegate {
 public:
  ChromeShellDelegate();
  virtual ~ChromeShellDelegate();

  static ChromeShellDelegate* instance() { return instance_; }

  StatusAreaHostAura* status_area_host_for_test() {
    return status_area_host_.get();
  }

  StatusAreaView* GetStatusArea();

  // ash::ShellDelegate overrides;
  virtual views::Widget* CreateStatusArea() OVERRIDE;
#if defined(OS_CHROMEOS)
  virtual void LockScreen() OVERRIDE;
#endif
  virtual void Exit() OVERRIDE;
  virtual void BuildAppListModel(ash::AppListModel* model) OVERRIDE;
  virtual ash::AppListViewDelegate* CreateAppListViewDelegate() OVERRIDE;
  virtual std::vector<aura::Window*> GetCycleWindowList(
      CycleSource source,
      CycleOrder order) const OVERRIDE;
  virtual void CreateNewWindow() OVERRIDE;
  virtual void LauncherItemClicked(const ash::LauncherItem& item) OVERRIDE;
  virtual int GetBrowserShortcutResourceId() OVERRIDE;
  virtual string16 GetLauncherItemTitle(const ash::LauncherItem& item) OVERRIDE;

 private:
  static ChromeShellDelegate* instance_;

  scoped_ptr<StatusAreaHostAura> status_area_host_;

  DISALLOW_COPY_AND_ASSIGN(ChromeShellDelegate);
};

#endif  // CHROME_BROWSER_UI_VIEWS_AURA_CHROME_SHELL_DELEGATE_H_
