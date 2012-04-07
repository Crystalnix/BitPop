// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>

#include "ash/launcher/launcher.h"
#include "ash/launcher/launcher_model.h"
#include "ash/launcher/launcher_types.h"
#include "ash/shell.h"
#include "ash/shell_delegate.h"
#include "ash/shell_factory.h"
#include "ash/shell_window_ids.h"
#include "ash/shell/example_factory.h"
#include "ash/shell/shell_main_parts.h"
#include "ash/shell/toplevel_window.h"
#include "ash/wm/window_util.h"
#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "grit/ui_resources.h"
#include "ui/aura/root_window.h"
#include "ui/aura/window_observer.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_paths.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/compositor/test/compositor_test_support.h"
#include "ui/views/test/test_views_delegate.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

namespace {

class ShellViewsDelegate : public views::TestViewsDelegate {
 public:
  ShellViewsDelegate() {}
  virtual ~ShellViewsDelegate() {}

  // Overridden from views::TestViewsDelegate:
  virtual views::NonClientFrameView* CreateDefaultNonClientFrameView(
      views::Widget* widget) OVERRIDE {
    return ash::Shell::GetInstance()->CreateDefaultNonClientFrameView(widget);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(ShellViewsDelegate);
};

// WindowWatcher is responsible for listening for newly created windows and
// creating items on the Launcher for them.
class WindowWatcher : public aura::WindowObserver {
 public:
  WindowWatcher()
      : window_(ash::Shell::GetInstance()->launcher()->window_container()) {
    window_->AddObserver(this);
  }

  virtual ~WindowWatcher() {
    window_->RemoveObserver(this);
  }

  aura::Window* GetWindowByID(ash::LauncherID id) {
    IDToWindow::const_iterator i = id_to_window_.find(id);
    return i != id_to_window_.end() ? i->second : NULL;
  }

  // aura::WindowObserver overrides:
  virtual void OnWindowAdded(aura::Window* new_window) OVERRIDE {
    static int image_count = 0;
    ash::LauncherModel* model = ash::Shell::GetInstance()->launcher()->model();
    ash::LauncherItem item(ash::TYPE_TABBED);
    id_to_window_[model->next_id()] = new_window;
    item.num_tabs = image_count + 1;
    item.image.setConfig(SkBitmap::kARGB_8888_Config, 16, 16);
    item.image.allocPixels();
    item.image.eraseARGB(255,
                         image_count == 0 ? 255 : 0,
                         image_count == 1 ? 255 : 0,
                         image_count == 2 ? 255 : 0);
    image_count = (image_count + 1) % 3;
    model->Add(model->item_count(), item);
  }

  virtual void OnWillRemoveWindow(aura::Window* window) OVERRIDE {
    for (IDToWindow::iterator i = id_to_window_.begin();
         i != id_to_window_.end(); ++i) {
      if (i->second == window) {
        ash::LauncherModel* model =
            ash::Shell::GetInstance()->launcher()->model();
        int index = model->ItemIndexByID(i->first);
        DCHECK_NE(-1, index);
        model->RemoveItemAt(index);
        id_to_window_.erase(i);
        break;
      }
    }
  }

 private:
  typedef std::map<ash::LauncherID, aura::Window*> IDToWindow;

  // Window watching for newly created windows to be added to.
  aura::Window* window_;

  // Maps from window to the id we gave it.
  IDToWindow id_to_window_;

  DISALLOW_COPY_AND_ASSIGN(WindowWatcher);
};

class ShellDelegateImpl : public ash::ShellDelegate {
 public:
  ShellDelegateImpl() {}

  void set_watcher(WindowWatcher* watcher) { watcher_ = watcher; }

  virtual void CreateNewWindow() OVERRIDE {
    ash::shell::ToplevelWindow::CreateParams create_params;
    create_params.can_resize = true;
    create_params.can_maximize = true;
    ash::shell::ToplevelWindow::CreateToplevelWindow(create_params);
  }

  virtual views::Widget* CreateStatusArea() OVERRIDE {
    return ash::internal::CreateStatusArea();
  }

#if defined(OS_CHROMEOS)
 virtual void LockScreen() OVERRIDE {
   ash::shell::CreateLockScreen();
 }
#endif

  virtual void Exit() OVERRIDE {
    MessageLoopForUI::current()->Quit();
  }

  virtual void BuildAppListModel(ash::AppListModel* model) OVERRIDE {
    ash::shell::BuildAppListModel(model);
  }

  virtual ash::AppListViewDelegate* CreateAppListViewDelegate() OVERRIDE {
    return ash::shell::CreateAppListViewDelegate();
  }

  std::vector<aura::Window*> GetCycleWindowList(
      CycleSource source,
      CycleOrder order) const OVERRIDE {
    aura::Window* default_container = ash::Shell::GetInstance()->GetContainer(
        ash::internal::kShellWindowId_DefaultContainer);
    std::vector<aura::Window*> windows = default_container->children();
    // Window cycling expects the topmost window at the front of the list.
    std::reverse(windows.begin(), windows.end());
    return windows;
  }

  virtual void LauncherItemClicked(
      const ash::LauncherItem& item) OVERRIDE {
    aura::Window* window = watcher_->GetWindowByID(item.id);
    window->Show();
    ash::ActivateWindow(window);
  }

  virtual int GetBrowserShortcutResourceId() OVERRIDE {
    return IDR_AURA_LAUNCHER_BROWSER_SHORTCUT;
  }

  virtual string16 GetLauncherItemTitle(
      const ash::LauncherItem& item) OVERRIDE {
    return watcher_->GetWindowByID(item.id)->title();
  }

 private:
  // Used to update Launcher. Owned by main.
  WindowWatcher* watcher_;

  DISALLOW_COPY_AND_ASSIGN(ShellDelegateImpl);
};

}  // namespace

namespace ash {
namespace shell {

void InitWindowTypeLauncher();

}  // namespace shell
}  // namespace ash

int main(int argc, char** argv) {
  CommandLine::Init(argc, argv);

  // The exit manager is in charge of calling the dtors of singleton objects.
  base::AtExitManager exit_manager;

  ash::shell::PreMainMessageLoopStart();

  // Create the message-loop here before creating the root window.
  MessageLoop message_loop(MessageLoop::TYPE_UI);
  ui::CompositorTestSupport::Initialize();

  // A ViewsDelegate is required.
  if (!views::ViewsDelegate::views_delegate)
    views::ViewsDelegate::views_delegate = new ShellViewsDelegate;

  ShellDelegateImpl* delegate = new ShellDelegateImpl;
  ash::Shell::CreateInstance(delegate);

  scoped_ptr<WindowWatcher> window_watcher(new WindowWatcher);
  delegate->set_watcher(window_watcher.get());

  ash::shell::InitWindowTypeLauncher();

  aura::RootWindow::GetInstance()->Run();

  window_watcher.reset();

  ash::Shell::DeleteInstance();

  aura::RootWindow::DeleteInstance();

  ui::CompositorTestSupport::Terminate();

  return 0;
}
