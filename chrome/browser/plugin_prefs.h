// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PLUGIN_PREFS_H_
#define CHROME_BROWSER_PLUGIN_PREFS_H_
#pragma once

#include <map>
#include <set>
#include <vector>

#include "base/basictypes.h"
#include "base/file_path.h"
#include "base/memory/ref_counted.h"
#include "base/synchronization/lock.h"
#include "chrome/browser/prefs/pref_change_registrar.h"
#include "content/public/browser/notification_observer.h"

class Profile;

namespace base {
class ListValue;
}

namespace webkit {
struct WebPluginInfo;
namespace npapi {
class PluginGroup;
class PluginList;
}
}

// This class stores information about whether a plug-in or a plug-in group is
// enabled or disabled.
// Except where otherwise noted, it can be used on every thread.
class PluginPrefs : public base::RefCountedThreadSafe<PluginPrefs>,
                    public content::NotificationObserver {
 public:
  enum PolicyStatus {
    NO_POLICY = 0,  // Neither enabled or disabled by policy.
    POLICY_ENABLED,
    POLICY_DISABLED,
  };

  // Returns the instance associated with |profile|, creating it if necessary.
  static PluginPrefs* GetForProfile(Profile* profile);

  // Usually the PluginPrefs associated with a TestingProfile is NULL.
  // This method overrides that for a given TestingProfile, returning the newly
  // created PluginPrefs object.
  static PluginPrefs* GetForTestingProfile(Profile* profile);

  // Sets the plug-in list for tests.
  void SetPluginListForTesting(webkit::npapi::PluginList* plugin_list);

  // Creates a new instance. This method should only be used for testing.
  PluginPrefs();

  // Associates this instance with |prefs|. This enables or disables
  // plugin groups as defined by the user's preferences.
  // This method should only be called on the UI thread.
  void SetPrefs(PrefService* prefs);

  // Detaches from the PrefService before it is destroyed.
  // As the name says, this method should only be called on the UI thread.
  void ShutdownOnUIThread();

  // Enable or disable a plugin group.
  void EnablePluginGroup(bool enable, const string16& group_name);

  // Enable or disable a specific plugin file.
  // Returns false if the plug-in state cannot be changed because of a policy.
  bool EnablePlugin(bool enable, const FilePath& file_path);

  // Enable or disable a plug-in in all profiles. This sets a default for
  // profiles which are created later as well.
  // Returns false if the plug-in state cannot be changed because of a policy.
  // This method should only be called on the UI thread.
  static bool EnablePluginGlobally(bool enable, const FilePath& file_path);

  // Returns whether there is a policy enabling or disabling plug-ins of the
  // given name.
  PolicyStatus PolicyStatusForPlugin(const string16& name) const;

  // Returns whether the plugin is enabled or not.
  bool IsPluginEnabled(const webkit::WebPluginInfo& plugin) const;

  // Registers the preferences used by this class.
  // This method should only be called on the UI thread.
  static void RegisterPrefs(PrefService* prefs);

  void set_profile(Profile* profile) { profile_ = profile; }

  // content::NotificationObserver method override.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

 private:
  friend class base::RefCountedThreadSafe<PluginPrefs>;
  friend class PluginPrefsTest;

  virtual ~PluginPrefs();

  // Allows unit tests to directly set enforced plug-in patterns.
  void SetPolicyEnforcedPluginPatterns(
      const std::set<string16>& disabled_patterns,
      const std::set<string16>& disabled_exception_patterns,
      const std::set<string16>& enabled_patterns);

  // Returns the plugin list to use, either the singleton or the override.
  webkit::npapi::PluginList* GetPluginList() const;

  // Callback for after the plugin groups have been loaded.
  void EnablePluginGroupInternal(
      bool enabled,
      const string16& group_name,
      const std::vector<webkit::npapi::PluginGroup>& groups);
  void EnablePluginInternal(
      bool enabled,
      const FilePath& path,
      const std::vector<webkit::npapi::PluginGroup>& groups);

  // Called on the file thread to get the data necessary to update the saved
  // preferences.
  void GetPreferencesDataOnFileThread();

  // Called on the UI thread with the plugin data to save the preferences.
  void OnUpdatePreferences(
      const std::vector<webkit::npapi::PluginGroup>& groups);

  // Sends the notification that plugin data has changed.
  void NotifyPluginStatusChanged();

  static void ListValueToStringSet(const base::ListValue* src,
                                   std::set<string16>* dest);

  // Checks if |name| matches any of the patterns in |pattern_set|.
  static bool IsStringMatchedInSet(const string16& name,
                                   const std::set<string16>& pattern_set);

  // Guards access to the following data structures.
  mutable base::Lock lock_;

  std::map<FilePath, bool> plugin_state_;
  std::map<string16, bool> plugin_group_state_;

  std::set<string16> policy_disabled_plugin_patterns_;
  std::set<string16> policy_disabled_plugin_exception_patterns_;
  std::set<string16> policy_enabled_plugin_patterns_;

  // Weak pointer, owns us. Only used as a notification source.
  Profile* profile_;

  // Weak pointer, owned by the profile.
  PrefService* prefs_;

  // PluginList to use for testing. If this is NULL, defaults to the global
  // singleton.
  webkit::npapi::PluginList* plugin_list_;

  PrefChangeRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(PluginPrefs);
};

#endif  // CHROME_BROWSER_PLUGIN_PREFS_H_
