// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/ntp/app_launcher_handler.h"

#include <string>
#include <vector>

#include "base/auto_reset.h"
#include "base/command_line.h"
#include "base/metrics/histogram.h"
#include "base/string_number_conversions.h"
#include "base/string_split.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/extensions/apps_promo.h"
#include "chrome/browser/extensions/extension_prefs.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/webui/extension_icon_source.h"
#include "chrome/browser/ui/webui/ntp/shown_sections_handler.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chrome/common/extensions/extension_icon_set.h"
#include "chrome/common/extensions/extension_resource.h"
#include "chrome/common/url_constants.h"
#include "content/browser/disposition_utils.h"
#include "content/browser/tab_contents/tab_contents.h"
#include "content/common/notification_service.h"
#include "content/common/notification_type.h"
#include "googleurl/src/gurl.h"
#include "grit/browser_resources.h"
#include "grit/generated_resources.h"
#include "net/base/escape.h"
#include "ui/base/animation/animation.h"
#include "webkit/glue/window_open_disposition.h"

namespace {

// The URL prefixes used by the NTP to signal when the web store or an app
// has launched so we can record the proper histogram.
const char* kPingLaunchAppByID = "record-app-launch-by-id";
const char* kPingLaunchWebStore = "record-webstore-launch";
const char* kPingLaunchAppByURL = "record-app-launch-by-url";

const UnescapeRule::Type kUnescapeRules =
    UnescapeRule::NORMAL | UnescapeRule::URL_SPECIAL_CHARS;

extension_misc::AppLaunchBucket ParseLaunchSource(
    const std::string& launch_source) {
  int bucket_num = extension_misc::APP_LAUNCH_BUCKET_INVALID;
  base::StringToInt(launch_source, &bucket_num);
  extension_misc::AppLaunchBucket bucket =
      static_cast<extension_misc::AppLaunchBucket>(bucket_num);
  CHECK(bucket < extension_misc::APP_LAUNCH_BUCKET_BOUNDARY);
  return bucket;
}

}  // namespace

AppLauncherHandler::AppLauncherHandler(ExtensionService* extension_service)
    : extensions_service_(extension_service),
      promo_active_(false),
      ignore_changes_(false) {
}

AppLauncherHandler::~AppLauncherHandler() {}

// static
void AppLauncherHandler::CreateAppInfo(const Extension* extension,
                                       ExtensionPrefs* prefs,
                                       DictionaryValue* value) {
  bool enabled =
      prefs->GetExtensionState(extension->id()) != Extension::DISABLED;
  GURL icon_big =
      ExtensionIconSource::GetIconURL(extension,
                                      Extension::EXTENSION_ICON_LARGE,
                                      ExtensionIconSet::MATCH_EXACTLY,
                                      !enabled);
  GURL icon_small =
      ExtensionIconSource::GetIconURL(extension,
                                      Extension::EXTENSION_ICON_BITTY,
                                      ExtensionIconSet::MATCH_BIGGER,
                                      !enabled);

  value->Clear();
  value->SetString("id", extension->id());
  value->SetString("name", extension->name());
  value->SetString("description", extension->description());
  value->SetString("launch_url", extension->GetFullLaunchURL().spec());
  if (enabled) {
    value->SetString("options_url", extension->options_url().spec());
  }
  value->SetBoolean("can_uninstall",
                    Extension::UserMayDisable(extension->location()));
  value->SetString("icon_big", icon_big.spec());
  value->SetString("icon_small", icon_small.spec());
  value->SetInteger("launch_container", extension->launch_container());
  value->SetInteger("launch_type",
      prefs->GetLaunchType(extension->id(),
                                     ExtensionPrefs::LAUNCH_DEFAULT));
  value->SetBoolean("is_component",
      extension->location() == Extension::COMPONENT);

  int app_launch_index = prefs->GetAppLaunchIndex(extension->id());
  if (app_launch_index == -1) {
    // Make sure every app has a launch index (some predate the launch index).
    app_launch_index = prefs->GetNextAppLaunchIndex();
    prefs->SetAppLaunchIndex(extension->id(), app_launch_index);
  }
  value->SetInteger("app_launch_index", app_launch_index);

  int page_index = prefs->GetPageIndex(extension->id());
  if (page_index >= 0) {
    // Only provide a value if one is stored
    value->SetInteger("page_index", page_index);
  }
}

// static
bool AppLauncherHandler::HandlePing(Profile* profile, const std::string& path) {
  std::vector<std::string> params;
  base::SplitString(path, '+', &params);

  // Check if the user launched an app from the most visited or recently
  // closed sections.
  if (kPingLaunchAppByURL == params.at(0)) {
    CHECK(params.size() == 3);
    RecordAppLaunchByURL(
        profile, params.at(1), ParseLaunchSource(params.at(2)));
    return true;
  }

  bool is_web_store_ping = kPingLaunchWebStore == params.at(0);
  bool is_app_launch_ping = kPingLaunchAppByID == params.at(0);

  if (!is_web_store_ping && !is_app_launch_ping)
    return false;

  CHECK(params.size() >= 2);

  bool is_promo_active = params.at(1) == "true";

  // At this point, the user must have used the app launcher, so we hide the
  // promo if its still displayed.
  if (is_promo_active) {
    DCHECK(profile->GetExtensionService());
    profile->GetExtensionService()->apps_promo()->ExpireDefaultApps();
  }

  if (is_web_store_ping) {
    RecordWebStoreLaunch(is_promo_active);
  }  else {
    CHECK(params.size() == 3);
    RecordAppLaunchByID(is_promo_active, ParseLaunchSource(params.at(2)));
  }

  return true;
}

WebUIMessageHandler* AppLauncherHandler::Attach(WebUI* web_ui) {
  // TODO(arv): Add initialization code to the Apps store etc.
  return WebUIMessageHandler::Attach(web_ui);
}

void AppLauncherHandler::RegisterMessages() {
  web_ui_->RegisterMessageCallback("getApps",
      NewCallback(this, &AppLauncherHandler::HandleGetApps));
  web_ui_->RegisterMessageCallback("launchApp",
      NewCallback(this, &AppLauncherHandler::HandleLaunchApp));
  web_ui_->RegisterMessageCallback("setLaunchType",
      NewCallback(this, &AppLauncherHandler::HandleSetLaunchType));
  web_ui_->RegisterMessageCallback("uninstallApp",
      NewCallback(this, &AppLauncherHandler::HandleUninstallApp));
  web_ui_->RegisterMessageCallback("hideAppsPromo",
      NewCallback(this, &AppLauncherHandler::HandleHideAppsPromo));
  web_ui_->RegisterMessageCallback("createAppShortcut",
      NewCallback(this, &AppLauncherHandler::HandleCreateAppShortcut));
  web_ui_->RegisterMessageCallback("reorderApps",
      NewCallback(this, &AppLauncherHandler::HandleReorderApps));
  web_ui_->RegisterMessageCallback("setPageIndex",
      NewCallback(this, &AppLauncherHandler::HandleSetPageIndex));
  web_ui_->RegisterMessageCallback("promoSeen",
      NewCallback(this, &AppLauncherHandler::HandlePromoSeen));
}

void AppLauncherHandler::Observe(NotificationType type,
                                 const NotificationSource& source,
                                 const NotificationDetails& details) {
  if (ignore_changes_)
    return;

  switch (type.value) {
    case NotificationType::EXTENSION_LOADED:
    case NotificationType::EXTENSION_UNLOADED:
    case NotificationType::EXTENSION_LAUNCHER_REORDERED:
    // The promo may not load until a couple seconds after the first NTP view,
    // so we listen for the load notification and notify the NTP when ready.
    case NotificationType::WEB_STORE_PROMO_LOADED:
      if (web_ui_->tab_contents())
        HandleGetApps(NULL);
      break;
    case NotificationType::PREF_CHANGED: {
      if (!web_ui_->tab_contents())
        break;

      DictionaryValue dictionary;
      FillAppDictionary(&dictionary);
      web_ui_->CallJavascriptFunction("appsPrefChangeCallback", dictionary);
      break;
    }
    default:
      NOTREACHED();
  }
}

void AppLauncherHandler::FillAppDictionary(DictionaryValue* dictionary) {
  ListValue* list = new ListValue();
  const ExtensionList* extensions = extensions_service_->extensions();
  ExtensionList::const_iterator it;
  for (it = extensions->begin(); it != extensions->end(); ++it) {
    // Don't include the WebStore.
    // The WebStore launcher gets special treatment in ntp/apps.js.
    if ((*it)->is_app() &&
        (*it)->id() != extension_misc::kWebStoreAppId) {
      DictionaryValue* app_info = new DictionaryValue();
      CreateAppInfo(*it, extensions_service_->extension_prefs(), app_info);
      list->Append(app_info);
    }
  }

  extensions = extensions_service_->disabled_extensions();
  for (it = extensions->begin(); it != extensions->end(); ++it) {
    if ((*it)->is_app() &&
        (*it)->id() != extension_misc::kWebStoreAppId) {
      DictionaryValue* app_info = new DictionaryValue();
      CreateAppInfo(*it, extensions_service_->extension_prefs(), app_info);
      list->Append(app_info);
    }
  }

  dictionary->Set("apps", list);

#if defined(OS_MACOSX)
  // App windows are not yet implemented on mac.
  dictionary->SetBoolean("disableAppWindowLaunch", true);
  dictionary->SetBoolean("disableCreateAppShortcut", true);
#endif

#if defined(OS_CHROMEOS)
  // Making shortcut does not make sense on ChromeOS because it does not have
  // a desktop.
  dictionary->SetBoolean("disableCreateAppShortcut", true);
#endif

  dictionary->SetBoolean(
      "showLauncher",
      extensions_service_->apps_promo()->ShouldShowAppLauncher(
          extensions_service_->GetAppIds()));
}

void AppLauncherHandler::FillPromoDictionary(DictionaryValue* dictionary) {
  dictionary->SetString("promoHeader", AppsPromo::GetPromoHeaderText());
  dictionary->SetString("promoButton", AppsPromo::GetPromoButtonText());
  dictionary->SetString("promoLink", AppsPromo::GetPromoLink().spec());
  dictionary->SetString("promoLogo", AppsPromo::GetPromoLogo().spec());
  dictionary->SetString("promoExpire", AppsPromo::GetPromoExpireText());
}

void AppLauncherHandler::HandleGetApps(const ListValue* args) {
  DictionaryValue dictionary;

  // Tell the client whether to show the promo for this view. We don't do this
  // in the case of PREF_CHANGED because:
  //
  // a) At that point in time, depending on the pref that changed, it can look
  //    like the set of apps installed has changed, and we will mark the promo
  //    expired.
  // b) Conceptually, it doesn't really make sense to count a
  //    prefchange-triggered refresh as a promo 'view'.
  AppsPromo* apps_promo = extensions_service_->apps_promo();
  PrefService* prefs = web_ui_->GetProfile()->GetPrefs();
  bool apps_promo_just_expired = false;
  if (apps_promo->ShouldShowPromo(extensions_service_->GetAppIds(),
                                  &apps_promo_just_expired)) {
    apps_promo->MaximizeAppsIfNecessary();
    dictionary.SetBoolean("showPromo", true);
    FillPromoDictionary(&dictionary);
    promo_active_ = true;
  } else {
    dictionary.SetBoolean("showPromo", false);
    promo_active_ = false;
  }

  // If the default apps have just expired (user viewed them too many times with
  // no interaction), then we uninstall them and focus the recent sites section.
  if (apps_promo_just_expired) {
    ignore_changes_ = true;
    UninstallDefaultApps();
    ignore_changes_ = false;
    ShownSectionsHandler::SetShownSection(prefs, THUMB);
  }

  FillAppDictionary(&dictionary);
  web_ui_->CallJavascriptFunction("getAppsCallback", dictionary);

  // First time we get here we set up the observer so that we can tell update
  // the apps as they change.
  if (registrar_.IsEmpty()) {
    registrar_.Add(this, NotificationType::EXTENSION_LOADED,
        NotificationService::AllSources());
    registrar_.Add(this, NotificationType::EXTENSION_UNLOADED,
        NotificationService::AllSources());
    registrar_.Add(this, NotificationType::EXTENSION_LAUNCHER_REORDERED,
        NotificationService::AllSources());
    registrar_.Add(this, NotificationType::WEB_STORE_PROMO_LOADED,
        NotificationService::AllSources());
  }
  if (pref_change_registrar_.IsEmpty()) {
    pref_change_registrar_.Init(
        extensions_service_->extension_prefs()->pref_service());
    pref_change_registrar_.Add(ExtensionPrefs::kExtensionsPref, this);
  }
}

void AppLauncherHandler::HandleLaunchApp(const ListValue* args) {
  std::string extension_id;
  double source = -1.0;
  bool alt_key = false;
  bool ctrl_key = false;
  bool meta_key = false;
  bool shift_key = false;
  double button = 0.0;

  CHECK(args->GetString(0, &extension_id));
  CHECK(args->GetDouble(1, &source));
  if (args->GetSize() > 2) {
      CHECK(args->GetBoolean(2, &alt_key));
      CHECK(args->GetBoolean(3, &ctrl_key));
      CHECK(args->GetBoolean(4, &meta_key));
      CHECK(args->GetBoolean(5, &shift_key));
      CHECK(args->GetDouble(6, &button));
  }

  extension_misc::AppLaunchBucket launch_bucket =
      static_cast<extension_misc::AppLaunchBucket>(
          static_cast<int>(source));
  CHECK(launch_bucket >= 0 &&
        launch_bucket < extension_misc::APP_LAUNCH_BUCKET_BOUNDARY);

  const Extension* extension =
      extensions_service_->GetExtensionById(extension_id, false);

  // Prompt the user to re-enable the application if disabled.
  if (!extension) {
    PromptToEnableApp(extension_id);
    return;
  }

  Profile* profile = extensions_service_->profile();

  // If the user pressed special keys when clicking, override the saved
  // preference for launch container.
  bool middle_button = (button == 1.0);
  WindowOpenDisposition disposition =
        disposition_utils::DispositionFromClick(middle_button, alt_key,
                                                ctrl_key, meta_key, shift_key);

  if (extension_id != extension_misc::kWebStoreAppId) {
    RecordAppLaunchByID(promo_active_, launch_bucket);
    extensions_service_->apps_promo()->ExpireDefaultApps();
  }

  if (disposition == NEW_FOREGROUND_TAB || disposition == NEW_BACKGROUND_TAB) {
    // TODO(jamescook): Proper support for background tabs.
    Browser::OpenApplication(
        profile, extension, extension_misc::LAUNCH_TAB, disposition);
  } else if (disposition == NEW_WINDOW) {
    // Force a new window open.
    Browser::OpenApplication(
            profile, extension, extension_misc::LAUNCH_WINDOW, disposition);
  } else {
    // Look at preference to find the right launch container.  If no preference
    // is set, launch as a regular tab.
    extension_misc::LaunchContainer launch_container =
        extensions_service_->extension_prefs()->GetLaunchContainer(
            extension, ExtensionPrefs::LAUNCH_REGULAR);

    // To give a more "launchy" experience when using the NTP launcher, we close
    // it automatically.
    Browser* browser = BrowserList::GetLastActive();
    TabContents* old_contents = NULL;
    if (browser)
      old_contents = browser->GetSelectedTabContents();

    TabContents* new_contents = Browser::OpenApplication(
        profile, extension, launch_container,
        old_contents ? CURRENT_TAB : NEW_FOREGROUND_TAB);

    // This will also destroy the handler, so do not perform any actions after.
    if (new_contents != old_contents && browser->tab_count() > 1)
      browser->CloseTabContents(old_contents);
  }
}

void AppLauncherHandler::HandleSetLaunchType(const ListValue* args) {
  std::string extension_id;
  double launch_type;
  CHECK(args->GetString(0, &extension_id));
  CHECK(args->GetDouble(1, &launch_type));

  const Extension* extension =
      extensions_service_->GetExtensionById(extension_id, true);
  CHECK(extension);

  extensions_service_->extension_prefs()->SetLaunchType(
      extension_id,
      static_cast<ExtensionPrefs::LaunchType>(
          static_cast<int>(launch_type)));
}

void AppLauncherHandler::HandleUninstallApp(const ListValue* args) {
  std::string extension_id = UTF16ToUTF8(ExtractStringValue(args));
  const Extension* extension = extensions_service_->GetExtensionById(
      extension_id, false);
  if (!extension)
    return;

  if (!Extension::UserMayDisable(extension->location())) {
    LOG(ERROR) << "Attempt to uninstall an extension that is non-usermanagable "
               << "was made. Extension id : " << extension->id();
    return;
  }
  if (!extension_id_prompting_.empty())
    return;  // Only one prompt at a time.

  extension_id_prompting_ = extension_id;
  GetExtensionUninstallDialog()->ConfirmUninstall(this, extension);
}

void AppLauncherHandler::HandleHideAppsPromo(const ListValue* args) {
  // If the user has intentionally hidden the promotion, we'll uninstall all the
  // default apps (we know the user hasn't installed any apps on their own at
  // this point, or the promotion wouldn't have been shown).
  ignore_changes_ = true;
  UninstallDefaultApps();
  extensions_service_->apps_promo()->HidePromo();
  ignore_changes_ = false;
  HandleGetApps(NULL);
}

void AppLauncherHandler::HandleCreateAppShortcut(const ListValue* args) {
  std::string extension_id;
  if (!args->GetString(0, &extension_id)) {
    NOTREACHED();
    return;
  }

  const Extension* extension =
      extensions_service_->GetExtensionById(extension_id, true);
  CHECK(extension);

  Browser* browser = BrowserList::GetLastActive();
  if (!browser)
    return;
  browser->window()->ShowCreateChromeAppShortcutsDialog(
      browser->profile(), extension);
}

void AppLauncherHandler::HandleReorderApps(const ListValue* args) {
  CHECK(args->GetSize() == 2);

  std::string dragged_app_id;
  ListValue* app_order;
  CHECK(args->GetString(0, &dragged_app_id));
  CHECK(args->GetList(1, &app_order));

  std::vector<std::string> extension_ids;
  for (size_t i = 0; i < app_order->GetSize(); ++i) {
    std::string value;
    if (app_order->GetString(i, &value))
      extension_ids.push_back(value);
  }

  // Don't update the page; it already knows the apps have been reordered.
  scoped_ptr<AutoReset<bool> > auto_reset;
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kNewTabPage4))
    auto_reset.reset(new AutoReset<bool>(&ignore_changes_, true));

  extensions_service_->extension_prefs()->SetAppDraggedByUser(dragged_app_id);
  extensions_service_->extension_prefs()->SetAppLauncherOrder(extension_ids);
}

void AppLauncherHandler::HandleSetPageIndex(const ListValue* args) {
  std::string extension_id;
  double page_index;
  CHECK(args->GetString(0, &extension_id));
  CHECK(args->GetDouble(1, &page_index));

  extensions_service_->extension_prefs()->SetPageIndex(extension_id,
      static_cast<int>(page_index));
}

void AppLauncherHandler::HandlePromoSeen(const ListValue* args) {
  UMA_HISTOGRAM_ENUMERATION(extension_misc::kAppsPromoHistogram,
                            extension_misc::PROMO_SEEN,
                            extension_misc::PROMO_BUCKET_BOUNDARY);
}

// static
void AppLauncherHandler::RecordWebStoreLaunch(bool promo_active) {
  UMA_HISTOGRAM_ENUMERATION(extension_misc::kAppLaunchHistogram,
                            extension_misc::APP_LAUNCH_NTP_WEBSTORE,
                            extension_misc::APP_LAUNCH_BUCKET_BOUNDARY);

  if (!promo_active) return;

  UMA_HISTOGRAM_ENUMERATION(extension_misc::kAppsPromoHistogram,
                            extension_misc::PROMO_LAUNCH_WEB_STORE,
                            extension_misc::PROMO_BUCKET_BOUNDARY);
}

// static
void AppLauncherHandler::RecordAppLaunchByID(
    bool promo_active, extension_misc::AppLaunchBucket bucket) {
  CHECK(bucket != extension_misc::APP_LAUNCH_BUCKET_INVALID);

  UMA_HISTOGRAM_ENUMERATION(extension_misc::kAppLaunchHistogram, bucket,
                            extension_misc::APP_LAUNCH_BUCKET_BOUNDARY);

  if (!promo_active) return;

  UMA_HISTOGRAM_ENUMERATION(extension_misc::kAppsPromoHistogram,
                            extension_misc::PROMO_LAUNCH_APP,
                            extension_misc::PROMO_BUCKET_BOUNDARY);
}

// static
void AppLauncherHandler::RecordAppLaunchByURL(
    Profile* profile,
    std::string escaped_url,
    extension_misc::AppLaunchBucket bucket) {
  CHECK(bucket != extension_misc::APP_LAUNCH_BUCKET_INVALID);

  GURL url(UnescapeURLComponent(escaped_url, kUnescapeRules));
  DCHECK(profile->GetExtensionService());
  if (!profile->GetExtensionService()->IsInstalledApp(url))
    return;

  UMA_HISTOGRAM_ENUMERATION(extension_misc::kAppLaunchHistogram, bucket,
                            extension_misc::APP_LAUNCH_BUCKET_BOUNDARY);
}

void AppLauncherHandler::PromptToEnableApp(const std::string& extension_id) {
  const Extension* extension =
      extensions_service_->GetExtensionById(extension_id, true);
  CHECK(extension);

  ExtensionPrefs* extension_prefs = extensions_service_->extension_prefs();
  if (!extension_prefs->DidExtensionEscalatePermissions(extension_id)) {
    // Enable the extension immediately if its privileges weren't escalated.
    extensions_service_->EnableExtension(extension_id);

    // Launch app asynchronously so the image will update.
    StringValue* app_id = Value::CreateStringValue(extension->id());
    web_ui_->CallJavascriptFunction("launchAppAfterEnable", *app_id);
    return;
  }

  if (!extension_id_prompting_.empty())
    return;  // Only one prompt at a time.

  extension_id_prompting_ = extension_id;
  GetExtensionInstallUI()->ConfirmReEnable(this, extension);
}

void AppLauncherHandler::ExtensionDialogAccepted() {
  // Do the uninstall work here.
  DCHECK(!extension_id_prompting_.empty());

  // The extension can be uninstalled in another window while the UI was
  // showing. Do nothing in that case.
  const Extension* extension =
      extensions_service_->GetExtensionById(extension_id_prompting_, true);
  if (!extension)
    return;

  extensions_service_->UninstallExtension(extension_id_prompting_,
                                          false /* external_uninstall */, NULL);

  extension_id_prompting_ = "";
}

void AppLauncherHandler::ExtensionDialogCanceled() {
  const Extension* extension =
      extensions_service_->GetExtensionById(extension_id_prompting_, true);
  ExtensionService::RecordPermissionMessagesHistogram(
      extension, "Extensions.Permissions_ReEnableCancel");

  extension_id_prompting_ = "";
}

void AppLauncherHandler::InstallUIProceed() {
  // Do the re-enable work here.
  DCHECK(!extension_id_prompting_.empty());

  // The extension can be uninstalled in another window while the UI was
  // showing. Do nothing in that case.
  const Extension* extension =
      extensions_service_->GetExtensionById(extension_id_prompting_, true);
  if (!extension)
    return;

  extensions_service_->GrantPermissionsAndEnableExtension(extension);

  // We bounce this off the NTP so the browser can update the apps icon.
  // If we don't launch the app asynchronously, then the app's disabled
  // icon disappears but isn't replaced by the enabled icon, making a poor
  // visual experience.
  StringValue* app_id = Value::CreateStringValue(extension->id());
  web_ui_->CallJavascriptFunction("launchAppAfterEnable", *app_id);

  extension_id_prompting_ = "";
}

void AppLauncherHandler::InstallUIAbort() {
  ExtensionDialogCanceled();
}

ExtensionUninstallDialog* AppLauncherHandler::GetExtensionUninstallDialog() {
  if (!extension_uninstall_dialog_.get()) {
    extension_uninstall_dialog_.reset(
        new ExtensionUninstallDialog(web_ui_->GetProfile()));
  }
  return extension_uninstall_dialog_.get();
}

ExtensionInstallUI* AppLauncherHandler::GetExtensionInstallUI() {
  if (!extension_install_ui_.get()) {
    extension_install_ui_.reset(
        new ExtensionInstallUI(web_ui_->GetProfile()));
  }
  return extension_install_ui_.get();
}

void AppLauncherHandler::UninstallDefaultApps() {
  AppsPromo* apps_promo = extensions_service_->apps_promo();
  const ExtensionIdSet& app_ids = apps_promo->old_default_apps();
  for (ExtensionIdSet::const_iterator iter = app_ids.begin();
       iter != app_ids.end(); ++iter) {
    if (extensions_service_->GetExtensionById(*iter, true))
      extensions_service_->UninstallExtension(*iter, false, NULL);
  }
}
