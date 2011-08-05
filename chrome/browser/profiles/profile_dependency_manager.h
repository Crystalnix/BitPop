// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PROFILES_PROFILE_DEPENDENCY_MANAGER_H_
#define CHROME_BROWSER_PROFILES_PROFILE_DEPENDENCY_MANAGER_H_

#include <map>
#include <vector>

#include "base/memory/singleton.h"

#ifndef NDEBUG
#include <set>
#endif

class Profile;
class ProfileKeyedServiceFactory;

// A singleton that listens for profile destruction notifications and
// rebroadcasts them to each ProfileKeyedServiceFactory in a safe order based
// on the stated dependencies by each service.
class ProfileDependencyManager {
 public:
  // Adds/Removes a component from our list of live components. Removing will
  // also remove live dependency links.
  void AddComponent(ProfileKeyedServiceFactory* component);
  void RemoveComponent(ProfileKeyedServiceFactory* component);

  // Adds a dependency between two factories.
  void AddEdge(ProfileKeyedServiceFactory* depended,
               ProfileKeyedServiceFactory* dependee);

  // Called by each Profile to alert us that we should destroy services
  // associated with it.
  //
  // Why not use the existing PROFILE_DESTROYED notification?
  //
  // - Because we need to do everything here after the application has handled
  //   being notified about PROFILE_DESTROYED.
  // - Because this class is a singleton and Singletons can't rely on
  //   NotificationService in unit tests because NotificationService is
  //   replaced in many tests.
  void DestroyProfileServices(Profile* profile);

#ifndef NDEBUG
  // Unmark |profile| as dead. This exists because of unit tests, which will
  // often have similar stack structures. 0xWhatever might be created, go out
  // of scope, and then a new Profile object might be created at 0xWhatever.
  void ProfileNowExists(Profile* profile);

  // Debugging assertion called as part of GetServiceForProfile in debug
  // mode. This will NOTREACHED() whenever the user is trying to access a stale
  // Profile*.
  void AssertProfileWasntDestroyed(Profile* profile);
#endif

  static ProfileDependencyManager* GetInstance();

 private:
  friend class ProfileDependencyManagerUnittests;
  friend struct DefaultSingletonTraits<ProfileDependencyManager>;

  typedef std::multimap<ProfileKeyedServiceFactory*,
                        ProfileKeyedServiceFactory*> EdgeMap;

  ProfileDependencyManager();
  virtual ~ProfileDependencyManager();

  // Using the dependency graph defined in |edges_|, fills |destruction_order_|
  // so that Observe() can notify each ProfileKeyedServiceFactory in order.
  void BuildDestructionOrder();

  std::vector<ProfileKeyedServiceFactory*> all_components_;

  EdgeMap edges_;

  std::vector<ProfileKeyedServiceFactory*> destruction_order_;

#ifndef NDEBUG
  // A list of profile objects that have gone through the Shutdown()
  // phase. These pointers are most likely invalid, but we keep track of their
  // locations in memory so we can nicely assert if we're asked to do anything
  // with them.
  std::set<Profile*> dead_profile_pointers_;
#endif
};

#endif  // CHROME_BROWSER_PROFILES_PROFILE_DEPENDENCY_MANAGER_H_
