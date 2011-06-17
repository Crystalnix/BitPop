// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PLUGIN_UPDATER_H_
#define CHROME_BROWSER_PLUGIN_UPDATER_H_
#pragma once

#include <set>
#include <vector>

#include "base/basictypes.h"
#include "base/file_path.h"
#include "base/memory/singleton.h"
#include "content/common/notification_observer.h"

class DictionaryValue;
class ListValue;
class NotificationDetails;
class NotificationSource;
class Profile;

namespace webkit {
namespace npapi {
class PluginGroup;
struct WebPluginInfo;
}
}

class PluginUpdater : public NotificationObserver {
 public:
  // Get a list of all the plugin groups. The caller should take ownership
  // of the returned ListValue.
  static ListValue* GetPluginGroupsData();

  // Enable or disable a plugin group.
  void EnablePluginGroup(bool enable, const string16& group_name);

  // Enable or disable a specific plugin file.
  void EnablePlugin(bool enable, const FilePath::StringType& file_path);

  // Enable or disable plugin groups as defined by the user's preference file.
  void UpdatePluginGroupsStateFromPrefs(Profile* profile);

  // Write the enable/disable status to the user's preference file.
  void UpdatePreferences(Profile* profile, int delay_ms);

  // NotificationObserver method overrides
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

  static PluginUpdater* GetInstance();

 private:
  PluginUpdater();
  virtual ~PluginUpdater() {}

  // Called on the file thread to get the data necessary to update the saved
  // preferences.  The profile pointer is only to be passed to the UI thread.
  static void GetPreferencesDataOnFileThread(void* profile);

  // Called on the UI thread with the plugin data to save the preferences.
  static void OnUpdatePreferences(
      Profile* profile,
      const std::vector<webkit::npapi::WebPluginInfo>& plugins,
      const std::vector<webkit::npapi::PluginGroup>& groups);

  // Queues sending the notification that plugin data has changed.  This is done
  // so that if a bunch of changes happen, we only send one notification.
  void NotifyPluginStatusChanged();

  // Used for the post task to notify that plugin enabled status changed.
  static void OnNotifyPluginStatusChanged();

  static DictionaryValue* CreatePluginFileSummary(
      const webkit::npapi::WebPluginInfo& plugin);

  // Force plugins to be enabled or disabled due to policy.
  // |disabled_list| contains the list of StringValues of the names of the
  // policy-disabled plugins, |exceptions_list| the policy-allowed plugins,
  // and |enabled_list| the policy-enabled plugins.
  void UpdatePluginsStateFromPolicy(const ListValue* disabled_list,
                                    const ListValue* exceptions_list,
                                    const ListValue* enabled_list);

  void ListValueToStringSet(const ListValue* src, std::set<string16>* dest);

  // Needed to allow singleton instantiation using private constructor.
  friend struct DefaultSingletonTraits<PluginUpdater>;

  bool notify_pending_;

  DISALLOW_COPY_AND_ASSIGN(PluginUpdater);
};

#endif  // CHROME_BROWSER_PLUGIN_UPDATER_H_
