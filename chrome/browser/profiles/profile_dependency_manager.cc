// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/profiles/profile_dependency_manager.h"

#include <algorithm>
#include <deque>
#include <iterator>

#include "chrome/browser/autofill/personal_data_manager_factory.h"
#include "chrome/browser/background/background_contents_service_factory.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/content_settings/cookie_settings.h"
#include "chrome/browser/custom_handlers/protocol_handler_registry_factory.h"
#include "chrome/browser/download/download_service_factory.h"
#include "chrome/browser/extensions/api/bluetooth/bluetooth_api_factory.h"
#include "chrome/browser/extensions/api/bookmarks/bookmark_api_factory.h"
#include "chrome/browser/extensions/api/commands/command_service_factory.h"
#include "chrome/browser/extensions/api/cookies/cookies_api_factory.h"
#include "chrome/browser/extensions/api/dial/dial_api_factory.h"
#include "chrome/browser/extensions/api/discovery/suggested_links_registry_factory.h"
#include "chrome/browser/extensions/api/font_settings/font_settings_api_factory.h"
#include "chrome/browser/extensions/api/history/history_api_factory.h"
#include "chrome/browser/extensions/api/idle/idle_manager_factory.h"
#include "chrome/browser/extensions/api/managed_mode/managed_mode_api_factory.h"
#include "chrome/browser/extensions/api/management/management_api_factory.h"
#include "chrome/browser/extensions/api/media_galleries_private/media_galleries_private_api_factory.h"
#include "chrome/browser/extensions/api/omnibox/omnibox_api_factory.h"
#include "chrome/browser/extensions/api/preference/preference_api_factory.h"
#include "chrome/browser/extensions/api/processes/processes_api_factory.h"
#include "chrome/browser/extensions/api/push_messaging/push_messaging_api_factory.h"
#include "chrome/browser/extensions/api/tab_capture/tab_capture_registry_factory.h"
#include "chrome/browser/extensions/api/tabs/tabs_windows_api_factory.h"
#include "chrome/browser/extensions/api/web_navigation/web_navigation_api_factory.h"
#include "chrome/browser/extensions/app_restore_service_factory.h"
#include "chrome/browser/extensions/extension_system_factory.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/google/google_url_tracker_factory.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/history/shortcuts_backend_factory.h"
#include "chrome/browser/intents/web_intents_registry_factory.h"
#include "chrome/browser/media_gallery/media_galleries_preferences_factory.h"
#include "chrome/browser/notifications/desktop_notification_service_factory.h"
#include "chrome/browser/password_manager/password_store_factory.h"
#include "chrome/browser/plugins/plugin_prefs_factory.h"
#include "chrome/browser/predictors/autocomplete_action_predictor_factory.h"
#include "chrome/browser/predictors/predictor_database_factory.h"
#include "chrome/browser/predictors/resource_prefetch_predictor_factory.h"
#include "chrome/browser/prerender/prerender_link_manager_factory.h"
#include "chrome/browser/prerender/prerender_manager_factory.h"
#include "chrome/browser/printing/cloud_print/cloud_print_proxy_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_keyed_service.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"
#include "chrome/browser/search_engines/template_url_fetcher_factory.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "chrome/browser/signin/token_service_factory.h"
#include "chrome/browser/speech/chrome_speech_recognition_preferences.h"
#include "chrome/browser/speech/speech_input_extension_manager.h"
#include "chrome/browser/spellchecker/spellcheck_factory.h"
#include "chrome/browser/sync/profile_sync_service_factory.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/thumbnails/thumbnail_service_factory.h"
#include "chrome/browser/ui/find_bar/find_bar_state_factory.h"
#include "chrome/browser/ui/global_error/global_error_service_factory.h"
#include "chrome/browser/ui/tabs/pinned_tab_service_factory.h"
#include "chrome/browser/ui/webui/chrome_url_data_manager_factory.h"
#include "chrome/browser/ui/webui/ntp/ntp_resource_cache_factory.h"
#include "chrome/browser/user_style_sheet_watcher_factory.h"
#include "chrome/browser/visitedlink/visitedlink_master_factory.h"
#include "chrome/browser/webdata/web_data_service_factory.h"

#if defined(ENABLE_CAPTIVE_PORTAL_DETECTION)
#include "chrome/browser/captive_portal/captive_portal_service_factory.h"
#endif

#if defined(ENABLE_CONFIGURATION_POLICY)
#include "chrome/browser/policy/user_policy_signin_service_factory.h"
#endif

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/extensions/input_method_api_factory.h"
#endif

#if defined(USE_AURA)
#include "chrome/browser/ui/gesture_prefs_observer_factory_aura.h"
#endif

#ifndef NDEBUG
#include "base/command_line.h"
#include "base/file_util.h"
#include "chrome/common/chrome_switches.h"
#endif

class Profile;

void ProfileDependencyManager::AddComponent(
    ProfileKeyedBaseFactory* component) {
  all_components_.push_back(component);
  destruction_order_.clear();
}

void ProfileDependencyManager::RemoveComponent(
    ProfileKeyedBaseFactory* component) {
  all_components_.erase(std::remove(all_components_.begin(),
                                    all_components_.end(),
                                    component),
                        all_components_.end());

  // Remove all dependency edges that contain this component.
  EdgeMap::iterator it = edges_.begin();
  while (it != edges_.end()) {
    EdgeMap::iterator temp = it;
    ++it;

    if (temp->first == component || temp->second == component)
      edges_.erase(temp);
  }

  destruction_order_.clear();
}

void ProfileDependencyManager::AddEdge(ProfileKeyedBaseFactory* depended,
                                       ProfileKeyedBaseFactory* dependee) {
  edges_.insert(std::make_pair(depended, dependee));
  destruction_order_.clear();
}

void ProfileDependencyManager::CreateProfileServices(Profile* profile,
                                                     bool is_testing_profile) {
#ifndef NDEBUG
  // Unmark |profile| as dead. This exists because of unit tests, which will
  // often have similar stack structures. 0xWhatever might be created, go out
  // of scope, and then a new Profile object might be created at 0xWhatever.
  dead_profile_pointers_.erase(profile);
#endif

  AssertFactoriesBuilt();

  if (destruction_order_.empty())
    BuildDestructionOrder(profile);

  // Iterate in reverse destruction order for creation.
  for (std::vector<ProfileKeyedBaseFactory*>::reverse_iterator rit =
           destruction_order_.rbegin(); rit != destruction_order_.rend();
       ++rit) {
    if (!profile->IsOffTheRecord()) {
      // We only register preferences on normal profiles because the incognito
      // profile shares the pref service with the normal one.
      (*rit)->RegisterUserPrefsOnProfile(profile);
    }

    if (is_testing_profile && (*rit)->ServiceIsNULLWhileTesting()) {
      (*rit)->SetEmptyTestingFactory(profile);
    } else if ((*rit)->ServiceIsCreatedWithProfile()) {
      // Create the service.
      (*rit)->CreateServiceNow(profile);
    }
  }
}

void ProfileDependencyManager::DestroyProfileServices(Profile* profile) {
  if (destruction_order_.empty())
    BuildDestructionOrder(profile);

  for (std::vector<ProfileKeyedBaseFactory*>::const_iterator it =
           destruction_order_.begin(); it != destruction_order_.end(); ++it) {
    (*it)->ProfileShutdown(profile);
  }

#ifndef NDEBUG
  // The profile is now dead to the rest of the program.
  dead_profile_pointers_.insert(profile);
#endif

  for (std::vector<ProfileKeyedBaseFactory*>::const_iterator it =
           destruction_order_.begin(); it != destruction_order_.end(); ++it) {
    (*it)->ProfileDestroyed(profile);
  }
}

#ifndef NDEBUG
void ProfileDependencyManager::AssertProfileWasntDestroyed(Profile* profile) {
  if (dead_profile_pointers_.find(profile) != dead_profile_pointers_.end()) {
    NOTREACHED() << "Attempted to access a Profile that was ShutDown(). This "
                 << "is most likely a heap smasher in progress. After "
                 << "ProfileKeyedService::Shutdown() completes, your service "
                 << "MUST NOT refer to depended Profile services again.";
  }
}
#endif

// static
ProfileDependencyManager* ProfileDependencyManager::GetInstance() {
  return Singleton<ProfileDependencyManager>::get();
}

ProfileDependencyManager::ProfileDependencyManager()
    : built_factories_(false) {
}

ProfileDependencyManager::~ProfileDependencyManager() {}

// This method gets the instance of each ServiceFactory. We do this so that
// each ServiceFactory initializes itself and registers its dependencies with
// the global PreferenceDependencyManager. We need to have a complete
// dependency graph when we create a profile so we can dispatch the profile
// creation message to the services that want to create their services at
// profile creation time.
//
// TODO(erg): This needs to be something else. I don't think putting every
// FooServiceFactory here will scale or is desireable long term.
void ProfileDependencyManager::AssertFactoriesBuilt() {
  if (built_factories_)
    return;

#if defined(ENABLE_BACKGROUND)
  BackgroundContentsServiceFactory::GetInstance();
#endif
  BookmarkModelFactory::GetInstance();
#if defined(ENABLE_CAPTIVE_PORTAL_DETECTION)
  captive_portal::CaptivePortalServiceFactory::GetInstance();
#endif
  ChromeURLDataManagerFactory::GetInstance();
#if defined(ENABLE_PRINTING)
  CloudPrintProxyServiceFactory::GetInstance();
#endif
  CookieSettings::Factory::GetInstance();
#if defined(ENABLE_NOTIFICATIONS)
  DesktopNotificationServiceFactory::GetInstance();
#endif
  DownloadServiceFactory::GetInstance();
#if defined(ENABLE_EXTENSIONS)
  extensions::AppRestoreServiceFactory::GetInstance();
  extensions::BookmarkAPIFactory::GetInstance();
  extensions::BluetoothAPIFactory::GetInstance();
  extensions::CommandServiceFactory::GetInstance();
  extensions::CookiesAPIFactory::GetInstance();
  extensions::DialAPIFactory::GetInstance();
  extensions::ExtensionSystemFactory::GetInstance();
  extensions::FontSettingsAPIFactory::GetInstance();
  extensions::HistoryAPIFactory::GetInstance();
  extensions::IdleManagerFactory::GetInstance();
#if defined(OS_CHROMEOS)
  extensions::InputMethodAPIFactory::GetInstance();
#endif
  extensions::ManagedModeAPIFactory::GetInstance();
  extensions::MediaGalleriesPrivateAPIFactory::GetInstance();
  extensions::OmniboxAPIFactory::GetInstance();
  extensions::PreferenceAPIFactory::GetInstance();
  extensions::ProcessesAPIFactory::GetInstance();
  extensions::PushMessagingAPIFactory::GetInstance();
  extensions::SuggestedLinksRegistryFactory::GetInstance();
  extensions::TabCaptureRegistryFactory::GetInstance();
  extensions::TabsWindowsAPIFactory::GetInstance();
  extensions::WebNavigationAPIFactory::GetInstance();
  ExtensionManagementAPIFactory::GetInstance();
#endif
  FaviconServiceFactory::GetInstance();
  FindBarStateFactory::GetInstance();
#if defined(USE_AURA)
  GesturePrefsObserverFactoryAura::GetInstance();
#endif
  GlobalErrorServiceFactory::GetInstance();
  GoogleURLTrackerFactory::GetInstance();
  HistoryServiceFactory::GetInstance();
  MediaGalleriesPreferencesFactory::GetInstance();
  NTPResourceCacheFactory::GetInstance();
  PasswordStoreFactory::GetInstance();
  PersonalDataManagerFactory::GetInstance();
#if !defined(OS_ANDROID)
  PinnedTabServiceFactory::GetInstance();
#endif
  PluginPrefsFactory::GetInstance();
#if defined(ENABLE_CONFIGURATION_POLICY) && !defined(OS_CHROMEOS)
  // Not used on chromeos because signin happens before the profile is loaded.
  policy::UserPolicySigninServiceFactory::GetInstance();
#endif
  predictors::AutocompleteActionPredictorFactory::GetInstance();
  predictors::PredictorDatabaseFactory::GetInstance();
  predictors::ResourcePrefetchPredictorFactory::GetInstance();
  prerender::PrerenderManagerFactory::GetInstance();
  prerender::PrerenderLinkManagerFactory::GetInstance();
  ProfileSyncServiceFactory::GetInstance();
  ProtocolHandlerRegistryFactory::GetInstance();
#if defined(ENABLE_SESSION_SERVICE)
  SessionServiceFactory::GetInstance();
#endif
  ShortcutsBackendFactory::GetInstance();
  ThumbnailServiceFactory::GetInstance();
  SigninManagerFactory::GetInstance();
#if defined(ENABLE_INPUT_SPEECH)
  SpeechInputExtensionManager::InitializeFactory();
  ChromeSpeechRecognitionPreferences::InitializeFactory();
#endif
  SpellcheckServiceFactory::GetInstance();
  TabRestoreServiceFactory::GetInstance();
  TemplateURLFetcherFactory::GetInstance();
  TemplateURLServiceFactory::GetInstance();
#if defined(ENABLE_THEMES)
  ThemeServiceFactory::GetInstance();
#endif
  TokenServiceFactory::GetInstance();
  UserStyleSheetWatcherFactory::GetInstance();
  VisitedLinkMasterFactory::GetInstance();
  WebDataServiceFactory::GetInstance();
#if defined(ENABLE_WEB_INTENTS)
  WebIntentsRegistryFactory::GetInstance();
#endif

  built_factories_ = true;
}

void ProfileDependencyManager::BuildDestructionOrder(Profile* profile) {
#if !defined(NDEBUG)
  // Whenever we try to build a destruction ordering, we should also dump a
  // dependency graph to "/path/to/profile/profile-dependencies.dot".
  if (CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kDumpProfileDependencyGraph)) {
    FilePath dot_file =
        profile->GetPath().AppendASCII("profile-dependencies.dot");
    std::string contents = DumpGraphvizDependency();
    file_util::WriteFile(dot_file, contents.c_str(), contents.size());
  }
#endif

  // Step 1: Build a set of nodes with no incoming edges.
  std::deque<ProfileKeyedBaseFactory*> queue;
  std::copy(all_components_.begin(),
            all_components_.end(),
            std::back_inserter(queue));

  std::deque<ProfileKeyedBaseFactory*>::iterator queue_end = queue.end();
  for (EdgeMap::const_iterator it = edges_.begin();
       it != edges_.end(); ++it) {
    queue_end = std::remove(queue.begin(), queue_end, it->second);
  }
  queue.erase(queue_end, queue.end());

  // Step 2: Do the Kahn topological sort.
  std::vector<ProfileKeyedBaseFactory*> output;
  EdgeMap edges(edges_);
  while (!queue.empty()) {
    ProfileKeyedBaseFactory* node = queue.front();
    queue.pop_front();
    output.push_back(node);

    std::pair<EdgeMap::iterator, EdgeMap::iterator> range =
        edges.equal_range(node);
    EdgeMap::iterator it = range.first;
    while (it != range.second) {
      ProfileKeyedBaseFactory* dest = it->second;
      EdgeMap::iterator temp = it;
      it++;
      edges.erase(temp);

      bool has_incoming_edges = false;
      for (EdgeMap::iterator jt = edges.begin(); jt != edges.end(); ++jt) {
        if (jt->second == dest) {
          has_incoming_edges = true;
          break;
        }
      }

      if (!has_incoming_edges)
        queue.push_back(dest);
    }
  }

  if (edges.size()) {
    NOTREACHED() << "Dependency graph has a cycle. We are doomed.";
  }

  std::reverse(output.begin(), output.end());
  destruction_order_ = output;
}

#if !defined(NDEBUG)

std::string ProfileDependencyManager::DumpGraphvizDependency() {
  std::string result("digraph {\n");

  // Make a copy of all components.
  std::deque<ProfileKeyedBaseFactory*> components;
  std::copy(all_components_.begin(),
            all_components_.end(),
            std::back_inserter(components));

  // State all dependencies and remove |second| so we don't generate an
  // implicit dependency on the Profile hard coded node.
  std::deque<ProfileKeyedBaseFactory*>::iterator components_end =
      components.end();
  result.append("  /* Dependencies */\n");
  for (EdgeMap::const_iterator it = edges_.begin(); it != edges_.end(); ++it) {
    result.append("  ");
    result.append(it->second->name());
    result.append(" -> ");
    result.append(it->first->name());
    result.append(";\n");

    components_end = std::remove(components.begin(), components_end,
                                 it->second);
  }
  components.erase(components_end, components.end());

  // Every node that doesn't depend on anything else will implicitly depend on
  // the Profile.
  result.append("\n  /* Toplevel attachments */\n");
  for (std::deque<ProfileKeyedBaseFactory*>::const_iterator it =
           components.begin(); it != components.end(); ++it) {
    result.append("  ");
    result.append((*it)->name());
    result.append(" -> Profile;\n");
  }

  result.append("\n  /* Toplevel profile */\n");
  result.append("  Profile [shape=box];\n");

  result.append("}\n");
  return result;
}

#endif
