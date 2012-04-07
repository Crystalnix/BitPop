// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_service.h"

#include <algorithm>
#include <set>

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/histogram.h"
#include "base/path_service.h"
#include "base/stl_util.h"
#include "base/string16.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/stringprintf.h"
#include "base/threading/thread_restrictions.h"
#include "base/time.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "base/version.h"
#include "chrome/browser/accessibility/accessibility_extension_api.h"
#include "chrome/browser/bookmarks/bookmark_extension_api.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_plugin_service_filter.h"
#include "chrome/browser/download/download_extension_api.h"
#include "chrome/browser/extensions/api/socket/socket_api_controller.h"
#include "chrome/browser/extensions/app_notification_manager.h"
#include "chrome/browser/extensions/apps_promo.h"
#include "chrome/browser/extensions/component_loader.h"
#include "chrome/browser/extensions/crx_installer.h"
#include "chrome/browser/extensions/default_apps_trial.h"
#include "chrome/browser/extensions/extension_browser_event_router.h"
#include "chrome/browser/extensions/extension_cookies_api.h"
#include "chrome/browser/extensions/extension_data_deleter.h"
#include "chrome/browser/extensions/extension_error_reporter.h"
#include "chrome/browser/extensions/extension_global_error.h"
#include "chrome/browser/extensions/extension_host.h"
#include "chrome/browser/extensions/extension_input_ime_api.h"
#include "chrome/browser/extensions/extension_management_api.h"
#include "chrome/browser/extensions/extension_preference_api.h"
#include "chrome/browser/extensions/extension_process_manager.h"
#include "chrome/browser/extensions/extension_processes_api.h"
#include "chrome/browser/extensions/extension_sorting.h"
#include "chrome/browser/extensions/extension_special_storage_policy.h"
#include "chrome/browser/extensions/extension_sync_data.h"
#include "chrome/browser/extensions/extension_updater.h"
#include "chrome/browser/extensions/extension_web_ui.h"
#include "chrome/browser/extensions/extension_webnavigation_api.h"
#include "chrome/browser/extensions/external_extension_provider_impl.h"
#include "chrome/browser/extensions/external_extension_provider_interface.h"
#include "chrome/browser/extensions/installed_loader.h"
#include "chrome/browser/extensions/pending_extension_manager.h"
#include "chrome/browser/extensions/permissions_updater.h"
#include "chrome/browser/extensions/settings/settings_frontend.h"
#include "chrome/browser/extensions/unpacked_installer.h"
#include "chrome/browser/history/history_extension_api.h"
#include "chrome/browser/net/chrome_url_request_context.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/search_engines/template_url_service.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/sync/api/sync_change.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/global_error_service.h"
#include "chrome/browser/ui/global_error_service_factory.h"
#include "chrome/browser/ui/webui/chrome_url_data_manager.h"
#include "chrome/browser/ui/webui/extensions/extension_icon_source.h"
#include "chrome/browser/ui/webui/favicon_source.h"
#include "chrome/browser/ui/webui/ntp/thumbnail_source.h"
#include "chrome/browser/web_applications/web_app.h"
#include "chrome/common/child_process_logging.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chrome/common/extensions/extension_error_utils.h"
#include "chrome/common/extensions/extension_file_util.h"
#include "chrome/common/extensions/extension_messages.h"
#include "chrome/common/extensions/extension_resource.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "content/browser/plugin_process_host.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/devtools_agent_host_registry.h"
#include "content/public/browser/devtools_manager.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/plugin_service.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/common/pepper_plugin_info.h"
#include "googleurl/src/gurl.h"
#include "grit/theme_resources.h"
#include "net/base/registry_controlled_domain.h"
#include "webkit/database/database_tracker.h"
#include "webkit/database/database_util.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/extensions/file_browser_event_router.h"
#include "chrome/browser/chromeos/extensions/input_method_event_router.h"
#include "chrome/browser/chromeos/extensions/media_player_event_router.h"
#include "chrome/browser/chromeos/input_method/input_method_manager.h"
#include "chrome/browser/extensions/extension_input_ime_api.h"
#include "webkit/fileapi/file_system_context.h"
#include "webkit/fileapi/file_system_mount_point_provider.h"
#endif

#if defined(OS_CHROMEOS) && defined(USE_VIRTUAL_KEYBOARD)
#include "chrome/browser/extensions/extension_input_ui_api.h"
#endif

using base::Time;
using content::BrowserThread;
using content::DevToolsAgentHost;
using content::DevToolsAgentHostRegistry;
using content::PluginService;

namespace errors = extension_manifest_errors;

namespace {

#if defined(OS_LINUX)
static const int kOmniboxIconPaddingLeft = 2;
static const int kOmniboxIconPaddingRight = 2;
#elif defined(OS_MACOSX)
static const int kOmniboxIconPaddingLeft = 0;
static const int kOmniboxIconPaddingRight = 2;
#else
static const int kOmniboxIconPaddingLeft = 0;
static const int kOmniboxIconPaddingRight = 0;
#endif

const char* kNaClPluginMimeType = "application/x-nacl";

static void ForceShutdownPlugin(const FilePath& plugin_path) {
  PluginProcessHost* plugin =
      PluginService::GetInstance()->FindNpapiPluginProcess(plugin_path);
  if (plugin)
    plugin->ForceShutdown();
}

static bool IsSyncableExtension(const Extension& extension) {
  return extension.GetSyncType() == Extension::SYNC_TYPE_EXTENSION;
}

static bool IsSyncableApp(const Extension& extension) {
  return extension.GetSyncType() == Extension::SYNC_TYPE_APP;
}

}  // namespace

ExtensionService::ExtensionRuntimeData::ExtensionRuntimeData()
    : background_page_ready(false),
      being_upgraded(false),
      has_used_webrequest(false) {
}

ExtensionService::ExtensionRuntimeData::~ExtensionRuntimeData() {
}

ExtensionService::NaClModuleInfo::NaClModuleInfo() {
}

ExtensionService::NaClModuleInfo::~NaClModuleInfo() {
}

// ExtensionService.

const char* ExtensionService::kInstallDirectoryName = "Extensions";

const char* ExtensionService::kLocalAppSettingsDirectoryName =
    "Local App Settings";
const char* ExtensionService::kLocalExtensionSettingsDirectoryName =
    "Local Extension Settings";
const char* ExtensionService::kSyncAppSettingsDirectoryName =
    "Sync App Settings";
const char* ExtensionService::kSyncExtensionSettingsDirectoryName =
    "Sync Extension Settings";

void ExtensionService::CheckExternalUninstall(const std::string& id) {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // Check if the providers know about this extension.
  ProviderCollection::const_iterator i;
  for (i = external_extension_providers_.begin();
       i != external_extension_providers_.end(); ++i) {
    DCHECK(i->get()->IsReady());
    if (i->get()->HasExtension(id))
      return;  // Yup, known extension, don't uninstall.
  }

  // We get the list of external extensions to check from preferences.
  // It is possible that an extension has preferences but is not loaded.
  // For example, an extension that requires experimental permissions
  // will not be loaded if the experimental command line flag is not used.
  // In this case, do not uninstall.
  const Extension* extension = GetInstalledExtension(id);
  if (!extension) {
    // We can't call UninstallExtension with an unloaded/invalid
    // extension ID.
    LOG(WARNING) << "Attempted uninstallation of unloaded/invalid extension "
                 << "with id: " << id;
    return;
  }
  UninstallExtension(id, true, NULL);
}

void ExtensionService::ClearProvidersForTesting() {
  external_extension_providers_.clear();
}

void ExtensionService::AddProviderForTesting(
    ExternalExtensionProviderInterface* test_provider) {
  CHECK(test_provider);
  external_extension_providers_.push_back(
      linked_ptr<ExternalExtensionProviderInterface>(test_provider));
}

bool ExtensionService::OnExternalExtensionUpdateUrlFound(
    const std::string& id,
    const GURL& update_url,
    Extension::Location location) {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  CHECK(Extension::IdIsValid(id));

  const Extension* extension = GetExtensionById(id, true);
  if (extension) {
    // Already installed. Skip this install if the current location has
    // higher priority than |location|.
    Extension::Location current = extension->location();
    if (current == Extension::GetHigherPriorityLocation(current, location))
      return false;
    // Otherwise, overwrite the current installation.
  }

  // Add |id| to the set of pending extensions.  If it can not be added,
  // then there is already a pending record from a higher-priority install
  // source.  In this case, signal that this extension will not be
  // installed by returning false.
  if (!pending_extension_manager()->AddFromExternalUpdateUrl(
          id, update_url, location))
    return false;

  external_extension_url_added_ = true;
  return true;
}

// If a download url matches one of these patterns and has a referrer of the
// webstore, then we're willing to treat that as a gallery download.
static const char* kAllowedDownloadURLPatterns[] = {
  "https://clients2.google.com/service/update2*",
  "https://clients2.googleusercontent.com/crx/*"
};

bool ExtensionService::IsDownloadFromGallery(const GURL& download_url,
                                             const GURL& referrer_url) {
  const Extension* download_extension =
      extensions_.GetHostedAppByURL(ExtensionURLInfo(download_url));
  const Extension* referrer_extension =
      extensions_.GetHostedAppByURL(ExtensionURLInfo(referrer_url));
  const Extension* webstore_app = GetWebStoreApp();

  bool referrer_valid = (referrer_extension == webstore_app);
  bool download_valid = (download_extension == webstore_app);

  // We also allow the download to be from a small set of trusted paths.
  if (!download_valid) {
    for (size_t i = 0; i < arraysize(kAllowedDownloadURLPatterns); i++) {
      URLPattern pattern(URLPattern::SCHEME_HTTPS,
                         kAllowedDownloadURLPatterns[i]);
      if (pattern.MatchesURL(download_url)) {
        download_valid = true;
        break;
      }
    }
  }

  // If the command-line gallery URL is set, then be a bit more lenient.
  GURL store_url =
      GURL(CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
           switches::kAppsGalleryURL));
  if (!store_url.is_empty()) {
    std::string store_tld =
        net::RegistryControlledDomainService::GetDomainAndRegistry(store_url);
    if (!referrer_valid) {
      std::string referrer_tld =
          net::RegistryControlledDomainService::GetDomainAndRegistry(
              referrer_url);
      // The referrer gets stripped when transitioning from https to http,
      // or when hitting an unknown test cert and that commonly happens in
      // testing environments.  Given this, we allow an empty referrer when
      // the command-line flag is set.
      // Otherwise, the TLD must match the TLD of the command-line url.
      referrer_valid = referrer_url.is_empty() || (referrer_tld == store_tld);
    }

    if (!download_valid) {
      std::string download_tld =
          net::RegistryControlledDomainService::GetDomainAndRegistry(
              download_url);

      // Otherwise, the TLD must match the TLD of the command-line url.
      download_valid = (download_tld == store_tld);
    }
  }

  return (referrer_valid && download_valid);
}

const Extension* ExtensionService::GetInstalledApp(const GURL& url) {
  const Extension* extension = extensions_.GetExtensionOrAppByURL(
      ExtensionURLInfo(url));
  if (extension && extension->is_app())
    return extension;

  return NULL;
}

bool ExtensionService::IsInstalledApp(const GURL& url) {
  return !!GetInstalledApp(url);
}

void ExtensionService::SetInstalledAppForRenderer(int renderer_child_id,
                                                  const Extension* app) {
  installed_app_hosts_[renderer_child_id] = app;
}

const Extension* ExtensionService::GetInstalledAppForRenderer(
    int renderer_child_id) {
  InstalledAppMap::iterator i = installed_app_hosts_.find(renderer_child_id);
  if (i == installed_app_hosts_.end())
    return NULL;
  return i->second;
}

// static
// This function is used to implement the command-line switch
// --uninstall-extension, and to uninstall an extension via sync.  The LOG
// statements within this function are used to inform the user if the uninstall
// cannot be done.
bool ExtensionService::UninstallExtensionHelper(
    ExtensionService* extensions_service,
    const std::string& extension_id) {

  const Extension* extension =
      extensions_service->GetInstalledExtension(extension_id);

  // We can't call UninstallExtension with an invalid extension ID.
  if (!extension) {
    LOG(WARNING) << "Attempted uninstallation of non-existent extension with "
                 << "id: " << extension_id;
    return false;
  }

  // The following call to UninstallExtension will not allow an uninstall of a
  // policy-controlled extension.
  std::string error;
  if (!extensions_service->UninstallExtension(extension_id, false, &error)) {
    LOG(WARNING) << "Cannot uninstall extension with id " << extension_id
                 << ": " << error;
    return false;
  }

  return true;
}

ExtensionService::ExtensionService(Profile* profile,
                                   const CommandLine* command_line,
                                   const FilePath& install_directory,
                                   ExtensionPrefs* extension_prefs,
                                   bool autoupdate_enabled,
                                   bool extensions_enabled)
    : profile_(profile),
      extension_prefs_(extension_prefs),
      settings_frontend_(extensions::SettingsFrontend::Create(profile)),
      pending_extension_manager_(*ALLOW_THIS_IN_INITIALIZER_LIST(this)),
      install_directory_(install_directory),
      extensions_enabled_(extensions_enabled),
      show_extensions_prompts_(true),
      ready_(false),
      toolbar_model_(ALLOW_THIS_IN_INITIALIZER_LIST(this)),
      menu_manager_(profile),
      app_notification_manager_(new AppNotificationManager(profile)),
      apps_promo_(profile->GetPrefs()),
      event_routers_initialized_(false),
      extension_warnings_(profile),
      socket_controller_(NULL),
      tracker_(ALLOW_THIS_IN_INITIALIZER_LIST(this)) {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // Figure out if extension installation should be enabled.
  if (command_line->HasSwitch(switches::kDisableExtensions)) {
    extensions_enabled_ = false;
  } else if (profile->GetPrefs()->GetBoolean(prefs::kDisableExtensions)) {
    extensions_enabled_ = false;
  }

  registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_PROCESS_TERMINATED,
                 content::NotificationService::AllBrowserContextsAndSources());
  registrar_.Add(this, content::NOTIFICATION_RENDERER_PROCESS_CREATED,
                 content::NotificationService::AllBrowserContextsAndSources());
  registrar_.Add(this, content::NOTIFICATION_RENDERER_PROCESS_TERMINATED,
                 content::NotificationService::AllBrowserContextsAndSources());
  pref_change_registrar_.Init(profile->GetPrefs());
  pref_change_registrar_.Add(prefs::kExtensionInstallAllowList, this);
  pref_change_registrar_.Add(prefs::kExtensionInstallDenyList, this);

  // Set up the ExtensionUpdater
  if (autoupdate_enabled) {
    int update_frequency = kDefaultUpdateFrequencySeconds;
    if (command_line->HasSwitch(switches::kExtensionsUpdateFrequency)) {
      base::StringToInt(command_line->GetSwitchValueASCII(
          switches::kExtensionsUpdateFrequency),
          &update_frequency);
    }
    updater_.reset(new ExtensionUpdater(this,
                                        extension_prefs,
                                        profile->GetPrefs(),
                                        profile,
                                        update_frequency));
  }

  component_loader_.reset(
      new extensions::ComponentLoader(this,
                                      profile->GetPrefs(),
                                      g_browser_process->local_state()));

  app_notification_manager_->Init();

  if (extensions_enabled_) {
    if (!command_line->HasSwitch(switches::kImport) &&
        !command_line->HasSwitch(switches::kImportFromFile)) {
      ExternalExtensionProviderImpl::CreateExternalProviders(
          this, profile_, &external_extension_providers_);
    }
  }

  // Use monochrome icons for Omnibox icons.
  omnibox_popup_icon_manager_.set_monochrome(true);
  omnibox_icon_manager_.set_monochrome(true);
  omnibox_icon_manager_.set_padding(gfx::Insets(0, kOmniboxIconPaddingLeft,
                                                0, kOmniboxIconPaddingRight));

  // How long is the path to the Extensions directory?
  UMA_HISTOGRAM_CUSTOM_COUNTS("Extensions.ExtensionRootPathLength",
                              install_directory_.value().length(), 0, 500, 100);
}

const ExtensionSet* ExtensionService::extensions() const {
  return &extensions_;
}

const ExtensionSet* ExtensionService::disabled_extensions() const {
  return &disabled_extensions_;
}

const ExtensionSet* ExtensionService::terminated_extensions() const {
  return &terminated_extensions_;
}

const ExtensionSet* ExtensionService::GenerateInstalledExtensionsSet() const {
  ExtensionSet* installed_extensions = new ExtensionSet();
  installed_extensions->InsertAll(extensions_);
  installed_extensions->InsertAll(disabled_extensions_);
  installed_extensions->InsertAll(terminated_extensions_);
  return installed_extensions;
}

PendingExtensionManager* ExtensionService::pending_extension_manager() {
  return &pending_extension_manager_;
}

ExtensionService::~ExtensionService() {
  // No need to unload extensions here because they are profile-scoped, and the
  // profile is in the process of being deleted.

  ProviderCollection::const_iterator i;
  for (i = external_extension_providers_.begin();
       i != external_extension_providers_.end(); ++i) {
    ExternalExtensionProviderInterface* provider = i->get();
    provider->ServiceShutdown();
  }

  // TODO(miket): if we find ourselves adding more and more per-API
  // controllers, we should manage them all with an
  // APIControllerController (still working on that name).
  if (socket_controller_) {
    // If this check failed, then a unit test was using sockets but didn't
    // provide the IO thread message loop needed for those sockets to do their
    // job (including destroying themselves at shutdown).
    DCHECK(BrowserThread::IsMessageLoopValid(BrowserThread::IO));
    BrowserThread::DeleteSoon(BrowserThread::IO, FROM_HERE, socket_controller_);
  }
}

void ExtensionService::InitEventRoutersAfterImport() {
  RegisterForImportFinished();
}

void ExtensionService::RegisterForImportFinished() {
  if (!registrar_.IsRegistered(this, chrome::NOTIFICATION_IMPORT_FINISHED,
                               content::Source<Profile>(profile_))) {
    registrar_.Add(this, chrome::NOTIFICATION_IMPORT_FINISHED,
                   content::Source<Profile>(profile_));
  }
}

void ExtensionService::InitAfterImport() {
  CheckForExternalUpdates();

  GarbageCollectExtensions();

  // Idempotent, so although there is a possible race if the import
  // process finished sometime in the middle of ProfileImpl::InitExtensions,
  // it cannot happen twice.
  InitEventRouters();
}

void ExtensionService::InitEventRouters() {
  if (event_routers_initialized_)
    return;

  downloads_event_router_.reset(new ExtensionDownloadsEventRouter(profile_));
  history_event_router_.reset(new HistoryExtensionEventRouter());
  history_event_router_->ObserveProfile(profile_);
  browser_event_router_.reset(new ExtensionBrowserEventRouter(profile_));
  browser_event_router_->Init();
  preference_event_router_.reset(new ExtensionPreferenceEventRouter(profile_));
  bookmark_event_router_.reset(new BookmarkExtensionEventRouter(
      profile_->GetBookmarkModel()));
  bookmark_event_router_->Init();
  cookies_event_router_.reset(new ExtensionCookiesEventRouter(profile_));
  cookies_event_router_->Init();
  management_event_router_.reset(new ExtensionManagementEventRouter(profile_));
  management_event_router_->Init();
  ExtensionProcessesEventRouter::GetInstance()->ObserveProfile(profile_);
  web_navigation_event_router_.reset(
      new ExtensionWebNavigationEventRouter(profile_));
  web_navigation_event_router_->Init();

#if defined(OS_CHROMEOS)
  file_browser_event_router_.reset(
      new ExtensionFileBrowserEventRouter(profile_));
  file_browser_event_router_->ObserveFileSystemEvents();

  input_method_event_router_.reset(
      new chromeos::ExtensionInputMethodEventRouter);

  ExtensionMediaPlayerEventRouter::GetInstance()->Init(profile_);
  ExtensionInputImeEventRouter::GetInstance()->Init();
#endif

#if defined(OS_CHROMEOS) && defined(USE_VIRTUAL_KEYBOARD)
  ExtensionInputUiEventRouter::GetInstance()->Init();
#endif

  event_routers_initialized_ = true;
}

const Extension* ExtensionService::GetExtensionById(
    const std::string& id, bool include_disabled) const {
  return GetExtensionByIdInternal(id, true, include_disabled, false);
}

void ExtensionService::Init() {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  DCHECK(!ready_);  // Can't redo init.
  DCHECK_EQ(extensions_.size(), 0u);

  component_loader_->LoadAll();
  extensions::InstalledLoader(this).LoadAllExtensions();

  // If we are running in the import process, don't bother initializing the
  // extension service since this can interfere with the main browser process
  // that is already running an extension service for this profile.
  // TODO(aa): can we start up even less of ExtensionService?
  // http://crbug.com/107636
  if (!CommandLine::ForCurrentProcess()->HasSwitch(switches::kImport) &&
      !CommandLine::ForCurrentProcess()->HasSwitch(switches::kImportFromFile)) {
    if (g_browser_process->profile_manager() &&
        g_browser_process->profile_manager()->will_import()) {
      RegisterForImportFinished();
    } else {
      // TODO(erikkay) this should probably be deferred to a future point
      // rather than running immediately at startup.
      CheckForExternalUpdates();

      // TODO(erikkay) this should probably be deferred as well.
      GarbageCollectExtensions();
    }
  }
}

bool ExtensionService::UpdateExtension(
    const std::string& id,
    const FilePath& extension_path,
    const GURL& download_url,
    CrxInstaller** out_crx_installer) {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  PendingExtensionInfo pending_extension_info;
  bool is_pending_extension = pending_extension_manager_.GetById(
      id, &pending_extension_info);

  const Extension* extension =
      GetExtensionByIdInternal(id, true, true, false);
  if (!is_pending_extension && !extension) {
    LOG(WARNING) << "Will not update extension " << id
                 << " because it is not installed or pending";
    // Delete extension_path since we're not creating a CrxInstaller
    // that would do it for us.
    if (!BrowserThread::PostTask(
            BrowserThread::FILE, FROM_HERE,
            base::Bind(
                &extension_file_util::DeleteFile, extension_path, false)))
      NOTREACHED();

    return false;
  }

  // We want a silent install only for non-pending extensions and
  // pending extensions that have install_silently set.
  ExtensionInstallUI* client =
      (!is_pending_extension || pending_extension_info.install_silently()) ?
      NULL : new ExtensionInstallUI(profile_);

  scoped_refptr<CrxInstaller> installer(CrxInstaller::Create(this, client));
  installer->set_expected_id(id);
  if (is_pending_extension)
    installer->set_install_source(pending_extension_info.install_source());
  else if (extension)
    installer->set_install_source(extension->location());
  if (pending_extension_info.install_silently())
    installer->set_allow_silent_install(true);
  // If the extension came from sync and its auto-update URL is from the
  // webstore, treat it as a webstore install. Note that we ignore some older
  // extensions with blank auto-update URLs because we are mostly concerned
  // with restrictions on NaCl extensions, which are newer.
  int creation_flags = Extension::NO_FLAGS;
  if ((extension && extension->from_webstore()) ||
      (!extension && pending_extension_info.is_from_sync() &&
       extension_urls::IsWebstoreUpdateUrl(
           pending_extension_info.update_url()))) {
    creation_flags |= Extension::FROM_WEBSTORE;
  }

  // Bookmark apps being updated is kind of a contradiction, but that's because
  // we mark the default apps as bookmark apps, and they're hosted in the web
  // store, thus they can get updated. See http://crbug.com/101605 for more
  // details.
  if (extension && extension->from_bookmark())
    creation_flags |= Extension::FROM_BOOKMARK;

  if (extension) {
    // Additionally, if the extension is an external extension, we preserve the
    // creation flags (usually from_bookmark), even if the current pref values
    // don't reflect them. This is to fix http://crbug.com/109791 for users that
    // had default apps updated and lost the from_bookmark bit.
    ProviderCollection::const_iterator i;
    for (i = external_extension_providers_.begin();
        i != external_extension_providers_.end(); ++i) {
      ExternalExtensionProviderInterface* provider = i->get();
      if (provider->HasExtension(extension->id())) {
        creation_flags |= provider->GetCreationFlags();
        break;
      }
    }
  }
  installer->set_creation_flags(creation_flags);

  installer->set_delete_source(true);
  installer->set_download_url(download_url);
  installer->set_install_cause(extension_misc::INSTALL_CAUSE_UPDATE);
  installer->InstallCrx(extension_path);

  if (out_crx_installer)
    *out_crx_installer = installer;

  return true;
}

void ExtensionService::ReloadExtension(const std::string& extension_id) {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  FilePath path;
  const Extension* current_extension = GetExtensionById(extension_id, false);

  // Disable the extension if it's loaded. It might not be loaded if it crashed.
  if (current_extension) {
    // If the extension has an inspector open for its background page, detach
    // the inspector and hang onto a cookie for it, so that we can reattach
    // later.
    ExtensionProcessManager* manager = profile_->GetExtensionProcessManager();
    ExtensionHost* host = manager->GetBackgroundHostForExtension(extension_id);
    if (host && DevToolsAgentHostRegistry::HasDevToolsAgentHost(
            host->render_view_host())) {
      // Look for an open inspector for the background page.
      DevToolsAgentHost* agent =
          DevToolsAgentHostRegistry::GetDevToolsAgentHost(
              host->render_view_host());
      int devtools_cookie =
          content::DevToolsManager::GetInstance()->DetachClientHost(agent);
      if (devtools_cookie >= 0)
        orphaned_dev_tools_[extension_id] = devtools_cookie;
    }

    path = current_extension->path();
    DisableExtension(extension_id);
    disabled_extension_paths_[extension_id] = path;
  } else {
    path = unloaded_extension_paths_[extension_id];
  }

  // Check the installed extensions to see if what we're reloading was already
  // installed.
  scoped_ptr<ExtensionInfo> installed_extension(
      extension_prefs_->GetInstalledExtensionInfo(extension_id));
  if (installed_extension.get() &&
      installed_extension->extension_manifest.get()) {
    extensions::InstalledLoader(this).Load(*installed_extension, false);
  } else {
    // Otherwise, the extension is unpacked (location LOAD).
    // We should always be able to remember the extension's path. If it's not in
    // the map, someone failed to update |unloaded_extension_paths_|.
    CHECK(!path.empty());
    extensions::UnpackedInstaller::Create(this)->Load(path);
  }
}

bool ExtensionService::UninstallExtension(
    std::string extension_id,
    bool external_uninstall,
    std::string* error) {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  scoped_refptr<const Extension> extension(GetInstalledExtension(extension_id));

  // Callers should not send us nonexistent extensions.
  CHECK(extension);

  // Policy change which triggers an uninstall will always set
  // |external_uninstall| to true so this is the only way to uninstall
  // managed extensions.
  if (!Extension::UserMayDisable(extension->location()) &&
      !external_uninstall) {
    content::NotificationService::current()->Notify(
        chrome::NOTIFICATION_EXTENSION_UNINSTALL_NOT_ALLOWED,
        content::Source<Profile>(profile_),
        content::Details<const Extension>(extension));
    if (error != NULL) {
      *error = errors::kCannotUninstallManagedExtension;
    }
    return false;
  }

  // Extract the data we need for sync now, but don't actually sync until we've
  // completed the uninstallation.
  SyncBundle* sync_bundle = GetSyncBundleForExtension(*extension);

  SyncChange sync_change;
  if (sync_bundle) {
    ExtensionSyncData extension_sync_data(
        *extension,
        IsExtensionEnabled(extension_id),
        IsIncognitoEnabled(extension_id),
        extension_prefs_->GetAppNotificationClientId(extension_id),
        extension_prefs_->IsAppNotificationDisabled(extension_id),
        GetAppLaunchOrdinal(extension_id),
        GetPageOrdinal(extension_id));
    sync_change = extension_sync_data.GetSyncChange(SyncChange::ACTION_DELETE);
  }

  UMA_HISTOGRAM_ENUMERATION("Extensions.UninstallType",
                            extension->GetType(), 100);
  RecordPermissionMessagesHistogram(
      extension, "Extensions.Permissions_Uninstall");

  TemplateURLService* url_service =
      TemplateURLServiceFactory::GetForProfile(profile_);
  if (url_service)
    url_service->UnregisterExtensionKeyword(extension);

  // Unload before doing more cleanup to ensure that nothing is hanging on to
  // any of these resources.
  UnloadExtension(extension_id, extension_misc::UNLOAD_REASON_UNINSTALL);

  extension_prefs_->OnExtensionUninstalled(extension_id, extension->location(),
                                           external_uninstall);

  // Tell the backend to start deleting installed extensions on the file thread.
  if (Extension::LOAD != extension->location()) {
    if (!BrowserThread::PostTask(
            BrowserThread::FILE, FROM_HERE,
            base::Bind(
                &extension_file_util::UninstallExtension,
                install_directory_,
                extension_id)))
      NOTREACHED();
  }

  GURL launch_web_url_origin(extension->launch_web_url());
  launch_web_url_origin = launch_web_url_origin.GetOrigin();
  bool is_storage_isolated =
    (extension->is_storage_isolated() &&
    extension->HasAPIPermission(ExtensionAPIPermission::kExperimental));

  if (extension->is_hosted_app() &&
      !profile_->GetExtensionSpecialStoragePolicy()->
          IsStorageProtected(launch_web_url_origin)) {
    ExtensionDataDeleter::StartDeleting(
        profile_, extension_id, launch_web_url_origin, is_storage_isolated);
  }
  ExtensionDataDeleter::StartDeleting(
      profile_, extension_id, extension->url(), is_storage_isolated);

  UntrackTerminatedExtension(extension_id);

  // Notify interested parties that we've uninstalled this extension.
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_EXTENSION_UNINSTALLED,
      content::Source<Profile>(profile_),
      content::Details<const std::string>(&extension_id));

  if (sync_bundle && sync_bundle->HasExtensionId(extension_id)) {
    sync_bundle->sync_processor->ProcessSyncChanges(
        FROM_HERE, SyncChangeList(1, sync_change));
    sync_bundle->synced_extensions.erase(extension_id);
  }

  // Track the uninstallation.
  UMA_HISTOGRAM_ENUMERATION("Extensions.ExtensionUninstalled", 1, 2);

  static bool default_apps_trial_exists =
      base::FieldTrialList::TrialExists(kDefaultAppsTrialName);
  if (default_apps_trial_exists) {
    UMA_HISTOGRAM_ENUMERATION(
        base::FieldTrial::MakeName("Extensions.ExtensionUninstalled",
                                   kDefaultAppsTrialName),
        1, 2);
  }

  // Uninstalling one extension might have solved the problems of others.
  // Therefore, we clear warnings of this type for all extensions.
  std::set<ExtensionWarningSet::WarningType> warnings;
  extension_warnings_.GetWarningsAffectingExtension(extension_id, &warnings);
  extension_warnings_.ClearWarnings(warnings);

  return true;
}

bool ExtensionService::IsExtensionEnabled(
    const std::string& extension_id) const {
  if (extensions_.Contains(extension_id) ||
      terminated_extensions_.Contains(extension_id))
    return true;

  if (disabled_extensions_.Contains(extension_id))
    return false;

  // If the extension hasn't been loaded yet, check the prefs for it. Assume
  // enabled unless otherwise noted.
  return !extension_prefs_->IsExtensionDisabled(extension_id) &&
      !extension_prefs_->IsExternalExtensionUninstalled(extension_id);
}

bool ExtensionService::IsExternalExtensionUninstalled(
    const std::string& extension_id) const {
  return extension_prefs_->IsExternalExtensionUninstalled(extension_id);
}

void ExtensionService::EnableExtension(const std::string& extension_id) {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (IsExtensionEnabled(extension_id))
    return;

  extension_prefs_->SetExtensionState(extension_id, Extension::ENABLED);

  const Extension* extension =
      GetExtensionByIdInternal(extension_id, false, true, false);
  // This can happen if sync enables an extension that is not
  // installed yet.
  if (!extension)
    return;

  // Move it over to the enabled list.
  extensions_.Insert(make_scoped_refptr(extension));
  disabled_extensions_.Remove(extension->id());

  // Make sure any browser action contained within it is not hidden.
  extension_prefs_->SetBrowserActionVisibility(extension, true);

  NotifyExtensionLoaded(extension);

  SyncExtensionChangeIfNeeded(*extension);
}

void ExtensionService::DisableExtension(const std::string& extension_id) {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // The extension may have been disabled already.
  if (!IsExtensionEnabled(extension_id))
    return;

  const Extension* extension = GetInstalledExtension(extension_id);
  // |extension| can be NULL if sync disables an extension that is not
  // installed yet.
  if (extension && !Extension::UserMayDisable(extension->location()))
    return;

  extension_prefs_->SetExtensionState(extension_id, Extension::DISABLED);

  extension = GetExtensionByIdInternal(extension_id, true, false, true);
  if (!extension)
    return;

  // Move it over to the disabled list.
  disabled_extensions_.Insert(make_scoped_refptr(extension));
  if (extensions_.Contains(extension->id()))
    extensions_.Remove(extension->id());
  else
    terminated_extensions_.Remove(extension->id());

  NotifyExtensionUnloaded(extension, extension_misc::UNLOAD_REASON_DISABLE);

  SyncExtensionChangeIfNeeded(*extension);

  // Deactivating one extension might have solved the problems of others.
  // Therefore, we clear warnings of this type for all extensions.
  std::set<ExtensionWarningSet::WarningType> warnings;
  extension_warnings_.GetWarningsAffectingExtension(extension_id, &warnings);
  extension_warnings_.ClearWarnings(warnings);
}

void ExtensionService::GrantPermissionsAndEnableExtension(
    const Extension* extension) {
  CHECK(extension);
  RecordPermissionMessagesHistogram(
      extension, "Extensions.Permissions_ReEnable");
  extensions::PermissionsUpdater perms_updater(profile());
  perms_updater.GrantActivePermissions(extension);
  extension_prefs_->SetDidExtensionEscalatePermissions(extension, false);
  EnableExtension(extension->id());
}

// static
void ExtensionService::RecordPermissionMessagesHistogram(
    const Extension* e, const char* histogram) {
  // Since this is called from multiple sources, and since the Histogram macros
  // use statics, we need to manually lookup the Histogram ourselves.
  base::Histogram* counter = base::LinearHistogram::FactoryGet(
      histogram,
      1,
      ExtensionPermissionMessage::kEnumBoundary,
      ExtensionPermissionMessage::kEnumBoundary + 1,
      base::Histogram::kUmaTargetedHistogramFlag);

  ExtensionPermissionMessages permissions = e->GetPermissionMessages();
  if (permissions.empty()) {
    counter->Add(ExtensionPermissionMessage::kNone);
  } else {
    for (ExtensionPermissionMessages::iterator it = permissions.begin();
         it != permissions.end(); ++it)
      counter->Add(it->id());
  }
}

void ExtensionService::NotifyExtensionLoaded(const Extension* extension) {
  // The ChromeURLRequestContexts need to be first to know that the extension
  // was loaded, otherwise a race can arise where a renderer that is created
  // for the extension may try to load an extension URL with an extension id
  // that the request context doesn't yet know about. The profile is responsible
  // for ensuring its URLRequestContexts appropriately discover the loaded
  // extension.
  profile_->RegisterExtensionWithRequestContexts(extension);

  // Tell renderers about the new extension, unless it's a theme (renderers
  // don't need to know about themes).
  if (!extension->is_theme()) {
    for (content::RenderProcessHost::iterator i(
            content::RenderProcessHost::AllHostsIterator());
         !i.IsAtEnd(); i.Advance()) {
      content::RenderProcessHost* host = i.GetCurrentValue();
      Profile* host_profile =
          Profile::FromBrowserContext(host->GetBrowserContext());
      if (host_profile->GetOriginalProfile() ==
          profile_->GetOriginalProfile()) {
        std::vector<ExtensionMsg_Loaded_Params> loaded_extensions(
            1, ExtensionMsg_Loaded_Params(extension));
        host->Send(
            new ExtensionMsg_Loaded(loaded_extensions));
      }
    }
  }

  // Tell subsystems that use the EXTENSION_LOADED notification about the new
  // extension.
  //
  // NOTE: It is important that this happen after notifying the renderers about
  // the new extensions so that if we navigate to an extension URL in
  // NOTIFICATION_EXTENSION_LOADED, the renderer is guaranteed to know about it.
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_EXTENSION_LOADED,
      content::Source<Profile>(profile_),
      content::Details<const Extension>(extension));

  // Tell a random-ass collection of other subsystems about the new extension.
  // TODO(aa): What should we do with all this goop? Can it move into the
  // relevant objects via EXTENSION_LOADED?

  profile_->GetExtensionSpecialStoragePolicy()->
      GrantRightsForExtension(extension);

  UpdateActiveExtensionsInCrashReporter();

  ExtensionWebUI::RegisterChromeURLOverrides(
      profile_, extension->GetChromeURLOverrides());

  TemplateURLService* url_service =
      TemplateURLServiceFactory::GetForProfile(profile_);
  if (url_service)
    url_service->RegisterExtensionKeyword(extension);

  // Load the icon for omnibox-enabled extensions so it will be ready to display
  // in the URL bar.
  if (!extension->omnibox_keyword().empty()) {
    omnibox_popup_icon_manager_.LoadIcon(extension);
    omnibox_icon_manager_.LoadIcon(extension);
  }

  // If the extension has permission to load chrome://favicon/ resources we need
  // to make sure that the FaviconSource is registered with the
  // ChromeURLDataManager.
  if (extension->HasHostPermission(GURL(chrome::kChromeUIFaviconURL))) {
    FaviconSource* favicon_source = new FaviconSource(profile_,
                                                      FaviconSource::FAVICON);
    profile_->GetChromeURLDataManager()->AddDataSource(favicon_source);
  }
  // Same for chrome://thumb/ resources.
  if (extension->HasHostPermission(GURL(chrome::kChromeUIThumbnailURL))) {
    ThumbnailSource* thumbnail_source = new ThumbnailSource(profile_);
    profile_->GetChromeURLDataManager()->AddDataSource(thumbnail_source);
  }

  // TODO(mpcomplete): This ends up affecting all profiles. See crbug.com/80757.
  bool plugins_changed = false;
  for (size_t i = 0; i < extension->plugins().size(); ++i) {
    const Extension::PluginInfo& plugin = extension->plugins()[i];
    PluginService::GetInstance()->RefreshPlugins();
    PluginService::GetInstance()->AddExtraPluginPath(plugin.path);
    plugins_changed = true;
    ChromePluginServiceFilter* filter =
        ChromePluginServiceFilter::GetInstance();
    if (plugin.is_public) {
      filter->RestrictPluginToProfileAndOrigin(
          plugin.path, profile_, GURL());
    } else {
      filter->RestrictPluginToProfileAndOrigin(
          plugin.path, profile_, extension->url());
    }
  }

  bool nacl_modules_changed = false;
  for (size_t i = 0; i < extension->nacl_modules().size(); ++i) {
    const Extension::NaClModuleInfo& module = extension->nacl_modules()[i];
    RegisterNaClModule(module.url, module.mime_type);
    nacl_modules_changed = true;
  }

  if (nacl_modules_changed)
    UpdatePluginListWithNaClModules();

  if (plugins_changed || nacl_modules_changed)
    PluginService::GetInstance()->PurgePluginListCache(profile_, false);

#if defined(OS_CHROMEOS)
  for (std::vector<Extension::InputComponentInfo>::const_iterator component =
           extension->input_components().begin();
       component != extension->input_components().end();
       ++component) {
    if (component->type == Extension::INPUT_COMPONENT_TYPE_IME) {
      ExtensionInputImeEventRouter::GetInstance()->RegisterIme(
          profile_, extension->id(), *component);
    }
#if defined(USE_VIRTUAL_KEYBOARD)
    if (component->type == Extension::INPUT_COMPONENT_TYPE_VIRTUAL_KEYBOARD &&
        !component->layouts.empty()) {
      chromeos::input_method::InputMethodManager* input_method_manager =
          chromeos::input_method::InputMethodManager::GetInstance();
      const bool is_system_keyboard =
          extension->location() == Extension::COMPONENT;
      input_method_manager->RegisterVirtualKeyboard(
          extension->url(),
          component->name,  // human-readable name of the keyboard extension.
          component->layouts,
          is_system_keyboard);
    }
#endif
  }
#endif
}

void ExtensionService::NotifyExtensionUnloaded(
    const Extension* extension,
    extension_misc::UnloadedExtensionReason reason) {
  UnloadedExtensionInfo details(extension, reason);
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_EXTENSION_UNLOADED,
      content::Source<Profile>(profile_),
      content::Details<UnloadedExtensionInfo>(&details));

  for (content::RenderProcessHost::iterator i(
          content::RenderProcessHost::AllHostsIterator());
       !i.IsAtEnd(); i.Advance()) {
    content::RenderProcessHost* host = i.GetCurrentValue();
    Profile* host_profile =
        Profile::FromBrowserContext(host->GetBrowserContext());
    if (host_profile->GetOriginalProfile() == profile_->GetOriginalProfile())
      host->Send(new ExtensionMsg_Unloaded(extension->id()));
  }

  profile_->UnregisterExtensionWithRequestContexts(extension->id(), reason);
  profile_->GetExtensionSpecialStoragePolicy()->
      RevokeRightsForExtension(extension);

  ExtensionWebUI::UnregisterChromeURLOverrides(
      profile_, extension->GetChromeURLOverrides());

#if defined(OS_CHROMEOS)
    // Revoke external file access to
  if (profile_->GetFileSystemContext() &&
      profile_->GetFileSystemContext()->external_provider()) {
    profile_->GetFileSystemContext()->external_provider()->
        RevokeAccessForExtension(extension->id());
  }

  if (extension->input_components().size() > 0) {
    ExtensionInputImeEventRouter::GetInstance()->UnregisterAllImes(
        profile_, extension->id());
  }
#endif

  UpdateActiveExtensionsInCrashReporter();

  bool plugins_changed = false;
  for (size_t i = 0; i < extension->plugins().size(); ++i) {
    const Extension::PluginInfo& plugin = extension->plugins()[i];
    if (!BrowserThread::PostTask(BrowserThread::IO, FROM_HERE,
                                 base::Bind(&ForceShutdownPlugin, plugin.path)))
      NOTREACHED();
    PluginService::GetInstance()->RefreshPlugins();
    PluginService::GetInstance()->RemoveExtraPluginPath(plugin.path);
    plugins_changed = true;
    ChromePluginServiceFilter::GetInstance()->UnrestrictPlugin(plugin.path);
  }

  bool nacl_modules_changed = false;
  for (size_t i = 0; i < extension->nacl_modules().size(); ++i) {
    const Extension::NaClModuleInfo& module = extension->nacl_modules()[i];
    UnregisterNaClModule(module.url);
    nacl_modules_changed = true;
  }

  if (nacl_modules_changed)
    UpdatePluginListWithNaClModules();

  if (plugins_changed || nacl_modules_changed)
    PluginService::GetInstance()->PurgePluginListCache(profile_, false);
}

void ExtensionService::UpdateExtensionBlacklist(
  const std::vector<std::string>& blacklist) {
  // Use this set to indicate if an extension in the blacklist has been used.
  std::set<std::string> blacklist_set;
  for (unsigned int i = 0; i < blacklist.size(); ++i) {
    if (Extension::IdIsValid(blacklist[i])) {
      blacklist_set.insert(blacklist[i]);
    }
  }
  extension_prefs_->UpdateBlacklist(blacklist_set);
  std::vector<std::string> to_be_removed;
  // Loop current extensions, unload installed extensions.
  for (ExtensionSet::const_iterator iter = extensions_.begin();
       iter != extensions_.end(); ++iter) {
    const Extension* extension = (*iter);
    if (blacklist_set.find(extension->id()) != blacklist_set.end()) {
      to_be_removed.push_back(extension->id());
    }
  }

  // UnloadExtension will change the extensions_ list. So, we should
  // call it outside the iterator loop.
  for (unsigned int i = 0; i < to_be_removed.size(); ++i) {
    UnloadExtension(to_be_removed[i], extension_misc::UNLOAD_REASON_DISABLE);
  }
}

Profile* ExtensionService::profile() {
  return profile_;
}

ExtensionPrefs* ExtensionService::extension_prefs() {
  return extension_prefs_;
}

extensions::SettingsFrontend* ExtensionService::settings_frontend() {
  return settings_frontend_.get();
}

ExtensionContentSettingsStore*
    ExtensionService::GetExtensionContentSettingsStore() {
  return extension_prefs()->content_settings_store();
}

bool ExtensionService::is_ready() {
  return ready_;
}

ExtensionUpdater* ExtensionService::updater() {
  return updater_.get();
}

void ExtensionService::CheckAdminBlacklist() {
  std::vector<std::string> to_be_removed;
  // Loop through extensions list, unload installed extensions.
  for (ExtensionSet::const_iterator iter = extensions_.begin();
       iter != extensions_.end(); ++iter) {
    const Extension* extension = (*iter);
    if (!extension_prefs_->IsExtensionAllowedByPolicy(extension->id(),
                                                      extension->location())) {
      to_be_removed.push_back(extension->id());
    }
  }

  // UnloadExtension will change the extensions_ list. So, we should
  // call it outside the iterator loop.
  for (unsigned int i = 0; i < to_be_removed.size(); ++i)
    UnloadExtension(to_be_removed[i], extension_misc::UNLOAD_REASON_DISABLE);
}

void ExtensionService::CheckForUpdatesSoon() {
  if (updater()) {
    updater()->CheckSoon();
  } else {
    LOG(WARNING) << "CheckForUpdatesSoon() called with auto-update turned off";
  }
}

namespace {
  bool IsSyncableNone(const Extension& extension) { return false; }
}  // namespace

ExtensionService::SyncBundle::SyncBundle()
  : filter(IsSyncableNone),
    sync_processor(NULL) {
}

ExtensionService::SyncBundle::~SyncBundle() {
}

bool ExtensionService::SyncBundle::HasExtensionId(const std::string& id) const {
  return synced_extensions.find(id) != synced_extensions.end();
}

bool ExtensionService::SyncBundle::HasPendingExtensionId(const std::string& id)
    const {
  return pending_sync_data.find(id) != pending_sync_data.end();
}

void ExtensionService::SyncExtensionChangeIfNeeded(const Extension& extension) {
  SyncBundle* sync_bundle = GetSyncBundleForExtension(extension);
  if (sync_bundle) {
    ExtensionSyncData extension_sync_data(
        extension,
        IsExtensionEnabled(extension.id()),
        IsIncognitoEnabled(extension.id()),
        extension_prefs_->GetAppNotificationClientId(extension.id()),
        extension_prefs_->IsAppNotificationDisabled(extension.id()),
        GetAppLaunchOrdinal(extension.id()),
        GetPageOrdinal(extension.id()));

    SyncChangeList sync_change_list(1, extension_sync_data.GetSyncChange(
        sync_bundle->HasExtensionId(extension.id()) ?
            SyncChange::ACTION_UPDATE : SyncChange::ACTION_ADD));
    sync_bundle->sync_processor->ProcessSyncChanges(
        FROM_HERE, sync_change_list);
    sync_bundle->synced_extensions.insert(extension.id());
    sync_bundle->pending_sync_data.erase(extension.id());
  }
}

ExtensionService::SyncBundle* ExtensionService::GetSyncBundleForExtension(
    const Extension& extension) {
  if (app_sync_bundle_.filter(extension))
    return &app_sync_bundle_;
  else if (extension_sync_bundle_.filter(extension))
    return &extension_sync_bundle_;
  else
    return NULL;
}

ExtensionService::SyncBundle*
    ExtensionService::GetSyncBundleForExtensionSyncData(
    const ExtensionSyncData& extension_sync_data) {
  switch (extension_sync_data.type()) {
    case Extension::SYNC_TYPE_APP:
      return &app_sync_bundle_;
    case Extension::SYNC_TYPE_EXTENSION:
      return &extension_sync_bundle_;
    default:
      NOTREACHED();
      return NULL;
  }
}

#define GET_SYNC_BUNDLE_FOR_MODEL_TYPE_BODY() \
  do { \
    switch (type) { \
      case syncable::APPS: \
        return &app_sync_bundle_; \
      case syncable::EXTENSIONS: \
        return &extension_sync_bundle_; \
      default: \
        NOTREACHED(); \
        return NULL; \
    } \
  } while (0)

const ExtensionService::SyncBundle*
    ExtensionService::GetSyncBundleForModelTypeConst(
    syncable::ModelType type) const {
  GET_SYNC_BUNDLE_FOR_MODEL_TYPE_BODY();
}

ExtensionService::SyncBundle* ExtensionService::GetSyncBundleForModelType(
    syncable::ModelType type) {
  GET_SYNC_BUNDLE_FOR_MODEL_TYPE_BODY();
}

#undef GET_SYNC_BUNDLE_FOR_MODEL_TYPE_BODY

SyncError ExtensionService::MergeDataAndStartSyncing(
    syncable::ModelType type,
    const SyncDataList& initial_sync_data,
    SyncChangeProcessor* sync_processor) {
  CHECK(sync_processor);

  SyncBundle* bundle = NULL;

  switch (type) {
    case syncable::EXTENSIONS:
      bundle = &extension_sync_bundle_;
      bundle->filter = IsSyncableExtension;
      break;

    case syncable::APPS:
      bundle = &app_sync_bundle_;
      bundle->filter = IsSyncableApp;
      break;

    default:
      LOG(FATAL) << "Got " << type << " ModelType";
  }

  bundle->sync_processor = sync_processor;

  // Process extensions from sync.
  for (SyncDataList::const_iterator i = initial_sync_data.begin();
       i != initial_sync_data.end();
       ++i) {
    ExtensionSyncData extension_sync_data = ExtensionSyncData(*i);
    bundle->synced_extensions.insert(extension_sync_data.id());
    ProcessExtensionSyncData(extension_sync_data, *bundle);
  }

  // Process local extensions.
  // TODO(yoz): Determine whether pending extensions should be considered too.
  //            See crbug.com/104399.
  SyncDataList sync_data_list = GetAllSyncData(type);
  SyncChangeList sync_change_list;
  for (SyncDataList::const_iterator i = sync_data_list.begin();
       i != sync_data_list.end();
       ++i) {
    if (bundle->HasExtensionId(i->GetTag())) {
      sync_change_list.push_back(SyncChange(SyncChange::ACTION_UPDATE, *i));
    } else {
      bundle->synced_extensions.insert(i->GetTag());
      sync_change_list.push_back(SyncChange(SyncChange::ACTION_ADD, *i));
    }
  }
  bundle->sync_processor->ProcessSyncChanges(FROM_HERE, sync_change_list);

  extension_prefs_->extension_sorting()->FixNTPOrdinalCollisions();

  return SyncError();
}

void ExtensionService::StopSyncing(syncable::ModelType type) {
  SyncBundle* bundle = GetSyncBundleForModelType(type);
  CHECK(bundle);
  // This is the simplest way to clear out the bundle.
  *bundle = SyncBundle();
}

SyncDataList ExtensionService::GetAllSyncData(syncable::ModelType type) const {
  const SyncBundle* bundle = GetSyncBundleForModelTypeConst(type);
  CHECK(bundle);
  std::vector<ExtensionSyncData> extension_sync_data = GetSyncDataList(*bundle);
  SyncDataList result(extension_sync_data.size());
  for (int i = 0; i < static_cast<int>(extension_sync_data.size()); ++i) {
    result[i] = extension_sync_data[i].GetSyncData();
  }
  return result;
}

SyncError ExtensionService::ProcessSyncChanges(
    const tracked_objects::Location& from_here,
    const SyncChangeList& change_list) {
  for (SyncChangeList::const_iterator i = change_list.begin();
      i != change_list.end();
      ++i) {
    ExtensionSyncData extension_sync_data = ExtensionSyncData(*i);
    SyncBundle* bundle = GetSyncBundleForExtensionSyncData(extension_sync_data);
    CHECK(bundle);

    if (extension_sync_data.uninstalled())
      bundle->synced_extensions.erase(extension_sync_data.id());
    else
      bundle->synced_extensions.insert(extension_sync_data.id());
    ProcessExtensionSyncData(extension_sync_data, *bundle);
  }

  extension_prefs()->extension_sorting()->FixNTPOrdinalCollisions();

  return SyncError();
}

void ExtensionService::GetSyncDataListHelper(
    const ExtensionSet& extensions,
    const SyncBundle& bundle,
    std::vector<ExtensionSyncData>* sync_data_list) const {
  for (ExtensionSet::const_iterator it = extensions.begin();
       it != extensions.end(); ++it) {
    const Extension& extension = **it;
    if (bundle.filter(extension) &&
        // If we have pending extension data for this extension, then this
        // version is out of date.  We'll sync back the version we got from
        // sync.
        !bundle.HasPendingExtensionId(extension.id())) {
      sync_data_list->push_back(ExtensionSyncData(
          extension,
          IsExtensionEnabled(extension.id()),
          IsIncognitoEnabled(extension.id()),
          extension_prefs_->GetAppNotificationClientId(extension.id()),
          extension_prefs_->IsAppNotificationDisabled(extension.id()),
          GetAppLaunchOrdinal(extension.id()),
          GetPageOrdinal(extension.id())));
    }
  }
}

std::vector<ExtensionSyncData> ExtensionService::GetSyncDataList(
    const SyncBundle& bundle) const {
  std::vector<ExtensionSyncData> extension_sync_list;
  GetSyncDataListHelper(extensions_, bundle, &extension_sync_list);
  GetSyncDataListHelper(disabled_extensions_, bundle, &extension_sync_list);
  GetSyncDataListHelper(terminated_extensions_, bundle, &extension_sync_list);

  for (std::map<std::string, ExtensionSyncData>::const_iterator i =
           bundle.pending_sync_data.begin();
       i != bundle.pending_sync_data.end();
       ++i) {
    extension_sync_list.push_back(i->second);
  }

  return extension_sync_list;
}

void ExtensionService::ProcessExtensionSyncData(
    const ExtensionSyncData& extension_sync_data,
    SyncBundle& bundle) {
  const std::string& id = extension_sync_data.id();
  const Extension* extension = GetInstalledExtension(id);

  // TODO(bolms): we should really handle this better.  The particularly bad
  // case is where an app becomes an extension or vice versa, and we end up with
  // a zombie extension that won't go away.
  if (extension && !bundle.filter(*extension))
    return;

  // Handle uninstalls first.
  if (extension_sync_data.uninstalled()) {
    std::string error;
    if (!UninstallExtensionHelper(this, id)) {
      LOG(WARNING) << "Could not uninstall extension " << id
                   << " for sync";
    }
    return;
  }

  // Set user settings.
  if (extension_sync_data.enabled()) {
    EnableExtension(id);
  } else {
    DisableExtension(id);
  }

  // We need to cache some version information here because setting the
  // incognito flag invalidates the |extension| pointer (it reloads the
  // extension).
  bool extension_installed = (extension != NULL);
  int result = extension ?
      extension->version()->CompareTo(extension_sync_data.version()) : 0;
  SetIsIncognitoEnabled(id, extension_sync_data.incognito_enabled());
  extension = NULL;  // No longer safe to use.

  if (extension_sync_data.app_launch_ordinal().IsValid() &&
      extension_sync_data.page_ordinal().IsValid()) {
    SetAppLaunchOrdinal(id, extension_sync_data.app_launch_ordinal());
    SetPageOrdinal(id, extension_sync_data.page_ordinal());
  }

  if (extension_installed) {
    // If the extension is already installed, check if it's outdated.
    if (result < 0) {
      // Extension is outdated.
      bundle.pending_sync_data[extension_sync_data.id()] = extension_sync_data;
      CheckForUpdatesSoon();
    }
    if (extension_sync_data.type() == Extension::SYNC_TYPE_APP &&
        extension_sync_data.notifications_disabled() !=
        extension_prefs_->IsAppNotificationDisabled(id)) {
      extension_prefs_->SetAppNotificationDisabled(
          id, extension_sync_data.notifications_disabled());
    }
  } else {
    // TODO(akalin): Replace silent update with a list of enabled
    // permissions.
    const bool kInstallSilently = true;
    if (!pending_extension_manager()->AddFromSync(
            id,
            extension_sync_data.update_url(),
            bundle.filter,
            kInstallSilently)) {
      LOG(WARNING) << "Could not add pending extension for " << id;
      // This means that the extension is already pending installation, with a
      // non-INTERNAL location.  Add to pending_sync_data, even though it will
      // never be removed (we'll never install a syncable version of the
      // extension), so that GetAllSyncData() continues to send it.
    }
    // Track pending extensions so that we can return them in GetAllSyncData().
    bundle.pending_sync_data[extension_sync_data.id()] = extension_sync_data;
    CheckForUpdatesSoon();
  }
}

bool ExtensionService::IsIncognitoEnabled(
    const std::string& extension_id) const {
  // If this is an existing component extension we always allow it to
  // work in incognito mode.
  const Extension* extension = GetInstalledExtension(extension_id);
  if (extension && extension->location() == Extension::COMPONENT)
    return true;

  // Check the prefs.
  return extension_prefs_->IsIncognitoEnabled(extension_id);
}

void ExtensionService::SetIsIncognitoEnabled(
    const std::string& extension_id, bool enabled) {
  const Extension* extension = GetInstalledExtension(extension_id);
  if (extension && extension->location() == Extension::COMPONENT) {
    // This shouldn't be called for component extensions unless they are
    // syncable.
    DCHECK(extension->IsSyncable());

    // If we are here, make sure the we aren't trying to change the value.
    DCHECK_EQ(enabled, IsIncognitoEnabled(extension_id));

    return;
  }

  // Broadcast unloaded and loaded events to update browser state. Only bother
  // if the value changed and the extension is actually enabled, since there is
  // no UI otherwise.
  bool old_enabled = extension_prefs_->IsIncognitoEnabled(extension_id);
  if (enabled == old_enabled)
    return;

  extension_prefs_->SetIsIncognitoEnabled(extension_id, enabled);

  bool extension_is_enabled = extensions_.Contains(extension_id);

  // When we reload the extension the ID may be invalidated if we've passed it
  // by const ref everywhere. Make a copy to be safe.
  std::string id = extension_id;
  if (extension_is_enabled)
    ReloadExtension(id);

  // Reloading the extension invalidates the |extension| pointer.
  extension = GetInstalledExtension(id);
  if (extension)
    SyncExtensionChangeIfNeeded(*extension);
}

void ExtensionService::SetAppNotificationSetupDone(
    const std::string& extension_id,
    const std::string& oauth_client_id) {
  const Extension* extension = GetInstalledExtension(extension_id);
  // This method is called when the user sets up app notifications.
  // So it is not expected to be called until the extension is installed.
  if (!extension) {
    NOTREACHED();
    return;
  }
  extension_prefs_->SetAppNotificationClientId(extension_id, oauth_client_id);
  SyncExtensionChangeIfNeeded(*extension);
}

void ExtensionService::SetAppNotificationDisabled(
    const std::string& extension_id,
    bool value) {
  const Extension* extension = GetInstalledExtension(extension_id);
  // This method is called when the user enables/disables app notifications.
  // So it is not expected to be called until the extension is installed.
  if (!extension) {
    NOTREACHED();
    return;
  }
  if (value)
    UMA_HISTOGRAM_COUNTS("Apps.SetAppNotificationsDisabled", 1);
  else
    UMA_HISTOGRAM_COUNTS("Apps.SetAppNotificationsEnabled", 1);
  extension_prefs_->SetAppNotificationDisabled(extension_id, value);
  SyncExtensionChangeIfNeeded(*extension);
}

bool ExtensionService::CanCrossIncognito(const Extension* extension) {
  // We allow the extension to see events and data from another profile iff it
  // uses "spanning" behavior and it has incognito access. "split" mode
  // extensions only see events for a matching profile.
  CHECK(extension);
  return IsIncognitoEnabled(extension->id()) &&
      !extension->incognito_split_mode();
}

bool ExtensionService::CanLoadInIncognito(const Extension* extension) const {
  if (extension->is_hosted_app())
    return true;
  // Packaged apps and regular extensions need to be enabled specifically for
  // incognito (and split mode should be set).
  return extension->incognito_split_mode() &&
         IsIncognitoEnabled(extension->id());
}

StringOrdinal ExtensionService::GetAppLaunchOrdinal(
    const std::string& extension_id) const {
  return
      extension_prefs_->extension_sorting()->GetAppLaunchOrdinal(extension_id);
}

void ExtensionService::SetAppLaunchOrdinal(
    const std::string& extension_id,
    const StringOrdinal& app_launch_ordinal) {
  // Only apps should set this value, so we check that it is either an app or
  // that it is not yet installed (so we can't be sure it is an app). It is
  // possible to be setting this value through syncing before the app is
  // installed.
  const Extension* ext = GetExtensionById(extension_id, true);
  DCHECK(!ext || ext->is_app());

  extension_prefs_->extension_sorting()->SetAppLaunchOrdinal(
      extension_id, app_launch_ordinal);

  const Extension* extension = GetInstalledExtension(extension_id);
  if (extension)
    SyncExtensionChangeIfNeeded(*extension);
}

StringOrdinal ExtensionService::GetPageOrdinal(
    const std::string& extension_id) const {
  return extension_prefs_->extension_sorting()->GetPageOrdinal(extension_id);
}

void ExtensionService::SetPageOrdinal(const std::string& extension_id,
                                      const StringOrdinal& page_ordinal) {
  // Only apps should set this value, so we check that it is either an app or
  // that it is not yet installed (so we can't be sure it is an app). It is
  // possible to be setting this value through syncing before the app is
  // installed.
  const Extension* ext = GetExtensionById(extension_id, true);
  DCHECK(!ext || ext->is_app());

  extension_prefs_->extension_sorting()->SetPageOrdinal(
      extension_id, page_ordinal);

  const Extension* extension = GetInstalledExtension(extension_id);
  if (extension)
    SyncExtensionChangeIfNeeded(*extension);
}

void ExtensionService::OnExtensionMoved(
    const std::string& moved_extension_id,
    const std::string& predecessor_extension_id,
    const std::string& successor_extension_id) {
  extension_prefs_->extension_sorting()->OnExtensionMoved(
      moved_extension_id,
      predecessor_extension_id,
      successor_extension_id);

  const Extension* extension = GetInstalledExtension(moved_extension_id);
  if (extension)
    SyncExtensionChangeIfNeeded(*extension);
}

bool ExtensionService::AllowFileAccess(const Extension* extension) {
  return (CommandLine::ForCurrentProcess()->HasSwitch(
              switches::kDisableExtensionsFileAccessCheck) ||
          extension_prefs_->AllowFileAccess(extension->id()));
}

void ExtensionService::SetAllowFileAccess(const Extension* extension,
                                          bool allow) {
  // Reload to update browser state. Only bother if the value changed and the
  // extension is actually enabled, since there is no UI otherwise.
  bool old_allow = AllowFileAccess(extension);
  if (allow == old_allow)
    return;

  extension_prefs_->SetAllowFileAccess(extension->id(), allow);

  bool extension_is_enabled = extensions_.Contains(extension->id());
  if (extension_is_enabled)
    ReloadExtension(extension->id());
}

bool ExtensionService::GetBrowserActionVisibility(const Extension* extension) {
  return extension_prefs_->GetBrowserActionVisibility(extension);
}

void ExtensionService::SetBrowserActionVisibility(const Extension* extension,
                                                  bool visible) {
  extension_prefs_->SetBrowserActionVisibility(extension, visible);
}

// Some extensions will autoupdate themselves externally from Chrome.  These
// are typically part of some larger client application package.  To support
// these, the extension will register its location in the the preferences file
// (and also, on Windows, in the registry) and this code will periodically
// check that location for a .crx file, which it will then install locally if
// a new version is available.
// Errors are reported through ExtensionErrorReporter. Succcess is not
// reported.
void ExtensionService::CheckForExternalUpdates() {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // Note that this installation is intentionally silent (since it didn't
  // go through the front-end).  Extensions that are registered in this
  // way are effectively considered 'pre-bundled', and so implicitly
  // trusted.  In general, if something has HKLM or filesystem access,
  // they could install an extension manually themselves anyway.

  // If any external extension records give a URL, a provider will set
  // this to true.  Used by OnExternalProviderReady() to see if we need
  // to start an update check to fetch a new external extension.
  external_extension_url_added_ = false;

  // Ask each external extension provider to give us a call back for each
  // extension they know about. See OnExternalExtension(File|UpdateUrl)Found.
  ProviderCollection::const_iterator i;
  for (i = external_extension_providers_.begin();
       i != external_extension_providers_.end(); ++i) {
    ExternalExtensionProviderInterface* provider = i->get();
    provider->VisitRegisteredExtension();
  }

  // Do any required work that we would have done after completion of all
  // providers.
  if (external_extension_providers_.empty()) {
    OnAllExternalProvidersReady();
  }
}

void ExtensionService::OnExternalProviderReady(
    const ExternalExtensionProviderInterface* provider) {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  CHECK(provider->IsReady());

  // An external provider has finished loading.  We only take action
  // if all of them are finished. So we check them first.
  ProviderCollection::const_iterator i;
  for (i = external_extension_providers_.begin();
       i != external_extension_providers_.end(); ++i) {
    ExternalExtensionProviderInterface* provider = i->get();
    if (!provider->IsReady())
      return;
  }

  OnAllExternalProvidersReady();
}

void ExtensionService::OnAllExternalProvidersReady() {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // Install any pending extensions.
  if (external_extension_url_added_ && updater()) {
    external_extension_url_added_ = false;
    updater()->CheckNow();
  }

  // Uninstall all the unclaimed extensions.
  scoped_ptr<ExtensionPrefs::ExtensionsInfo> extensions_info(
      extension_prefs_->GetInstalledExtensionsInfo());
  for (size_t i = 0; i < extensions_info->size(); ++i) {
    ExtensionInfo* info = extensions_info->at(i).get();
    if (Extension::IsExternalLocation(info->extension_location))
      CheckExternalUninstall(info->extension_id);
  }
  IdentifyAlertableExtensions();
}

void ExtensionService::IdentifyAlertableExtensions() {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (!CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableExtensionAlerts)) {
    return;  // TODO(miket): enable unconditionally when done.
  }

  // Build up the lists of extensions that require acknowledgment.
  // If this is the first time, grandfather extensions that would have
  // caused notification.
  extension_global_error_.reset(new ExtensionGlobalError(this));
  bool needs_alert = false;
  for (ExtensionSet::const_iterator iter = extensions_.begin();
       iter != extensions_.end(); ++iter) {
    const Extension* e = *iter;
    if (Extension::IsExternalLocation(e->location())) {
      if (!extension_prefs_->IsExternalExtensionAcknowledged(e->id())) {
        extension_global_error_->AddExternalExtension(e->id());
        needs_alert = true;
      }
    }
    if (extension_prefs_->IsExtensionBlacklisted(e->id())) {
      if (!extension_prefs_->IsBlacklistedExtensionAcknowledged(e->id())) {
        extension_global_error_->AddBlacklistedExtension(e->id());
        needs_alert = true;
      }
    }
    if (extension_prefs_->IsExtensionOrphaned(e->id())) {
      if (!extension_prefs_->IsOrphanedExtensionAcknowledged(e->id())) {
        extension_global_error_->AddOrphanedExtension(e->id());
        needs_alert = true;
      }
    }
  }

  bool did_show_alert = false;
  if (needs_alert) {
    if (extension_prefs_->SetAlertSystemFirstRun()) {
      CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
      Browser* browser = BrowserList::GetLastActiveWithProfile(profile_);
      if (browser) {
        extension_global_error_->ShowBubbleView(browser);
        did_show_alert = true;
      }
    } else {
      // First run. Just acknowledge all the extensions, silently, by
      // shortcutting the display of the UI and going straight to the
      // callback for pressing the Accept button.
      HandleExtensionAlertAccept();
    }
  }

  if (!did_show_alert)
    extension_global_error_.reset();
}

void ExtensionService::HandleExtensionAlertClosed() {
  extension_global_error_.reset();
}

void ExtensionService::HandleExtensionAlertAccept() {
  const ExtensionIdSet *extension_ids =
      extension_global_error_->get_external_extension_ids();
  for (ExtensionIdSet::const_iterator iter = extension_ids->begin();
       iter != extension_ids->end(); ++iter) {
    AcknowledgeExternalExtension(*iter);
  }
  extension_ids = extension_global_error_->get_blacklisted_extension_ids();
  for (ExtensionIdSet::const_iterator iter = extension_ids->begin();
       iter != extension_ids->end(); ++iter) {
    extension_prefs_->AcknowledgeBlacklistedExtension(*iter);
  }
  extension_ids = extension_global_error_->get_orphaned_extension_ids();
  for (ExtensionIdSet::const_iterator iter = extension_ids->begin();
       iter != extension_ids->end(); ++iter) {
    extension_prefs_->AcknowledgeOrphanedExtension(*iter);
  }
}

void ExtensionService::AcknowledgeExternalExtension(const std::string& id) {
  extension_prefs_->AcknowledgeExternalExtension(id);
}

void ExtensionService::HandleExtensionAlertDetails(Browser* browser) {
  DCHECK(browser);
  browser->ShowExtensionsTab();
}

void ExtensionService::UnloadExtension(
    const std::string& extension_id,
    extension_misc::UnloadedExtensionReason reason) {
  // Make sure the extension gets deleted after we return from this function.
  scoped_refptr<const Extension> extension(
      GetExtensionByIdInternal(extension_id, true, true, false));

  // This method can be called via PostTask, so the extension may have been
  // unloaded by the time this runs.
  if (!extension) {
    // In case the extension may have crashed/uninstalled. Allow the profile to
    // clean up its RequestContexts.
    profile_->UnregisterExtensionWithRequestContexts(extension_id, reason);
    return;
  }

  // Keep information about the extension so that we can reload it later
  // even if it's not permanently installed.
  unloaded_extension_paths_[extension->id()] = extension->path();

  // Clean up if the extension is meant to be enabled after a reload.
  disabled_extension_paths_.erase(extension->id());

  // Clean up runtime data.
  extension_runtime_data_.erase(extension_id);

if (disabled_extensions_.Contains(extension->id())) {
    UnloadedExtensionInfo details(extension, reason);
    details.already_disabled = true;
    disabled_extensions_.Remove(extension->id());
    content::NotificationService::current()->Notify(
        chrome::NOTIFICATION_EXTENSION_UNLOADED,
        content::Source<Profile>(profile_),
        content::Details<UnloadedExtensionInfo>(&details));
    // Make sure the profile cleans up its RequestContexts when an already
    // disabled extension is unloaded (since they are also tracking the disabled
    // extensions).
    profile_->UnregisterExtensionWithRequestContexts(extension_id, reason);
    return;
  }

// Remove the extension from our list.
  extensions_.Remove(extension->id());

  NotifyExtensionUnloaded(extension.get(), reason);
}

void ExtensionService::UnloadAllExtensions() {
  profile_->GetExtensionSpecialStoragePolicy()->
      RevokeRightsForAllExtensions();

  extensions_.Clear();
  disabled_extensions_.Clear();
  terminated_extensions_.Clear();
  extension_runtime_data_.clear();

  // TODO(erikkay) should there be a notification for this?  We can't use
  // EXTENSION_UNLOADED since that implies that the extension has been disabled
  // or uninstalled, and UnloadAll is just part of shutdown.
}

void ExtensionService::ReloadExtensions() {
  UnloadAllExtensions();
  component_loader_->LoadAll();
  extensions::InstalledLoader(this).LoadAllExtensions();
}

void ExtensionService::GarbageCollectExtensions() {
  if (extension_prefs_->pref_service()->ReadOnly())
    return;

  scoped_ptr<ExtensionPrefs::ExtensionsInfo> info(
      extension_prefs_->GetInstalledExtensionsInfo());

  std::map<std::string, FilePath> extension_paths;
  for (size_t i = 0; i < info->size(); ++i)
    extension_paths[info->at(i)->extension_id] = info->at(i)->extension_path;

  if (!BrowserThread::PostTask(
          BrowserThread::FILE, FROM_HERE,
          base::Bind(
              &extension_file_util::GarbageCollectExtensions,
              install_directory_,
              extension_paths)))
    NOTREACHED();

  // Also garbage-collect themes.  We check |profile_| to be
  // defensive; in the future, we may call GarbageCollectExtensions()
  // from somewhere other than Init() (e.g., in a timer).
  if (profile_) {
    ThemeServiceFactory::GetForProfile(profile_)->RemoveUnusedThemes();
  }
}

void ExtensionService::OnLoadedInstalledExtensions() {
  if (updater_.get()) {
    updater_->Start();
  }

  ready_ = true;
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_EXTENSIONS_READY,
      content::Source<Profile>(profile_),
      content::NotificationService::NoDetails());
}

void ExtensionService::AddExtension(const Extension* extension) {
  // Ensure extension is deleted unless we transfer ownership.
  scoped_refptr<const Extension> scoped_extension(extension);

  // TODO(jstritar): We may be able to get rid of this branch by overriding the
  // default extension state to DISABLED when the --disable-extensions flag
  // is set (http://crbug.com/29067).
  if (!extensions_enabled() &&
      !extension->is_theme() &&
      extension->location() != Extension::COMPONENT &&
      !Extension::IsExternalLocation(extension->location()))
    return;

  SetBeingUpgraded(extension, false);

  // The extension is now loaded, remove its data from unloaded extension map.
  unloaded_extension_paths_.erase(extension->id());

  // If a terminated extension is loaded, remove it from the terminated list.
  UntrackTerminatedExtension(extension->id());

  // If the extension was disabled for a reload, then enable it.
  if (disabled_extension_paths_.erase(extension->id()) > 0)
    EnableExtension(extension->id());

  // Check if the extension's privileges have changed and disable the
  // extension if necessary.
  InitializePermissions(extension);

  bool disabled = extension_prefs_->IsExtensionDisabled(extension->id());
  if (disabled) {
    disabled_extensions_.Insert(scoped_extension);
    // TODO(aa): This seems dodgy. AddExtension() could get called with a
    // disabled extension for other reasons other than that an update was
    // disabled, e.g. as in ExtensionManagementTest.InstallRequiresConfirm.
    content::NotificationService::current()->Notify(
        chrome::NOTIFICATION_EXTENSION_UPDATE_DISABLED,
        content::Source<Profile>(profile_),
        content::Details<const Extension>(extension));
    SyncExtensionChangeIfNeeded(*extension);
    return;
  }

  // All apps that are displayed in the launcher are ordered by their ordinals
  // so we must ensure they have valid ordinals.
  if (extension->ShouldDisplayInLauncher())
    extension_prefs_->extension_sorting()->EnsureValidOrdinals(extension->id());

  extensions_.Insert(scoped_extension);
  SyncExtensionChangeIfNeeded(*extension);
  NotifyExtensionLoaded(extension);
}

void ExtensionService::InitializePermissions(const Extension* extension) {
  // If the extension has used the optional permissions API, it will have a
  // custom set of active permissions defined in the extension prefs. Here,
  // we update the extension's active permissions based on the prefs.
  scoped_refptr<ExtensionPermissionSet> active_permissions =
      extension_prefs()->GetActivePermissions(extension->id());

  if (active_permissions.get()) {
    // We restrict the active permissions to be within the bounds defined in the
    // extension's manifest.
    //  a) active permissions must be a subset of optional + default permissions
    //  b) active permissions must contains all default permissions
    scoped_refptr<ExtensionPermissionSet> total_permissions =
        ExtensionPermissionSet::CreateUnion(
            extension->required_permission_set(),
            extension->optional_permission_set());

    // Make sure the active permissions contain no more than optional + default.
    scoped_refptr<ExtensionPermissionSet> adjusted_active =
        ExtensionPermissionSet::CreateIntersection(
            total_permissions.get(), active_permissions.get());

    // Make sure the active permissions contain the default permissions.
    adjusted_active = ExtensionPermissionSet::CreateUnion(
            extension->required_permission_set(), adjusted_active.get());

    extensions::PermissionsUpdater perms_updater(profile());
    perms_updater.UpdateActivePermissions(extension, adjusted_active);
  }

  // We keep track of all permissions the user has granted each extension.
  // This allows extensions to gracefully support backwards compatibility
  // by including unknown permissions in their manifests. When the user
  // installs the extension, only the recognized permissions are recorded.
  // When the unknown permissions become recognized (e.g., through browser
  // upgrade), we can prompt the user to accept these new permissions.
  // Extensions can also silently upgrade to less permissions, and then
  // silently upgrade to a version that adds these permissions back.
  //
  // For example, pretend that Chrome 10 includes a permission "omnibox"
  // for an API that adds suggestions to the omnibox. An extension can
  // maintain backwards compatibility while still having "omnibox" in the
  // manifest. If a user installs the extension on Chrome 9, the browser
  // will record the permissions it recognized, not including "omnibox."
  // When upgrading to Chrome 10, "omnibox" will be recognized and Chrome
  // will disable the extension and prompt the user to approve the increase
  // in privileges. The extension could then release a new version that
  // removes the "omnibox" permission. When the user upgrades, Chrome will
  // still remember that "omnibox" had been granted, so that if the
  // extension once again includes "omnibox" in an upgrade, the extension
  // can upgrade without requiring this user's approval.
  const Extension* old = GetExtensionByIdInternal(extension->id(),
                                                  true, true, false);
  bool is_extension_upgrade = old != NULL;
  bool is_privilege_increase = false;

  // We only need to compare the granted permissions to the current permissions
  // if the extension is not allowed to silently increase its permissions.
  if (!extension->CanSilentlyIncreasePermissions()) {
    // Add all the recognized permissions if the granted permissions list
    // hasn't been initialized yet.
    scoped_refptr<ExtensionPermissionSet> granted_permissions =
        extension_prefs_->GetGrantedPermissions(extension->id());
    CHECK(granted_permissions.get());

    // Here, we check if an extension's privileges have increased in a manner
    // that requires the user's approval. This could occur because the browser
    // upgraded and recognized additional privileges, or an extension upgrades
    // to a version that requires additional privileges.
    is_privilege_increase =
        granted_permissions->HasLessPrivilegesThan(
            extension->GetActivePermissions());
  }

  if (is_extension_upgrade) {
    // Other than for unpacked extensions, CrxInstaller should have guaranteed
    // that we aren't downgrading.
    if (extension->location() != Extension::LOAD)
      CHECK(extension->version()->CompareTo(*(old->version())) >= 0);

    // Extensions get upgraded if the privileges are allowed to increase or
    // the privileges haven't increased.
    if (!is_privilege_increase) {
      SetBeingUpgraded(old, true);
      SetBeingUpgraded(extension, true);
    }

    // To upgrade an extension in place, unload the old one and
    // then load the new one.
    UnloadExtension(old->id(), extension_misc::UNLOAD_REASON_UPDATE);
    old = NULL;
  }

  // Extension has changed permissions significantly. Disable it. A
  // notification should be sent by the caller.
  if (is_privilege_increase) {
    if (!extension_prefs_->DidExtensionEscalatePermissions(extension->id())) {
      RecordPermissionMessagesHistogram(
          extension, "Extensions.Permissions_AutoDisable");
    }
    extension_prefs_->SetExtensionState(extension->id(), Extension::DISABLED);
    extension_prefs_->SetDidExtensionEscalatePermissions(extension, true);
  }
}

void ExtensionService::UpdateActiveExtensionsInCrashReporter() {
  std::set<std::string> extension_ids;
  for (ExtensionSet::const_iterator iter = extensions_.begin();
       iter != extensions_.end(); ++iter) {
    const Extension* extension = *iter;
    if (!extension->is_theme() && extension->location() != Extension::COMPONENT)
      extension_ids.insert(extension->id());
  }

  child_process_logging::SetActiveExtensions(extension_ids);
}

void ExtensionService::OnExtensionInstalled(
    const Extension* extension,
    bool from_webstore,
    const StringOrdinal& page_ordinal) {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // Ensure extension is deleted unless we transfer ownership.
  scoped_refptr<const Extension> scoped_extension(extension);
  const std::string& id = extension->id();
  // Extensions installed by policy can't be disabled. So even if a previous
  // installation disabled the extension, make sure it is now enabled.
  bool initial_enable =
      !extension_prefs_->IsExtensionDisabled(id) ||
      !Extension::UserMayDisable(extension->location());
  PendingExtensionInfo pending_extension_info;
  if (pending_extension_manager()->GetById(id, &pending_extension_info)) {
    pending_extension_manager()->Remove(id);

    if (!pending_extension_info.ShouldAllowInstall(*extension)) {
      LOG(WARNING)
          << "ShouldAllowInstall() returned false for "
          << id << " of type " << extension->GetType()
          << " and update URL " << extension->update_url().spec()
          << "; not installing";

      content::NotificationService::current()->Notify(
          chrome::NOTIFICATION_EXTENSION_INSTALL_NOT_ALLOWED,
          content::Source<Profile>(profile_),
          content::Details<const Extension>(extension));

      // Delete the extension directory since we're not going to
      // load it.
      if (!BrowserThread::PostTask(
              BrowserThread::FILE, FROM_HERE,
              base::Bind(&extension_file_util::DeleteFile,
                         extension->path(), true)))
        NOTREACHED();
      return;
    }
  } else {
    // We explicitly want to re-enable an uninstalled external
    // extension; if we're here, that means the user is manually
    // installing the extension.
    if (IsExternalExtensionUninstalled(id)) {
      initial_enable = true;
    }
  }

  // Do not record the install histograms for upgrades.
  if (!GetExtensionByIdInternal(extension->id(), true, true, false)) {
    UMA_HISTOGRAM_ENUMERATION("Extensions.InstallType",
                              extension->GetType(), 100);
    RecordPermissionMessagesHistogram(
        extension, "Extensions.Permissions_Install");
  }

  extension_prefs_->OnExtensionInstalled(
      extension,
      initial_enable ? Extension::ENABLED : Extension::DISABLED,
      from_webstore,
      page_ordinal);

  // Unpacked extensions default to allowing file access, but if that has been
  // overridden, don't reset the value.
  if (Extension::ShouldAlwaysAllowFileAccess(extension->location()) &&
      !extension_prefs_->HasAllowFileAccessSetting(id)) {
    extension_prefs_->SetAllowFileAccess(id, true);
  }

  // If the extension should automatically block network startup (e.g., it uses
  // the webRequest API), set the preference. Otherwise clear it, in case the
  // extension stopped using a relevant API.
  extension_prefs_->SetDelaysNetworkRequests(
      extension->id(), extension->ImplicitlyDelaysNetworkStartup());

  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_EXTENSION_INSTALLED,
      content::Source<Profile>(profile_),
      content::Details<const Extension>(extension));

  // Temporary feature to always install shortcuts for platform apps to
  // facilitate early testing.
  // TODO(benwells): Remove before launching platform apps.
  if (extension->is_platform_app()) {
      StartInstallApplicationShortcut(extension);
  }

  // Transfer ownership of |extension| to AddExtension.
  AddExtension(scoped_extension);
}

const Extension* ExtensionService::GetExtensionByIdInternal(
    const std::string& id, bool include_enabled, bool include_disabled,
    bool include_terminated) const {
  std::string lowercase_id = StringToLowerASCII(id);
  if (include_enabled) {
    const Extension* extension = extensions_.GetByID(lowercase_id);
    if (extension)
      return extension;
  }
  if (include_disabled) {
    const Extension* extension = disabled_extensions_.GetByID(lowercase_id);
    if (extension)
      return extension;
  }
  if (include_terminated) {
    const Extension* extension = terminated_extensions_.GetByID(lowercase_id);
    if (extension)
      return extension;
  }
  return NULL;
}

void ExtensionService::TrackTerminatedExtension(const Extension* extension) {
  if (!terminated_extensions_.Contains(extension->id()))
    terminated_extensions_.Insert(make_scoped_refptr(extension));

  UnloadExtension(extension->id(), extension_misc::UNLOAD_REASON_TERMINATE);
}

void ExtensionService::UntrackTerminatedExtension(const std::string& id) {
  std::string lowercase_id = StringToLowerASCII(id);
  terminated_extensions_.Remove(lowercase_id);
}

const Extension* ExtensionService::GetTerminatedExtension(
    const std::string& id) const {
  return GetExtensionByIdInternal(id, false, false, true);
}

const Extension* ExtensionService::GetInstalledExtension(
    const std::string& id) const {
  return GetExtensionByIdInternal(id, true, true, true);
}

const Extension* ExtensionService::GetWebStoreApp() {
  return GetExtensionById(extension_misc::kWebStoreAppId, false);
}

bool ExtensionService::ExtensionBindingsAllowed(const GURL& url) {
  // Allow bindings for all packaged extensions and component hosted apps.
  const Extension* extension = extensions_.GetExtensionOrAppByURL(
      ExtensionURLInfo(url));
  return extension && (!extension->is_hosted_app() ||
                       extension->location() == Extension::COMPONENT);
}

const SkBitmap& ExtensionService::GetOmniboxIcon(
    const std::string& extension_id) {
  return omnibox_icon_manager_.GetIcon(extension_id);
}

const SkBitmap& ExtensionService::GetOmniboxPopupIcon(
    const std::string& extension_id) {
  return omnibox_popup_icon_manager_.GetIcon(extension_id);
}

bool ExtensionService::OnExternalExtensionFileFound(
         const std::string& id,
         const Version* version,
         const FilePath& path,
         Extension::Location location,
         int creation_flags,
         bool mark_acknowledged) {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  CHECK(Extension::IdIsValid(id));
  if (extension_prefs_->IsExternalExtensionUninstalled(id))
    return false;

  DCHECK(version);

  // Before even bothering to unpack, check and see if we already have this
  // version. This is important because these extensions are going to get
  // installed on every startup.
  const Extension* existing = GetExtensionById(id, true);
  if (existing) {
    switch (existing->version()->CompareTo(*version)) {
      case -1:  // existing version is older, we should upgrade
        break;
      case 0:  // existing version is same, do nothing
        return false;
      case 1:  // existing version is newer, uh-oh
        LOG(WARNING) << "Found external version of extension " << id
                     << "that is older than current version. Current version "
                     << "is: " << existing->VersionString() << ". New version "
                     << "is: " << version->GetString()
                     << ". Keeping current version.";
        return false;
    }
  }

  // If the extension is already pending, don't start an install.
  if (!pending_extension_manager()->AddFromExternalFile(id, location))
    return false;

  // no client (silent install)
  scoped_refptr<CrxInstaller> installer(CrxInstaller::Create(this, NULL));
  installer->set_install_source(location);
  installer->set_expected_id(id);
  installer->set_expected_version(*version);
  installer->set_install_cause(extension_misc::INSTALL_CAUSE_EXTERNAL_FILE);
  installer->set_creation_flags(creation_flags);
  installer->InstallCrx(path);

  // Depending on the source, a new external extension might not need a user
  // notification on installation. For such extensions, mark them acknowledged
  // now to suppress the notification.
  if (mark_acknowledged)
    AcknowledgeExternalExtension(id);

  return true;
}

void ExtensionService::ReportExtensionLoadError(
    const FilePath& extension_path,
    const std::string &error,
    bool be_noisy) {
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_EXTENSION_LOAD_ERROR,
      content::Source<Profile>(profile_),
      content::Details<const std::string>(&error));

  std::string path_str = UTF16ToUTF8(extension_path.LossyDisplayName());
  string16 message = ASCIIToUTF16(base::StringPrintf(
      "Could not load extension from '%s'. %s",
      path_str.c_str(), error.c_str()));
  ExtensionErrorReporter::GetInstance()->ReportError(message, be_noisy);
}

void ExtensionService::DidCreateRenderViewForBackgroundPage(
    ExtensionHost* host) {
  OrphanedDevTools::iterator iter =
      orphaned_dev_tools_.find(host->extension_id());
  if (iter == orphaned_dev_tools_.end())
    return;

  DevToolsAgentHost* agent = DevToolsAgentHostRegistry::GetDevToolsAgentHost(
      host->render_view_host());
  content::DevToolsManager::GetInstance()->AttachClientHost(iter->second,
                                                            agent);
  orphaned_dev_tools_.erase(iter);
}

void ExtensionService::Observe(int type,
                               const content::NotificationSource& source,
                               const content::NotificationDetails& details) {
  switch (type) {
    case chrome::NOTIFICATION_EXTENSION_PROCESS_TERMINATED: {
      if (profile_ !=
          content::Source<Profile>(source).ptr()->GetOriginalProfile()) {
        break;
      }

      ExtensionHost* host = content::Details<ExtensionHost>(details).ptr();

      // Mark the extension as terminated and Unload it. We want it to
      // be in a consistent state: either fully working or not loaded
      // at all, but never half-crashed.  We do it in a PostTask so
      // that other handlers of this notification will still have
      // access to the Extension and ExtensionHost.
      MessageLoop::current()->PostTask(
          FROM_HERE,
          base::Bind(
              &ExtensionService::TrackTerminatedExtension,
              AsWeakPtr(),
              host->extension()));
      break;
    }
    case content::NOTIFICATION_RENDERER_PROCESS_CREATED: {
      content::RenderProcessHost* process =
          content::Source<content::RenderProcessHost>(source).ptr();
      Profile* host_profile =
          Profile::FromBrowserContext(process->GetBrowserContext());
      if (!profile_->IsSameProfile(host_profile->GetOriginalProfile()))
          break;

      // Valid extension function names, used to setup bindings in renderer.
      std::vector<std::string> function_names;
      ExtensionFunctionDispatcher::GetAllFunctionNames(&function_names);
      process->Send(new ExtensionMsg_SetFunctionNames(function_names));

      // Scripting whitelist. This is modified by tests and must be communicated
      // to renderers.
      process->Send(new ExtensionMsg_SetScriptingWhitelist(
          *Extension::GetScriptingWhitelist()));

      // Loaded extensions.
      std::vector<ExtensionMsg_Loaded_Params> loaded_extensions;
      for (ExtensionSet::const_iterator iter = extensions_.begin();
           iter != extensions_.end(); ++iter) {
        // Renderers don't need to know about themes.
        if (!(*iter)->is_theme())
          loaded_extensions.push_back(ExtensionMsg_Loaded_Params(*iter));
      }
      process->Send(new ExtensionMsg_Loaded(loaded_extensions));
      break;
    }
    case content::NOTIFICATION_RENDERER_PROCESS_TERMINATED: {
      content::RenderProcessHost* process =
          content::Source<content::RenderProcessHost>(source).ptr();
      Profile* host_profile =
          Profile::FromBrowserContext(process->GetBrowserContext());
      if (!profile_->IsSameProfile(host_profile->GetOriginalProfile()))
          break;

      installed_app_hosts_.erase(process->GetID());

      process_map_.RemoveAllFromProcess(process->GetID());
      BrowserThread::PostTask(
          BrowserThread::IO, FROM_HERE,
          base::Bind(&ExtensionInfoMap::UnregisterAllExtensionsInProcess,
                     profile_->GetExtensionInfoMap(),
                     process->GetID()));
      break;
    }
    case chrome::NOTIFICATION_PREF_CHANGED: {
      std::string* pref_name = content::Details<std::string>(details).ptr();
      if (*pref_name == prefs::kExtensionInstallAllowList ||
          *pref_name == prefs::kExtensionInstallDenyList) {
        CheckAdminBlacklist();
      } else {
        NOTREACHED() << "Unexpected preference name.";
      }
      break;
    }
    case chrome::NOTIFICATION_IMPORT_FINISHED: {
      InitAfterImport();
      break;
    }

    default:
      NOTREACHED() << "Unexpected notification type.";
  }
}

bool ExtensionService::HasApps() const {
  return !GetAppIds().empty();
}

ExtensionIdSet ExtensionService::GetAppIds() const {
  ExtensionIdSet result;
  for (ExtensionSet::const_iterator it = extensions_.begin();
       it != extensions_.end(); ++it) {
    if ((*it)->is_app() && (*it)->location() != Extension::COMPONENT)
      result.insert((*it)->id());
  }

  return result;
}

bool ExtensionService::IsBackgroundPageReady(const Extension* extension) {
  return (!extension->has_background_page() ||
          extension_runtime_data_[extension->id()].background_page_ready);
}

void ExtensionService::SetBackgroundPageReady(const Extension* extension) {
  DCHECK(extension->has_background_page());
  extension_runtime_data_[extension->id()].background_page_ready = true;
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_EXTENSION_BACKGROUND_PAGE_READY,
      content::Source<const Extension>(extension),
      content::NotificationService::NoDetails());
}

bool ExtensionService::IsBeingUpgraded(const Extension* extension) {
  return extension_runtime_data_[extension->id()].being_upgraded;
}

void ExtensionService::SetBeingUpgraded(const Extension* extension,
                                         bool value) {
  extension_runtime_data_[extension->id()].being_upgraded = value;
}

bool ExtensionService::HasUsedWebRequest(const Extension* extension) {
  return extension_runtime_data_[extension->id()].has_used_webrequest;
}

void ExtensionService::SetHasUsedWebRequest(const Extension* extension,
                                            bool value) {
  extension_runtime_data_[extension->id()].has_used_webrequest = value;
}

base::PropertyBag* ExtensionService::GetPropertyBag(
    const Extension* extension) {
  return &extension_runtime_data_[extension->id()].property_bag;
}

void ExtensionService::RegisterNaClModule(const GURL& url,
                                          const std::string& mime_type) {
  NaClModuleInfo info;
  info.url = url;
  info.mime_type = mime_type;

  DCHECK(FindNaClModule(url) == nacl_module_list_.end());
  nacl_module_list_.push_front(info);
}

void ExtensionService::UnregisterNaClModule(const GURL& url) {
  NaClModuleInfoList::iterator iter = FindNaClModule(url);
  DCHECK(iter != nacl_module_list_.end());
  nacl_module_list_.erase(iter);
}

void ExtensionService::UpdatePluginListWithNaClModules() {
  // An extension has been added which has a nacl_module component, which means
  // there is a MIME type that module wants to handle, so we need to add that
  // MIME type to plugins which handle NaCl modules in order to allow the
  // individual modules to handle these types.
  FilePath path;
  if (!PathService::Get(chrome::FILE_NACL_PLUGIN, &path))
    return;
  const content::PepperPluginInfo* pepper_info =
      PluginService::GetInstance()->GetRegisteredPpapiPluginInfo(path);
  if (!pepper_info)
    return;

  std::vector<webkit::WebPluginMimeType>::const_iterator mime_iter;
  // Check each MIME type the plugins handle for the NaCl MIME type.
  for (mime_iter = pepper_info->mime_types.begin();
       mime_iter != pepper_info->mime_types.end(); ++mime_iter) {
    if (mime_iter->mime_type == kNaClPluginMimeType) {
      // This plugin handles "application/x-nacl".

      PluginService::GetInstance()->
          UnregisterInternalPlugin(pepper_info->path);

      webkit::WebPluginInfo info = pepper_info->ToWebPluginInfo();

      for (ExtensionService::NaClModuleInfoList::const_iterator iter =
          nacl_module_list_.begin();
          iter != nacl_module_list_.end(); ++iter) {
        // Add the MIME type specified in the extension to this NaCl plugin,
        // With an extra "nacl" argument to specify the location of the NaCl
        // manifest file.
        webkit::WebPluginMimeType mime_type_info;
        mime_type_info.mime_type = iter->mime_type;
        mime_type_info.additional_param_names.push_back(UTF8ToUTF16("nacl"));
        mime_type_info.additional_param_values.push_back(
            UTF8ToUTF16(iter->url.spec()));
        info.mime_types.push_back(mime_type_info);
      }

      PluginService::GetInstance()->RefreshPlugins();
      PluginService::GetInstance()->RegisterInternalPlugin(info, true);
      // This plugin has been modified, no need to check the rest of its
      // types, but continue checking other plugins.
      break;
    }
  }
}

ExtensionService::NaClModuleInfoList::iterator
    ExtensionService::FindNaClModule(const GURL& url) {
  for (NaClModuleInfoList::iterator iter = nacl_module_list_.begin();
       iter != nacl_module_list_.end(); ++iter) {
    if (iter->url == url)
      return iter;
  }
  return nacl_module_list_.end();
}

void ExtensionService::StartInstallApplicationShortcut(
    const Extension* extension) {
#if !defined(OS_MACOSX)
  const int kAppIconSize = 32;

  shortcut_info_.extension_id = extension->id();
  shortcut_info_.url = GURL(extension->launch_web_url());
  shortcut_info_.title = UTF8ToUTF16(extension->name());
  shortcut_info_.description = UTF8ToUTF16(extension->description());
  shortcut_info_.create_in_applications_menu = true;
  shortcut_info_.create_in_quick_launch_bar = true;
  shortcut_info_.create_on_desktop = true;

  // The icon will be resized to |max_size|.
  const gfx::Size max_size(kAppIconSize, kAppIconSize);

  // Look for an icon. If there is no icon at the ideal size, we will resize
  // whatever we can get. Making a large icon smaller is prefered to making a
  // small icon larger, so look for a larger icon first:
  ExtensionResource icon_resource = extension->GetIconResource(
      kAppIconSize,
      ExtensionIconSet::MATCH_BIGGER);

  // If no icon exists that is the desired size or larger, get the
  // largest icon available:
  if (icon_resource.empty()) {
    icon_resource = extension->GetIconResource(
        kAppIconSize,
        ExtensionIconSet::MATCH_SMALLER);
  }

  // icon_resource may still be empty at this point, in which case LoadImage
  // which call the OnImageLoaded callback with a NULL image and exit
  // immediately.
  tracker_.LoadImage(extension,
                     icon_resource,
                     max_size,
                     ImageLoadingTracker::DONT_CACHE);
#endif
}

void ExtensionService::OnImageLoaded(SkBitmap *image,
                                     const ExtensionResource &resource,
                                     int index) {
  // If the image failed to load (e.g. if the resource being loaded was empty)
  // use the standard application icon.
  if (!image || image->isNull())
    image = ExtensionIconSource::LoadImageByResourceId(IDR_APP_DEFAULT_ICON);

  shortcut_info_.favicon = *image;
  web_app::CreateShortcut(profile_->GetPath(), shortcut_info_);
}

extensions::SocketController* ExtensionService::socket_controller() {
  // TODO(miket): Find a better place for SocketController to live. It needs
  // to be scoped such that it can be created and destroyed on the IO thread.
  //
  // To coexist with certain unit tests that don't have an IO thread message
  // loop available at ExtensionService shutdown, we lazy-initialize this
  // object so that those cases neither create nor destroy a SocketController.
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  if (!socket_controller_) {
    socket_controller_ = new extensions::SocketController();
  }
  return socket_controller_;
}
