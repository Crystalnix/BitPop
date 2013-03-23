// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ash/launcher/chrome_launcher_controller_per_app.h"

#include <vector>

#include "ash/launcher/launcher_model.h"
#include "ash/shell.h"
#include "ash/wm/window_util.h"
#include "base/command_line.h"
#include "base/string_number_conversions.h"
#include "base/values.h"
#include "chrome/browser/defaults.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/prefs/incognito_mode_prefs.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/prefs/scoped_user_pref_update.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/ash/app_sync_ui_state.h"
#include "chrome/browser/ui/ash/chrome_launcher_prefs.h"
#include "chrome/browser/ui/ash/extension_utils.h"
#include "chrome/browser/ui/ash/launcher/launcher_app_icon_loader.h"
#include "chrome/browser/ui/ash/launcher/launcher_app_tab_helper.h"
#include "chrome/browser/ui/ash/launcher/launcher_context_menu.h"
#include "chrome/browser/ui/ash/launcher/launcher_item_controller.h"
#include "chrome/browser/ui/ash/launcher/shell_window_launcher_controller.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/web_applications/web_app.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_resource.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/web_contents.h"
#include "extensions/common/url_pattern.h"
#include "grit/theme_resources.h"
#include "ui/aura/root_window.h"
#include "ui/aura/window.h"

using content::WebContents;
using extensions::Extension;

namespace {

// Item controller for an app shortcut. Shortcuts track app and launcher ids,
// but do not have any associated windows (opening a shortcut will replace the
// item with the appropriate LauncherItemController type).
class AppShortcutLauncherItemController : public LauncherItemController {
 public:
  AppShortcutLauncherItemController(const std::string& app_id,
                                    ChromeLauncherControllerPerApp* controller)
      : LauncherItemController(TYPE_SHORTCUT, app_id, controller) {
    // Google Drive should just refocus to it's main app UI.
    // TODO(davemoore): Generalize this for other applications.
    if (app_id == "apdfllckaahabafndbhieahigkjlhalf") {
      const Extension* extension =
          launcher_controller()->GetExtensionForAppID(app_id);
      refocus_url_ = GURL(extension->launch_web_url() + "*");
    }
  }

  virtual ~AppShortcutLauncherItemController() {}

  // LauncherItemController overrides:
  virtual string16 GetTitle() OVERRIDE {
    return GetAppTitle();
  }

  virtual bool HasWindow(aura::Window* window) const OVERRIDE {
    return false;
  }

  virtual bool IsOpen() const OVERRIDE {
    return false;
  }

  virtual void Launch(int event_flags) OVERRIDE {
    launcher_controller()->LaunchApp(app_id(), event_flags);
  }

  virtual void Activate() OVERRIDE {
    launcher_controller()->ActivateApp(app_id(), ui::EF_NONE);
  }

  virtual void Close() OVERRIDE {
    // TODO: maybe should treat as unpin?
  }

  virtual void Clicked() OVERRIDE {
    Activate();
  }

  virtual void OnRemoved() OVERRIDE {
    // AppShortcutLauncherItemController is unowned; delete on removal.
    delete this;
  }

  virtual void LauncherItemChanged(
      int model_index,
      const ash::LauncherItem& old_item) OVERRIDE {
  }

  // Stores the optional refocus url pattern for this item.
  const GURL& refocus_url() const { return refocus_url_; }
  void set_refocus_url(const GURL& refocus_url) { refocus_url_ = refocus_url; }

 private:
  GURL refocus_url_;
  DISALLOW_COPY_AND_ASSIGN(AppShortcutLauncherItemController);
};

std::string GetPrefKeyForRootWindow(aura::RootWindow* root_window) {
  gfx::Display display = gfx::Screen::GetScreenFor(
      root_window)->GetDisplayNearestWindow(root_window);
  DCHECK(display.is_valid());

  return base::Int64ToString(display.id());
}

void UpdatePerDisplayPref(PrefService* pref_service,
                          aura::RootWindow* root_window,
                          const char* pref_key,
                          const std::string& value) {
  std::string key = GetPrefKeyForRootWindow(root_window);
  if (key.empty())
    return;

  DictionaryPrefUpdate update(pref_service, prefs::kShelfPreferences);
  base::DictionaryValue* shelf_prefs = update.Get();
  base::DictionaryValue* prefs = NULL;
  if (!shelf_prefs->GetDictionary(key, &prefs)) {
    prefs = new base::DictionaryValue();
    shelf_prefs->Set(key, prefs);
  }
  prefs->SetStringWithoutPathExpansion(pref_key, value);
}

// Returns a pref value in |pref_service| for the display of |root_window|. The
// pref value is stored in |local_path| and |path|, but |pref_service| may have
// per-display preferences and the value can be specified by policy. Here is
// the priority:
//  * A value managed by policy. This is a single value that applies to all
//    displays.
//  * A user-set value for the specified display.
//  * A user-set value in |local_path| or |path|. |local_path| is preferred. See
//    comment in |kShelfAlignment| as to why we consider two prefs and why
//    |local_path| is preferred.
//  * A value recommended by policy. This is a single value that applies to all
//    root windows.
std::string GetPrefForRootWindow(PrefService* pref_service,
                                 aura::RootWindow* root_window,
                                 const char* local_path,
                                 const char* path) {
  const PrefService::Preference* local_pref =
      pref_service->FindPreference(local_path);
  const std::string value(pref_service->GetString(local_path));
  if (local_pref->IsManaged())
    return value;

  std::string pref_key = GetPrefKeyForRootWindow(root_window);
  if (!pref_key.empty()) {
    const base::DictionaryValue* shelf_prefs = pref_service->GetDictionary(
        prefs::kShelfPreferences);
    const base::DictionaryValue* display_pref = NULL;
    std::string per_display_value;
    if (shelf_prefs->GetDictionary(pref_key, &display_pref) &&
        display_pref->GetString(path, &per_display_value)) {
      return per_display_value;
    }
  }

  return value;
}

// If prefs have synced and no user-set value exists at |local_path|, the value
// from |synced_path| is copied to |local_path|.
void MaybePropagatePrefToLocal(PrefService* pref_service,
                               const char* local_path,
                               const char* synced_path) {
  if (!pref_service->FindPreference(local_path)->HasUserSetting() &&
      pref_service->IsSyncing()) {
    // First time the user is using this machine, propagate from remote to
    // local.
    pref_service->SetString(local_path, pref_service->GetString(synced_path));
  }
}

}  // namespace

// ChromeLauncherControllerPerApp ---------------------------------------------

ChromeLauncherControllerPerApp::ChromeLauncherControllerPerApp(
    Profile* profile,
    ash::LauncherModel* model)
    : model_(model),
      profile_(profile),
      app_sync_ui_state_(NULL) {
  if (!profile_) {
    // Use the original profile as on chromeos we may get a temporary off the
    // record profile.
    profile_ = ProfileManager::GetDefaultProfile()->GetOriginalProfile();

    app_sync_ui_state_ = AppSyncUIState::Get(profile_);
    if (app_sync_ui_state_)
      app_sync_ui_state_->AddObserver(this);
  }

  model_->AddObserver(this);
  // TODO(stevenjb): Find a better owner for shell_window_controller_?
  shell_window_controller_.reset(new ShellWindowLauncherController(this));
  app_tab_helper_.reset(new LauncherAppTabHelper(profile_));
  app_icon_loader_.reset(new LauncherAppIconLoader(profile_, this));

  notification_registrar_.Add(this,
                              chrome::NOTIFICATION_EXTENSION_LOADED,
                              content::Source<Profile>(profile_));
  notification_registrar_.Add(this,
                              chrome::NOTIFICATION_EXTENSION_UNLOADED,
                              content::Source<Profile>(profile_));
  pref_change_registrar_.Init(profile_->GetPrefs());
  pref_change_registrar_.Add(
      prefs::kPinnedLauncherApps,
      base::Bind(&ChromeLauncherControllerPerApp::UpdateAppLaunchersFromPref,
                 base::Unretained(this)));
  pref_change_registrar_.Add(
      prefs::kShelfAlignmentLocal,
      base::Bind(&ChromeLauncherControllerPerApp::SetShelfAlignmentFromPrefs,
                 base::Unretained(this)));
  pref_change_registrar_.Add(
      prefs::kShelfAutoHideBehaviorLocal,
      base::Bind(&ChromeLauncherControllerPerApp::
                     SetShelfAutoHideBehaviorFromPrefs,
                 base::Unretained(this)));
}

ChromeLauncherControllerPerApp::~ChromeLauncherControllerPerApp() {
  // Reset the shell window controller here since it has a weak pointer to this.
  shell_window_controller_.reset();

  model_->RemoveObserver(this);
  for (IDToItemControllerMap::iterator i = id_to_item_controller_map_.begin();
       i != id_to_item_controller_map_.end(); ++i) {
    i->second->OnRemoved();
    model_->RemoveItemAt(model_->ItemIndexByID(i->first));
  }

  if (ash::Shell::HasInstance())
    ash::Shell::GetInstance()->RemoveShellObserver(this);

  if (app_sync_ui_state_)
    app_sync_ui_state_->RemoveObserver(this);

  profile_->GetPrefs()->RemoveObserver(this);
}

void ChromeLauncherControllerPerApp::Init() {
  UpdateAppLaunchersFromPref();

  // TODO(sky): update unit test so that this test isn't necessary.
  if (ash::Shell::HasInstance()) {
    SetShelfAutoHideBehaviorFromPrefs();
    SetShelfAlignmentFromPrefs();
    PrefService* prefs = profile_->GetPrefs();
    if (!prefs->FindPreference(prefs::kShelfAlignmentLocal)->HasUserSetting() ||
        !prefs->FindPreference(prefs::kShelfAutoHideBehaviorLocal)->
            HasUserSetting()) {
      // This causes OnIsSyncingChanged to be called when the value of
      // PrefService::IsSyncing() changes.
      prefs->AddObserver(this);
    }
    ash::Shell::GetInstance()->AddShellObserver(this);
  }
}

ash::LauncherID ChromeLauncherControllerPerApp::CreateTabbedLauncherItem(
    LauncherItemController* controller,
    IncognitoState is_incognito,
    ash::LauncherItemStatus status) {
  ash::LauncherID id = model_->next_id();
  DCHECK(!HasItemController(id));
  DCHECK(controller);
  id_to_item_controller_map_[id] = controller;
  controller->set_launcher_id(id);

  ash::LauncherItem item;
  item.type = ash::TYPE_TABBED;
  item.is_incognito = (is_incognito == STATE_INCOGNITO);
  item.status = status;
  model_->Add(item);
  return id;
}

ash::LauncherID ChromeLauncherControllerPerApp::CreateAppLauncherItem(
    LauncherItemController* controller,
    const std::string& app_id,
    ash::LauncherItemStatus status) {
  DCHECK(controller);
  return InsertAppLauncherItem(controller, app_id, status,
                               model_->item_count());
}

void ChromeLauncherControllerPerApp::SetItemStatus(
    ash::LauncherID id,
    ash::LauncherItemStatus status) {
  int index = model_->ItemIndexByID(id);
  DCHECK_GE(index, 0);
  ash::LauncherItem item = model_->items()[index];
  item.status = status;
  model_->Set(index, item);
}

void ChromeLauncherControllerPerApp::SetItemController(
    ash::LauncherID id,
    LauncherItemController* controller) {
  IDToItemControllerMap::iterator iter = id_to_item_controller_map_.find(id);
  DCHECK(iter != id_to_item_controller_map_.end());
  iter->second->OnRemoved();
  iter->second = controller;
  controller->set_launcher_id(id);
}

void ChromeLauncherControllerPerApp::CloseLauncherItem(ash::LauncherID id) {
  if (IsPinned(id)) {
    // Create a new shortcut controller.
    IDToItemControllerMap::iterator iter = id_to_item_controller_map_.find(id);
    DCHECK(iter != id_to_item_controller_map_.end());
    SetItemStatus(id, ash::STATUS_CLOSED);
    std::string app_id = iter->second->app_id();
    iter->second->OnRemoved();
    iter->second = new AppShortcutLauncherItemController(app_id, this);
    iter->second->set_launcher_id(id);
  } else {
    LauncherItemClosed(id);
  }
}

void ChromeLauncherControllerPerApp::Unpin(ash::LauncherID id) {
  DCHECK(HasItemController(id));

  LauncherItemController* controller = id_to_item_controller_map_[id];
  if (controller->type() == LauncherItemController::TYPE_APP) {
    int index = model_->ItemIndexByID(id);
    ash::LauncherItem item = model_->items()[index];
    item.type = ash::TYPE_PLATFORM_APP;
    model_->Set(index, item);
  } else {
    LauncherItemClosed(id);
  }
  if (CanPin())
    PersistPinnedState();
}

void ChromeLauncherControllerPerApp::Pin(ash::LauncherID id) {
  DCHECK(HasItemController(id));

  int index = model_->ItemIndexByID(id);
  ash::LauncherItem item = model_->items()[index];

  if (item.type != ash::TYPE_PLATFORM_APP)
    return;

  item.type = ash::TYPE_APP_SHORTCUT;
  model_->Set(index, item);

  if (CanPin())
    PersistPinnedState();
}

bool ChromeLauncherControllerPerApp::IsPinned(ash::LauncherID id) {
  int index = model_->ItemIndexByID(id);
  ash::LauncherItemType type = model_->items()[index].type;
  return type == ash::TYPE_APP_SHORTCUT;
}

void ChromeLauncherControllerPerApp::TogglePinned(ash::LauncherID id) {
  if (!HasItemController(id))
    return;  // May happen if item closed with menu open.

  if (IsPinned(id))
    Unpin(id);
  else
    Pin(id);
}

bool ChromeLauncherControllerPerApp::IsPinnable(ash::LauncherID id) const {
  int index = model_->ItemIndexByID(id);
  if (index == -1)
    return false;

  ash::LauncherItemType type = model_->items()[index].type;
  return ((type == ash::TYPE_APP_SHORTCUT || type == ash::TYPE_PLATFORM_APP) &&
          CanPin());
}

void ChromeLauncherControllerPerApp::Launch(ash::LauncherID id,
                                            int event_flags) {
  if (!HasItemController(id))
    return;  // In case invoked from menu and item closed while menu up.
  id_to_item_controller_map_[id]->Launch(event_flags);
}

void ChromeLauncherControllerPerApp::Close(ash::LauncherID id) {
  if (!HasItemController(id))
    return;  // May happen if menu closed.
  id_to_item_controller_map_[id]->Close();
}

bool ChromeLauncherControllerPerApp::IsOpen(ash::LauncherID id) {
  if (!HasItemController(id))
    return false;
  return id_to_item_controller_map_[id]->IsOpen();
}

bool ChromeLauncherControllerPerApp::IsPlatformApp(ash::LauncherID id) {
  if (!HasItemController(id))
    return false;

  std::string app_id = GetAppIDForLauncherID(id);
  const Extension* extension = GetExtensionForAppID(app_id);
  DCHECK(extension);
  return extension->is_platform_app();
}

void ChromeLauncherControllerPerApp::LaunchApp(const std::string& app_id,
                                               int event_flags) {
  const Extension* extension = GetExtensionForAppID(app_id);
  extension_utils::OpenExtension(GetProfileForNewWindows(),
                                 extension,
                                 event_flags);
}

void ChromeLauncherControllerPerApp::ActivateApp(const std::string& app_id,
                                                 int event_flags) {
  if (app_id == extension_misc::kChromeAppId) {
    OnBrowserShortcutClicked(event_flags);
    return;
  }

  // If there is an existing non-shortcut controller for this app, open it.
  ash::LauncherID id = GetLauncherIDForAppID(app_id);
  URLPattern refocus_pattern(URLPattern::SCHEME_ALL);
  refocus_pattern.SetMatchAllURLs(true);

  if (id > 0) {
    LauncherItemController* controller = id_to_item_controller_map_[id];
    if (controller->type() != LauncherItemController::TYPE_SHORTCUT) {
      controller->Activate();
      return;
    }

    AppShortcutLauncherItemController* app_controller =
        static_cast<AppShortcutLauncherItemController*>(controller);
    const GURL refocus_url = app_controller->refocus_url();

    if (!refocus_url.is_empty())
      refocus_pattern.Parse(refocus_url.spec());
  }

  // Check if there are any open tabs for this app.
  AppIDToWebContentsListMap::iterator app_i =
      app_id_to_web_contents_list_.find(app_id);
  if (app_i != app_id_to_web_contents_list_.end()) {
    for (WebContentsList::iterator tab_i = app_i->second.begin();
         tab_i != app_i->second.end();
         ++tab_i) {
      WebContents* tab = *tab_i;
      const GURL tab_url = tab->GetURL();
      if (refocus_pattern.MatchesURL(tab_url)) {
        Browser* browser = chrome::FindBrowserWithWebContents(tab);
        TabStripModel* tab_strip = browser->tab_strip_model();
        int index = tab_strip->GetIndexOfWebContents(tab);
        DCHECK_NE(TabStripModel::kNoTab, index);
        tab_strip->ActivateTabAt(index, false);
        browser->window()->Show();
        ash::wm::ActivateWindow(browser->window()->GetNativeWindow());
        return;
      }
    }
  }

  LaunchApp(app_id, event_flags);
}

extensions::ExtensionPrefs::LaunchType
    ChromeLauncherControllerPerApp::GetLaunchType(ash::LauncherID id) {
  DCHECK(HasItemController(id));

  const Extension* extension = GetExtensionForAppID(
      id_to_item_controller_map_[id]->app_id());
  return profile_->GetExtensionService()->extension_prefs()->GetLaunchType(
      extension,
      extensions::ExtensionPrefs::LAUNCH_DEFAULT);
}

std::string ChromeLauncherControllerPerApp::GetAppID(
    content::WebContents* tab) {
  return app_tab_helper_->GetAppID(tab);
}

ash::LauncherID ChromeLauncherControllerPerApp::GetLauncherIDForAppID(
    const std::string& app_id) {
  for (IDToItemControllerMap::const_iterator i =
           id_to_item_controller_map_.begin();
       i != id_to_item_controller_map_.end(); ++i) {
    if (i->second->app_id() == app_id)
      return i->first;
  }
  return 0;
}

std::string ChromeLauncherControllerPerApp::GetAppIDForLauncherID(
    ash::LauncherID id) {
  DCHECK(HasItemController(id));
  return id_to_item_controller_map_[id]->app_id();
}

void ChromeLauncherControllerPerApp::SetAppImage(
    const std::string& id,
    const gfx::ImageSkia& image) {
  // TODO: need to get this working for shortcuts.

  for (IDToItemControllerMap::const_iterator i =
           id_to_item_controller_map_.begin();
       i != id_to_item_controller_map_.end(); ++i) {
    if (i->second->app_id() != id)
      continue;

    // Panel items may share the same app_id as the app that created them,
    // but they set their icon image in
    // BrowserLauncherItemController::UpdateLauncher(), so do not set panel
    // images here.
    if (i->second->type() == LauncherItemController::TYPE_EXTENSION_PANEL)
      continue;

    int index = model_->ItemIndexByID(i->first);
    ash::LauncherItem item = model_->items()[index];
    item.image = image;
    model_->Set(index, item);
    // It's possible we're waiting on more than one item, so don't break.
  }
}

void ChromeLauncherControllerPerApp::SetLauncherItemImage(
    ash::LauncherID launcher_id,
    const gfx::ImageSkia& image) {
  int index = model_->ItemIndexByID(launcher_id);
  if (index == -1)
    return;
  ash::LauncherItem item = model_->items()[index];
  item.image = image;
  model_->Set(index, item);
}

bool ChromeLauncherControllerPerApp::IsAppPinned(const std::string& app_id) {
  for (IDToItemControllerMap::const_iterator i =
           id_to_item_controller_map_.begin();
       i != id_to_item_controller_map_.end(); ++i) {
    if (IsPinned(i->first) && i->second->app_id() == app_id)
      return true;
  }
  return false;
}

void ChromeLauncherControllerPerApp::PinAppWithID(const std::string& app_id) {
  if (CanPin())
    DoPinAppWithID(app_id);
  else
    NOTREACHED();
}

void ChromeLauncherControllerPerApp::SetLaunchType(
    ash::LauncherID id,
    extensions::ExtensionPrefs::LaunchType launch_type) {
  if (!HasItemController(id))
    return;

  return profile_->GetExtensionService()->extension_prefs()->SetLaunchType(
      id_to_item_controller_map_[id]->app_id(), launch_type);
}

void ChromeLauncherControllerPerApp::UnpinAppsWithID(
    const std::string& app_id) {
  if (CanPin())
    DoUnpinAppsWithID(app_id);
  else
    NOTREACHED();
}

bool ChromeLauncherControllerPerApp::IsLoggedInAsGuest() {
  return ProfileManager::GetDefaultProfileOrOffTheRecord()->IsOffTheRecord();
}

void ChromeLauncherControllerPerApp::CreateNewWindow() {
  chrome::NewEmptyWindow(
      GetProfileForNewWindows(), chrome::HOST_DESKTOP_TYPE_ASH);
}

void ChromeLauncherControllerPerApp::CreateNewIncognitoWindow() {
  chrome::NewEmptyWindow(GetProfileForNewWindows()->GetOffTheRecordProfile());
}

bool ChromeLauncherControllerPerApp::CanPin() const {
  const PrefService::Preference* pref =
      profile_->GetPrefs()->FindPreference(prefs::kPinnedLauncherApps);
  return pref && pref->IsUserModifiable();
}

ash::ShelfAutoHideBehavior
    ChromeLauncherControllerPerApp::GetShelfAutoHideBehavior(
        aura::RootWindow* root_window) const {
  // See comment in |kShelfAlignment| as to why we consider two prefs.
  const std::string behavior_value(
      GetPrefForRootWindow(profile_->GetPrefs(),
                           root_window,
                           prefs::kShelfAutoHideBehaviorLocal,
                           prefs::kShelfAutoHideBehavior));

  // Note: To maintain sync compatibility with old images of chrome/chromeos
  // the set of values that may be encountered includes the now-extinct
  // "Default" as well as "Never" and "Always", "Default" should now
  // be treated as "Never" (http://crbug.com/146773).
  if (behavior_value == ash::kShelfAutoHideBehaviorAlways)
    return ash::SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS;
  return ash::SHELF_AUTO_HIDE_BEHAVIOR_NEVER;
}

bool ChromeLauncherControllerPerApp::CanUserModifyShelfAutoHideBehavior(
    aura::RootWindow* root_window) const {
  return profile_->GetPrefs()->
      FindPreference(prefs::kShelfAutoHideBehaviorLocal)->IsUserModifiable();
}

void ChromeLauncherControllerPerApp::ToggleShelfAutoHideBehavior(
    aura::RootWindow* root_window) {
  ash::ShelfAutoHideBehavior behavior = GetShelfAutoHideBehavior(root_window) ==
      ash::SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS ?
          ash::SHELF_AUTO_HIDE_BEHAVIOR_NEVER :
          ash::SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS;
  SetShelfAutoHideBehaviorPrefs(behavior, root_window);
  return;
}

void ChromeLauncherControllerPerApp::RemoveTabFromRunningApp(
    WebContents* tab,
    const std::string& app_id) {
  web_contents_to_app_id_.erase(tab);
  AppIDToWebContentsListMap::iterator i_app_id =
      app_id_to_web_contents_list_.find(app_id);
  if (i_app_id != app_id_to_web_contents_list_.end()) {
    WebContentsList* tab_list = &i_app_id->second;
    tab_list->remove(tab);
    if (tab_list->empty()) {
      app_id_to_web_contents_list_.erase(i_app_id);
      i_app_id = app_id_to_web_contents_list_.end();
      ash::LauncherID id = GetLauncherIDForAppID(app_id);
      if (id > 0)
        SetItemStatus(id, ash::STATUS_CLOSED);
    }
  }
}

void ChromeLauncherControllerPerApp::UpdateAppState(
    content::WebContents* contents,
    AppState app_state) {
  std::string app_id = GetAppID(contents);

  // Check the old |app_id| for a tab. If the contents has changed we need to
  // remove it from the previous app.
  if (web_contents_to_app_id_.find(contents) != web_contents_to_app_id_.end()) {
    std::string last_app_id = web_contents_to_app_id_[contents];
    if (last_app_id != app_id)
      RemoveTabFromRunningApp(contents, last_app_id);
  }

  if (app_id.empty())
    return;

  web_contents_to_app_id_[contents] = app_id;

  if (app_state == APP_STATE_REMOVED) {
    // The tab has gone away.
    RemoveTabFromRunningApp(contents, app_id);
  } else {
    WebContentsList& tab_list(app_id_to_web_contents_list_[app_id]);

    if (app_state == APP_STATE_INACTIVE) {
      WebContentsList::const_iterator i_tab =
          std::find(tab_list.begin(), tab_list.end(), contents);
      if (i_tab == tab_list.end())
        tab_list.push_back(contents);
      if (i_tab != tab_list.begin()) {
        // Going inactive, but wasn't the front tab, indicating that a new
        // tab has already become active.
        return;
      }
    } else {
      tab_list.remove(contents);
      tab_list.push_front(contents);
    }
    ash::LauncherID id = GetLauncherIDForAppID(app_id);
    if (id > 0) {
      // If the window is active, mark the app as active.
      SetItemStatus(id, app_state == APP_STATE_WINDOW_ACTIVE ?
          ash::STATUS_ACTIVE : ash::STATUS_RUNNING);
    }
  }
}

void ChromeLauncherControllerPerApp::SetRefocusURLPattern(
    ash::LauncherID id,
    const GURL& url) {
  DCHECK(HasItemController(id));
  LauncherItemController* controller = id_to_item_controller_map_[id];

  int index = model_->ItemIndexByID(id);
  if (index == -1) {
    NOTREACHED() << "Invalid launcher id";
    return;
  }

  ash::LauncherItemType type = model_->items()[index].type;
  if (type == ash::TYPE_APP_SHORTCUT) {
    AppShortcutLauncherItemController* app_controller =
        static_cast<AppShortcutLauncherItemController*>(controller);
    app_controller->set_refocus_url(url);
  } else {
    NOTREACHED() << "Invalid launcher type";
  }
}

const Extension* ChromeLauncherControllerPerApp::GetExtensionForAppID(
    const std::string& app_id) {
  return profile_->GetExtensionService()->GetInstalledExtension(app_id);
}

void ChromeLauncherControllerPerApp::OnBrowserShortcutClicked(
    int event_flags) {
  if (event_flags & ui::EF_CONTROL_DOWN) {
    CreateNewWindow();
    return;
  }

  Browser* last_browser = browser::FindTabbedBrowser(
      GetProfileForNewWindows(), true, chrome::HOST_DESKTOP_TYPE_ASH);

  if (!last_browser) {
    CreateNewWindow();
    return;
  }

  aura::Window* window = last_browser->window()->GetNativeWindow();
  window->Show();
  ash::wm::ActivateWindow(window);
}

void ChromeLauncherControllerPerApp::ItemClicked(const ash::LauncherItem& item,
                                                 int event_flags) {
  DCHECK(HasItemController(item.id));
  id_to_item_controller_map_[item.id]->Clicked();
}

int ChromeLauncherControllerPerApp::GetBrowserShortcutResourceId() {
  return IDR_PRODUCT_LOGO_32;
}

string16 ChromeLauncherControllerPerApp::GetTitle(
    const ash::LauncherItem& item) {
  DCHECK(HasItemController(item.id));
  return id_to_item_controller_map_[item.id]->GetTitle();
}

ui::MenuModel* ChromeLauncherControllerPerApp::CreateContextMenu(
    const ash::LauncherItem& item,
    aura::RootWindow* root_window) {
  return new LauncherContextMenu(this, &item, root_window);
}

ash::LauncherID ChromeLauncherControllerPerApp::GetIDByWindow(
    aura::Window* window) {
  for (IDToItemControllerMap::const_iterator i =
           id_to_item_controller_map_.begin();
       i != id_to_item_controller_map_.end(); ++i) {
    if (i->second->HasWindow(window))
      return i->first;
  }
  return 0;
}

bool ChromeLauncherControllerPerApp::IsDraggable(
    const ash::LauncherItem& item) {
  return item.type == ash::TYPE_APP_SHORTCUT ? CanPin() : true;
}

void ChromeLauncherControllerPerApp::LauncherItemAdded(int index) {
}

void ChromeLauncherControllerPerApp::LauncherItemRemoved(
    int index,
    ash::LauncherID id) {
}

void ChromeLauncherControllerPerApp::LauncherItemMoved(
    int start_index,
    int target_index) {
  ash::LauncherID id = model_->items()[target_index].id;
  if (HasItemController(id) && IsPinned(id))
    PersistPinnedState();
}

void ChromeLauncherControllerPerApp::LauncherItemChanged(
    int index,
    const ash::LauncherItem& old_item) {
  ash::LauncherID id = model_->items()[index].id;
  id_to_item_controller_map_[id]->LauncherItemChanged(index, old_item);
}

void ChromeLauncherControllerPerApp::LauncherStatusChanged() {
}

void ChromeLauncherControllerPerApp::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  switch (type) {
    case chrome::NOTIFICATION_EXTENSION_LOADED: {
      UpdateAppLaunchersFromPref();
      break;
    }
    case chrome::NOTIFICATION_EXTENSION_UNLOADED: {
      const content::Details<extensions::UnloadedExtensionInfo> unload_info(
          details);
      const Extension* extension = unload_info->extension;
      if (IsAppPinned(extension->id()))
        DoUnpinAppsWithID(extension->id());
      app_icon_loader_->ClearImage(extension->id());
      break;
    }
    default:
      NOTREACHED() << "Unexpected notification type=" << type;
  }
}

void ChromeLauncherControllerPerApp::OnShelfAlignmentChanged(
    aura::RootWindow* root_window) {
  const char* pref_value = NULL;
  switch (ash::Shell::GetInstance()->GetShelfAlignment(root_window)) {
    case ash::SHELF_ALIGNMENT_BOTTOM:
      pref_value = ash::kShelfAlignmentBottom;
      break;
    case ash::SHELF_ALIGNMENT_LEFT:
      pref_value = ash::kShelfAlignmentLeft;
      break;
    case ash::SHELF_ALIGNMENT_RIGHT:
      pref_value = ash::kShelfAlignmentRight;
      break;
  }

  UpdatePerDisplayPref(
      profile_->GetPrefs(), root_window, prefs::kShelfAlignment, pref_value);

  if (root_window == ash::Shell::GetPrimaryRootWindow()) {
    // See comment in |kShelfAlignment| about why we have two prefs here.
    profile_->GetPrefs()->SetString(prefs::kShelfAlignmentLocal, pref_value);
    profile_->GetPrefs()->SetString(prefs::kShelfAlignment, pref_value);
  }
}

void ChromeLauncherControllerPerApp::OnIsSyncingChanged() {
  MaybePropagatePrefToLocal(profile_->GetPrefs(),
                            prefs::kShelfAlignmentLocal,
                            prefs::kShelfAlignment);
  MaybePropagatePrefToLocal(profile_->GetPrefs(),
                            prefs::kShelfAutoHideBehaviorLocal,
                            prefs::kShelfAutoHideBehavior);
}

void ChromeLauncherControllerPerApp::OnAppSyncUIStatusChanged() {
  if (app_sync_ui_state_->status() == AppSyncUIState::STATUS_SYNCING)
    model_->SetStatus(ash::LauncherModel::STATUS_LOADING);
  else
    model_->SetStatus(ash::LauncherModel::STATUS_NORMAL);
}

void ChromeLauncherControllerPerApp::PersistPinnedState() {
  // It is a coding error to call PersistPinnedState() if the pinned apps are
  // not user-editable. The code should check earlier and not perform any
  // modification actions that trigger persisting the state.
  if (!CanPin()) {
    NOTREACHED() << "Can't pin but pinned state being updated";
    return;
  }

  // Mutating kPinnedLauncherApps is going to notify us and trigger us to
  // process the change. We don't want that to happen so remove ourselves as a
  // listener.
  pref_change_registrar_.Remove(prefs::kPinnedLauncherApps);
  {
    ListPrefUpdate updater(profile_->GetPrefs(), prefs::kPinnedLauncherApps);
    updater->Clear();
    for (size_t i = 0; i < model_->items().size(); ++i) {
      if (model_->items()[i].type == ash::TYPE_APP_SHORTCUT) {
        ash::LauncherID id = model_->items()[i].id;
        if (HasItemController(id) && IsPinned(id)) {
          base::DictionaryValue* app_value = ash::CreateAppDict(
              id_to_item_controller_map_[id]->app_id());
          if (app_value)
            updater->Append(app_value);
        }
      }
    }
  }
  pref_change_registrar_.Add(
      prefs::kPinnedLauncherApps,
      base::Bind(&ChromeLauncherControllerPerApp::UpdateAppLaunchersFromPref,
                 base::Unretained(this)));
}

ash::LauncherModel* ChromeLauncherControllerPerApp::model() {
  return model_;
}

Profile* ChromeLauncherControllerPerApp::profile() {
  return profile_;
}

Profile* ChromeLauncherControllerPerApp::GetProfileForNewWindows() {
  return ProfileManager::GetDefaultProfileOrOffTheRecord();
}

void ChromeLauncherControllerPerApp::LauncherItemClosed(ash::LauncherID id) {
  IDToItemControllerMap::iterator iter = id_to_item_controller_map_.find(id);
  DCHECK(iter != id_to_item_controller_map_.end());
  app_icon_loader_->ClearImage(iter->second->app_id());
  iter->second->OnRemoved();
  id_to_item_controller_map_.erase(iter);
  model_->RemoveItemAt(model_->ItemIndexByID(id));
}

void ChromeLauncherControllerPerApp::DoPinAppWithID(
    const std::string& app_id) {
  // If there is an item, do nothing and return.
  if (IsAppPinned(app_id))
    return;

  ash::LauncherID launcher_id = GetLauncherIDForAppID(app_id);
  if (launcher_id) {
    // App item exists, pin it
    Pin(launcher_id);
  } else {
    // Otherwise, create a shortcut item for it.
    CreateAppShortcutLauncherItem(app_id, model_->item_count());
    if (CanPin())
      PersistPinnedState();
  }
}

void ChromeLauncherControllerPerApp::DoUnpinAppsWithID(
    const std::string& app_id) {
  for (IDToItemControllerMap::iterator i = id_to_item_controller_map_.begin();
       i != id_to_item_controller_map_.end(); ) {
    IDToItemControllerMap::iterator current(i);
    ++i;
    if (current->second->app_id() == app_id && IsPinned(current->first))
      Unpin(current->first);
  }
}

void ChromeLauncherControllerPerApp::UpdateAppLaunchersFromPref() {
  // Construct a vector representation of to-be-pinned apps from the pref.
  std::vector<std::string> pinned_apps;
  const base::ListValue* pinned_apps_pref =
      profile_->GetPrefs()->GetList(prefs::kPinnedLauncherApps);
  for (base::ListValue::const_iterator it(pinned_apps_pref->begin());
       it != pinned_apps_pref->end(); ++it) {
    DictionaryValue* app = NULL;
    std::string app_id;
    if ((*it)->GetAsDictionary(&app) &&
        app->GetString(ash::kPinnedAppsPrefAppIDPath, &app_id) &&
        std::find(pinned_apps.begin(), pinned_apps.end(), app_id) ==
            pinned_apps.end() &&
        app_tab_helper_->IsValidID(app_id)) {
      pinned_apps.push_back(app_id);
    }
  }

  // Walk the model and |pinned_apps| from the pref lockstep, adding and
  // removing items as necessary. NB: This code uses plain old indexing instead
  // of iterators because of model mutations as part of the loop.
  std::vector<std::string>::const_iterator pref_app_id(pinned_apps.begin());
  int index = 0;
  for (; index < model_->item_count() && pref_app_id != pinned_apps.end();
       ++index) {
    // If the next app launcher according to the pref is present in the model,
    // delete all app launcher entries in between.
    if (IsAppPinned(*pref_app_id)) {
      for (; index < model_->item_count(); ++index) {
        const ash::LauncherItem& item(model_->items()[index]);
        if (item.type != ash::TYPE_APP_SHORTCUT)
          continue;

        IDToItemControllerMap::const_iterator entry =
            id_to_item_controller_map_.find(item.id);
        if (entry != id_to_item_controller_map_.end() &&
            entry->second->app_id() == *pref_app_id) {
          ++pref_app_id;
          break;
        } else {
          LauncherItemClosed(item.id);
          --index;
        }
      }
      // If the item wasn't found, that means id_to_item_controller_map_
      // is out of sync.
      DCHECK(index < model_->item_count());
    } else {
      // This app wasn't pinned before, insert a new entry.
      ash::LauncherID id = CreateAppShortcutLauncherItem(*pref_app_id, index);
      index = model_->ItemIndexByID(id);
      ++pref_app_id;
    }
  }

  // Remove any trailing existing items.
  while (index < model_->item_count()) {
    const ash::LauncherItem& item(model_->items()[index]);
    if (item.type == ash::TYPE_APP_SHORTCUT)
      LauncherItemClosed(item.id);
    else
      ++index;
  }

  // Append unprocessed items from the pref to the end of the model.
  for (; pref_app_id != pinned_apps.end(); ++pref_app_id)
    DoPinAppWithID(*pref_app_id);
}

void ChromeLauncherControllerPerApp::SetShelfAutoHideBehaviorPrefs(
    ash::ShelfAutoHideBehavior behavior,
    aura::RootWindow* root_window) {
  const char* value = NULL;
  switch (behavior) {
    case ash::SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS:
      value = ash::kShelfAutoHideBehaviorAlways;
      break;
    case ash::SHELF_AUTO_HIDE_BEHAVIOR_NEVER:
      value = ash::kShelfAutoHideBehaviorNever;
      break;
  }

  UpdatePerDisplayPref(
      profile_->GetPrefs(), root_window, prefs::kShelfAutoHideBehavior, value);

  if (root_window == ash::Shell::GetPrimaryRootWindow()) {
    // See comment in |kShelfAlignment| about why we have two prefs here.
    profile_->GetPrefs()->SetString(prefs::kShelfAutoHideBehaviorLocal, value);
    profile_->GetPrefs()->SetString(prefs::kShelfAutoHideBehavior, value);
  }
}

void ChromeLauncherControllerPerApp::SetShelfAutoHideBehaviorFromPrefs() {
  ash::Shell::RootWindowList root_windows;
  if (ash::Shell::IsLauncherPerDisplayEnabled())
    root_windows = ash::Shell::GetAllRootWindows();
  else
    root_windows.push_back(ash::Shell::GetPrimaryRootWindow());

  for (ash::Shell::RootWindowList::const_iterator iter = root_windows.begin();
       iter != root_windows.end(); ++iter) {
    ash::Shell::GetInstance()->SetShelfAutoHideBehavior(
        GetShelfAutoHideBehavior(*iter), *iter);
  }
}

void ChromeLauncherControllerPerApp::SetShelfAlignmentFromPrefs() {
  if (!CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kShowLauncherAlignmentMenu))
    return;

  ash::Shell::RootWindowList root_windows;
  if (ash::Shell::IsLauncherPerDisplayEnabled())
    root_windows = ash::Shell::GetAllRootWindows();
  else
    root_windows.push_back(ash::Shell::GetPrimaryRootWindow());
  for (ash::Shell::RootWindowList::const_iterator iter = root_windows.begin();
       iter != root_windows.end(); ++iter) {
    // See comment in |kShelfAlignment| as to why we consider two prefs.
    const std::string alignment_value(
        GetPrefForRootWindow(profile_->GetPrefs(),
                             *iter,
                             prefs::kShelfAlignmentLocal,
                             prefs::kShelfAlignment));
    ash::ShelfAlignment alignment = ash::SHELF_ALIGNMENT_BOTTOM;
    if (alignment_value == ash::kShelfAlignmentLeft)
      alignment = ash::SHELF_ALIGNMENT_LEFT;
    else if (alignment_value == ash::kShelfAlignmentRight)
      alignment = ash::SHELF_ALIGNMENT_RIGHT;
    ash::Shell::GetInstance()->SetShelfAlignment(alignment, *iter);
  }
}

WebContents* ChromeLauncherControllerPerApp::GetLastActiveWebContents(
    const std::string& app_id) {
  AppIDToWebContentsListMap::const_iterator i =
      app_id_to_web_contents_list_.find(app_id);
  if (i == app_id_to_web_contents_list_.end())
    return NULL;
  DCHECK_GT(i->second.size(), 0u);
  return *i->second.begin();
}

ash::LauncherID ChromeLauncherControllerPerApp::InsertAppLauncherItem(
    LauncherItemController* controller,
    const std::string& app_id,
    ash::LauncherItemStatus status,
    int index) {
  ash::LauncherID id = model_->next_id();
  DCHECK(!HasItemController(id));
  DCHECK(controller);
  id_to_item_controller_map_[id] = controller;
  controller->set_launcher_id(id);

  ash::LauncherItem item;
  item.type = controller->GetLauncherItemType();
  item.is_incognito = false;
  item.image = Extension::GetDefaultIcon(true);

  WebContents* active_tab = GetLastActiveWebContents(app_id);
  if (active_tab) {
    Browser* browser = chrome::FindBrowserWithWebContents(active_tab);
    DCHECK(browser);
    if (browser->window()->IsActive())
      status = ash::STATUS_ACTIVE;
    else
      status = ash::STATUS_RUNNING;
  }
  item.status = status;

  model_->AddAt(index, item);

  if (controller->type() != LauncherItemController::TYPE_EXTENSION_PANEL)
    app_icon_loader_->FetchImage(app_id);

  return id;
}

bool ChromeLauncherControllerPerApp::HasItemController(
    ash::LauncherID id) const {
  return id_to_item_controller_map_.find(id) !=
         id_to_item_controller_map_.end();
}

ash::LauncherID ChromeLauncherControllerPerApp::CreateAppShortcutLauncherItem(
    const std::string& app_id,
    int index) {
  AppShortcutLauncherItemController* controller =
      new AppShortcutLauncherItemController(app_id, this);
  ash::LauncherID launcher_id = InsertAppLauncherItem(
      controller, app_id, ash::STATUS_CLOSED, index);
  return launcher_id;
}

void ChromeLauncherControllerPerApp::SetAppTabHelperForTest(
    AppTabHelper* helper) {
  app_tab_helper_.reset(helper);
}

void ChromeLauncherControllerPerApp::SetAppIconLoaderForTest(
    AppIconLoader* loader) {
  app_icon_loader_.reset(loader);
}

const std::string&
ChromeLauncherControllerPerApp::GetAppIdFromLauncherIdForTest(
    ash::LauncherID id) {
  return id_to_item_controller_map_[id]->app_id();
}
