// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/profiles/profile_dependency_manager.h"

#include <algorithm>
#include <deque>
#include <iterator>

#include "chrome/browser/autocomplete/network_action_predictor_factory.h"
#include "chrome/browser/autofill/personal_data_manager_factory.h"
#include "chrome/browser/background/background_contents_service_factory.h"
#include "chrome/browser/content_settings/cookie_settings.h"
#include "chrome/browser/download/download_service_factory.h"
#include "chrome/browser/intents/web_intents_registry_factory.h"
#include "chrome/browser/notifications/desktop_notification_service_factory.h"
#include "chrome/browser/plugin_prefs_factory.h"
#include "chrome/browser/prerender/prerender_manager_factory.h"
#include "chrome/browser/printing/cloud_print/cloud_print_proxy_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_keyed_service.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"
#include "chrome/browser/protector/protector_service_factory.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "chrome/browser/speech/speech_input_extension_manager.h"
#include "chrome/browser/spellchecker/spellcheck_factory.h"
#include "chrome/browser/sync/profile_sync_service_factory.h"
#include "chrome/browser/tabs/pinned_tab_service_factory.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/find_bar/find_bar_state_factory.h"
#include "chrome/browser/ui/global_error_service_factory.h"
#include "chrome/browser/ui/webui/ntp/ntp_resource_cache_factory.h"

#ifndef NDEBUG
#include "base/command_line.h"
#include "base/file_util.h"
#include "chrome/common/chrome_switches.h"
#endif

class Profile;

void ProfileDependencyManager::AddComponent(
    ProfileKeyedServiceFactory* component) {
  all_components_.push_back(component);
  destruction_order_.clear();
}

void ProfileDependencyManager::RemoveComponent(
    ProfileKeyedServiceFactory* component) {
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

void ProfileDependencyManager::AddEdge(ProfileKeyedServiceFactory* depended,
                                       ProfileKeyedServiceFactory* dependee) {
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
  for (std::vector<ProfileKeyedServiceFactory*>::reverse_iterator rit =
           destruction_order_.rbegin(); rit != destruction_order_.rend();
       ++rit) {
    if (!profile->IsOffTheRecord()) {
      // We only register preferences on normal profiles because the incognito
      // profile shares the pref service with the normal one.
      (*rit)->RegisterUserPrefsOnProfile(profile);
    }

    if (is_testing_profile && (*rit)->ServiceIsNULLWhileTesting()) {
      (*rit)->SetTestingFactory(profile, NULL);
    } else if ((*rit)->ServiceIsCreatedWithProfile()) {
      // Create the service.
      (*rit)->GetServiceForProfile(profile, true);
    }
  }
}

void ProfileDependencyManager::DestroyProfileServices(Profile* profile) {
  if (destruction_order_.empty())
    BuildDestructionOrder(profile);

  for (std::vector<ProfileKeyedServiceFactory*>::const_iterator it =
           destruction_order_.begin(); it != destruction_order_.end(); ++it) {
    (*it)->ProfileShutdown(profile);
  }

#ifndef NDEBUG
  // The profile is now dead to the rest of the program.
  dead_profile_pointers_.insert(profile);
#endif

  for (std::vector<ProfileKeyedServiceFactory*>::const_iterator it =
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
// each ServiceFactory initializes iteslf and registers its dependencies with
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

  BackgroundContentsServiceFactory::GetInstance();
  CloudPrintProxyServiceFactory::GetInstance();
  CookieSettings::Factory::GetInstance();
  DesktopNotificationServiceFactory::GetInstance();
  DownloadServiceFactory::GetInstance();
  FindBarStateFactory::GetInstance();
  GlobalErrorServiceFactory::GetInstance();
  NetworkActionPredictorFactory::GetInstance();
  NTPResourceCacheFactory::GetInstance();
  PersonalDataManagerFactory::GetInstance();
  PinnedTabServiceFactory::GetInstance();
  PluginPrefsFactory::GetInstance();
  protector::ProtectorServiceFactory::GetInstance();
  prerender::PrerenderManagerFactory::GetInstance();
  ProfileSyncServiceFactory::GetInstance();
  SessionServiceFactory::GetInstance();
  SigninManagerFactory::GetInstance();
  SpeechInputExtensionManager::InitializeFactory();
  SpellCheckFactory::GetInstance();
  TabRestoreServiceFactory::GetInstance();
  ThemeServiceFactory::GetInstance();
  TemplateURLServiceFactory::GetInstance();
  WebIntentsRegistryFactory::GetInstance();

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
  std::deque<ProfileKeyedServiceFactory*> queue;
  std::copy(all_components_.begin(),
            all_components_.end(),
            std::back_inserter(queue));

  std::deque<ProfileKeyedServiceFactory*>::iterator queue_end = queue.end();
  for (EdgeMap::const_iterator it = edges_.begin();
       it != edges_.end(); ++it) {
    queue_end = std::remove(queue.begin(), queue_end, it->second);
  }
  queue.erase(queue_end, queue.end());

  // Step 2: Do the Kahn topological sort.
  std::vector<ProfileKeyedServiceFactory*> output;
  EdgeMap edges(edges_);
  while (!queue.empty()) {
    ProfileKeyedServiceFactory* node = queue.front();
    queue.pop_front();
    output.push_back(node);

    std::pair<EdgeMap::iterator, EdgeMap::iterator> range =
        edges.equal_range(node);
    EdgeMap::iterator it = range.first;
    while (it != range.second) {
      ProfileKeyedServiceFactory* dest = it->second;
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
  std::deque<ProfileKeyedServiceFactory*> components;
  std::copy(all_components_.begin(),
            all_components_.end(),
            std::back_inserter(components));

  // State all dependencies and remove |second| so we don't generate an
  // implicit dependency on the Profile hard coded node.
  std::deque<ProfileKeyedServiceFactory*>::iterator components_end =
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
  for (std::deque<ProfileKeyedServiceFactory*>::const_iterator it =
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
