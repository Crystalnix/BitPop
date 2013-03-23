// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ash/launcher/browser_launcher_item_controller.h"

#include "ash/launcher/launcher.h"
#include "ash/launcher/launcher_model.h"
#include "ash/shell.h"
#include "ash/wm/window_util.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/tab_helper.h"
#include "chrome/browser/favicon/favicon_tab_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/ash/launcher/chrome_launcher_controller.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/web_applications/web_app.h"
#include "content/public/browser/web_contents.h"
#include "grit/ui_resources.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/window.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/views/widget/widget.h"

using extensions::Extension;

BrowserLauncherItemController::BrowserLauncherItemController(
    Type type,
    aura::Window* window,
    TabStripModel* tab_model,
    ChromeLauncherController* launcher_controller,
    const std::string& app_id)
    : LauncherItemController(type, app_id, launcher_controller),
      window_(window),
      tab_model_(tab_model),
      is_incognito_(tab_model->profile()->GetOriginalProfile() !=
                    tab_model->profile() &&
                    !tab_model->profile()->IsGuestSession()) {
  DCHECK(window_);
  window_->AddObserver(this);
}

BrowserLauncherItemController::~BrowserLauncherItemController() {
  tab_model_->RemoveObserver(this);
  window_->RemoveObserver(this);
  if (launcher_id() > 0)
    launcher_controller()->CloseLauncherItem(launcher_id());
}

void BrowserLauncherItemController::Init() {
  tab_model_->AddObserver(this);
  ash::LauncherItemStatus app_status =
      ash::wm::IsActiveWindow(window_) ?
      ash::STATUS_ACTIVE : ash::STATUS_RUNNING;
  if (type() != TYPE_TABBED) {
    launcher_controller()->CreateAppLauncherItem(this, app_id(), app_status);
  } else {
    launcher_controller()->CreateTabbedLauncherItem(
        this,
        is_incognito_ ? ChromeLauncherController::STATE_INCOGNITO :
                        ChromeLauncherController::STATE_NOT_INCOGNITO,
        app_status);
  }
  // In testing scenarios we can get tab strips with no active contents.
  if (tab_model_->active_index() != TabStripModel::kNoTab)
    UpdateLauncher(tab_model_->GetActiveWebContents());
}

// static
BrowserLauncherItemController* BrowserLauncherItemController::Create(
    Browser* browser) {
  // Under testing this can be called before the controller is created.
  if (!ChromeLauncherController::instance())
    return NULL;

  Type type;
  std::string app_id;
  if (browser->is_type_tabbed() || browser->is_type_popup()) {
    type = TYPE_TABBED;
  } else if (browser->is_app()) {
    if (browser->is_type_panel()) {
      if (browser->app_type() == Browser::APP_TYPE_CHILD)
        type = TYPE_EXTENSION_PANEL;
      else
        type = TYPE_APP_PANEL;
    } else {
      type = TYPE_TABBED;
    }
    app_id = web_app::GetExtensionIdFromApplicationName(browser->app_name());
  } else {
    return NULL;
  }
  BrowserLauncherItemController* controller =
      new BrowserLauncherItemController(type,
                                        browser->window()->GetNativeWindow(),
                                        browser->tab_strip_model(),
                                        ChromeLauncherController::instance(),
                                        app_id);
  controller->Init();
  return controller;
}

void BrowserLauncherItemController::BrowserActivationStateChanged() {
  content::WebContents* active_contents = tab_model_->GetActiveWebContents();
  if (active_contents)
    UpdateAppState(active_contents);
  UpdateItemStatus();
}

string16 BrowserLauncherItemController::GetTitle() {
  if (type() == TYPE_TABBED || type() == TYPE_EXTENSION_PANEL) {
    if (tab_model_->active_index() != TabStripModel::kNoTab) {
      const content::WebContents* contents = tab_model_->GetActiveWebContents();
      if (contents)
        return contents->GetTitle();
    }
  }
  return GetAppTitle();
}

bool BrowserLauncherItemController::HasWindow(aura::Window* window) const {
  return window_ == window;
}

bool BrowserLauncherItemController::IsOpen() const {
  return true;
}

void BrowserLauncherItemController::Launch(int event_flags) {
  DCHECK(!app_id().empty());
  launcher_controller()->LaunchApp(app_id(), event_flags);
}

void BrowserLauncherItemController::Activate() {
  window_->Show();
  ash::wm::ActivateWindow(window_);
}

void BrowserLauncherItemController::Close() {
  views::Widget* widget = views::Widget::GetWidgetForNativeView(window_);
  if (widget)
    widget->Close();
}

void BrowserLauncherItemController::Clicked() {
  views::Widget* widget =
      views::Widget::GetWidgetForNativeView(window_);
  if (widget && widget->IsActive()) {
    widget->Minimize();
  } else {
    Activate();
  }
}

void BrowserLauncherItemController::OnRemoved() {
}

void BrowserLauncherItemController::LauncherItemChanged(
    int index,
    const ash::LauncherItem& old_item) {
  if (launcher_model()->items()[index].status == ash::STATUS_ACTIVE &&
      old_item.status == ash::STATUS_RUNNING) {
    Activate();
  }
}

void BrowserLauncherItemController::ActiveTabChanged(
    content::WebContents* old_contents,
    content::WebContents* new_contents,
    int index,
    bool user_gesture) {
  // Update immediately on a tab change.
  if (old_contents)
    UpdateAppState(old_contents);
  UpdateAppState(new_contents);
  UpdateLauncher(new_contents);
}

void BrowserLauncherItemController::TabInsertedAt(
    content::WebContents* contents,
    int index,
    bool foreground) {
  UpdateAppState(contents);
}

void BrowserLauncherItemController::TabDetachedAt(
    content::WebContents* contents,
    int index) {
  launcher_controller()->UpdateAppState(
      contents, ChromeLauncherController::APP_STATE_REMOVED);
}

void BrowserLauncherItemController::TabChangedAt(
    content::WebContents* contents,
    int index,
    TabStripModelObserver::TabChangeType change_type) {
  UpdateAppState(contents);
  if (index != tab_model_->active_index() ||
      !(change_type != TabStripModelObserver::LOADING_ONLY &&
        change_type != TabStripModelObserver::TITLE_NOT_LOADING)) {
    return;
  }

  UpdateLauncher(contents);
}

void BrowserLauncherItemController::TabReplacedAt(
    TabStripModel* tab_strip_model,
    content::WebContents* old_contents,
    content::WebContents* new_contents,
    int index) {
  launcher_controller()->UpdateAppState(
      old_contents,
      ChromeLauncherController::APP_STATE_REMOVED);
  UpdateAppState(new_contents);
}

void BrowserLauncherItemController::FaviconUpdated() {
  UpdateLauncher(tab_model_->GetActiveWebContents());
}

void BrowserLauncherItemController::OnWindowPropertyChanged(
    aura::Window* window,
    const void* key,
    intptr_t old) {
  if (key == aura::client::kDrawAttentionKey)
    UpdateItemStatus();
}

void BrowserLauncherItemController::UpdateItemStatus() {
  ash::LauncherItemStatus status;
  if (ash::wm::IsActiveWindow(window_)) {
    // Clear attention state if active.
    if (window_->GetProperty(aura::client::kDrawAttentionKey))
      window_->SetProperty(aura::client::kDrawAttentionKey, false);
    status = ash::STATUS_ACTIVE;
  } else if (window_->GetProperty(aura::client::kDrawAttentionKey)) {
    status = ash::STATUS_ATTENTION;
  } else {
    status = ash::STATUS_RUNNING;
  }
  launcher_controller()->SetItemStatus(launcher_id(), status);
}

void BrowserLauncherItemController::UpdateLauncher(content::WebContents* tab) {
  if (type() == TYPE_APP_PANEL)
    return;  // Maintained entirely by ChromeLauncherController.

  if (!tab)
    return;  // Assume the window is going to be closed if there are no tabs.

  int item_index = launcher_model()->ItemIndexByID(launcher_id());
  if (item_index == -1)
    return;

  ash::LauncherItem item = launcher_model()->items()[item_index];
  if (type() == TYPE_EXTENSION_PANEL) {
    if (!favicon_loader_.get() || favicon_loader_->web_contents() != tab)
      favicon_loader_.reset(new LauncherFaviconLoader(this, tab));

    // Update the icon for extension panels.
    extensions::TabHelper* extensions_tab_helper =
        extensions::TabHelper::FromWebContents(tab);
    gfx::ImageSkia new_image = gfx::ImageSkia(favicon_loader_->GetFavicon());
    if (new_image.isNull() && extensions_tab_helper->GetExtensionAppIcon())
      new_image = gfx::ImageSkia(*extensions_tab_helper->GetExtensionAppIcon());
    // Only update the icon if we have a new image, or none has been set yet.
    // This avoids flickering to an empty image when a pinned app is opened.
    if (!new_image.isNull())
      item.image = new_image;
    else if (item.image.isNull())
      item.image = extensions::Extension::GetDefaultIcon(true);
  } else {
    DCHECK_EQ(TYPE_TABBED, type());
    ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
    FaviconTabHelper* favicon_tab_helper =
        FaviconTabHelper::FromWebContents(tab);
    if (favicon_tab_helper->ShouldDisplayFavicon()) {
      item.image = favicon_tab_helper->GetFavicon().AsImageSkia();
      if (item.image.isNull()) {
        item.image = *rb.GetImageSkiaNamed(IDR_DEFAULT_FAVICON);
      }
    } else {
      item.image = *rb.GetImageSkiaNamed(IDR_DEFAULT_FAVICON);
    }
  }
  launcher_model()->Set(item_index, item);
}

void BrowserLauncherItemController::UpdateAppState(content::WebContents* tab) {
  ChromeLauncherController::AppState app_state;

  if (tab_model_->GetIndexOfWebContents(tab) == TabStripModel::kNoTab) {
    app_state = ChromeLauncherController::APP_STATE_REMOVED;
  } else if (tab_model_->GetActiveWebContents() == tab) {
    if (ash::wm::IsActiveWindow(window_))
      app_state = ChromeLauncherController::APP_STATE_WINDOW_ACTIVE;
    else
      app_state = ChromeLauncherController::APP_STATE_ACTIVE;
  } else {
    app_state = ChromeLauncherController::APP_STATE_INACTIVE;
  }
  launcher_controller()->UpdateAppState(tab, app_state);
}

ash::LauncherModel* BrowserLauncherItemController::launcher_model() {
  return launcher_controller()->model();
}
