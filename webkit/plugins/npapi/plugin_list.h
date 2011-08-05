// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_PLUGINS_NPAPI_PLUGIN_LIST_H_
#define WEBKIT_PLUGINS_NPAPI_PLUGIN_LIST_H_

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/basictypes.h"
#include "base/file_path.h"
#include "base/memory/linked_ptr.h"
#include "base/memory/scoped_vector.h"
#include "base/synchronization/lock.h"
#include "third_party/npapi/bindings/nphostapi.h"
#include "webkit/plugins/npapi/plugin_group.h"
#include "webkit/plugins/npapi/webplugininfo.h"

class GURL;

namespace base {

template <typename T>
struct DefaultLazyInstanceTraits;

}  // namespace base

namespace webkit {
namespace npapi {

extern FilePath::CharType kDefaultPluginLibraryName[];

class PluginInstance;

// This struct holds entry points into a plugin.  The entry points are
// slightly different between Win/Mac and Unixes.
struct PluginEntryPoints {
#if !defined(OS_POSIX) || defined(OS_MACOSX)
  NP_GetEntryPointsFunc np_getentrypoints;
#endif
  NP_InitializeFunc np_initialize;
  NP_ShutdownFunc np_shutdown;
};

// The PluginList is responsible for loading our NPAPI based plugins. It does
// so in whatever manner is appropriate for the platform. On Windows, it loads
// plugins from a known directory by looking for DLLs which start with "NP",
// and checking to see if they are valid NPAPI libraries. On the Mac, it walks
// the machine-wide and user plugin directories and loads anything that has
// the correct types. On Linux, it walks the plugin directories as well
// (e.g. /usr/lib/browser-plugins/).
// This object is thread safe.
class PluginList {
 public:
  // Gets the one instance of the PluginList.
  static PluginList* Singleton();

  // Returns true if we're in debug-plugin-loading mode. This is controlled
  // by a command line switch.
  static bool DebugPluginLoading();

  // Returns true iff the plugin list has been loaded already.
  bool PluginsLoaded();

  // Cause the plugin list to refresh next time they are accessed, regardless
  // of whether they are already loaded.
  void RefreshPlugins();

  // Add/Remove an extra plugin to load when we actually do the loading.  Must
  // be called before the plugins have been loaded.
  void AddExtraPluginPath(const FilePath& plugin_path);
  void RemoveExtraPluginPath(const FilePath& plugin_path);

  // Same as above, but specifies a directory in which to search for plugins.
  void AddExtraPluginDir(const FilePath& plugin_dir);

  // Get the ordered list of directories from which to load plugins
  void GetPluginDirectories(std::vector<FilePath>* plugin_dirs);

  // Register an internal plugin with the specified plugin information.
  // An internal plugin must be registered before it can
  // be loaded using PluginList::LoadPlugin().
  void RegisterInternalPlugin(const WebPluginInfo& info);

  // This second version is for "plugins" that have been compiled
  // directly into the binary -- callers must provide the metadata and
  // the entry points.
  // TODO(evan): we use file names here, but they're not really files, they're
  // actually a string that uniquely identifies the plugin.
  void RegisterInternalPlugin(const FilePath& filename,
                              const std::string& name,
                              const std::string& description,
                              const std::string& mime_type,
                              const PluginEntryPoints& entry_points);

  // Removes a specified internal plugin from the list. The search will match
  // on the path from the version info previously registered.
  //
  // This is generally only necessary for tests.
  void UnregisterInternalPlugin(const FilePath& path);

  // Creates a WebPluginInfo structure given a plugin's path.  On success
  // returns true, with the information being put into "info".  If it's an
  // internal plugin, "entry_points" is filled in as well with a
  // internally-owned PluginEntryPoints pointer.
  // Returns false if the library couldn't be found, or if it's not a plugin.
  bool ReadPluginInfo(const FilePath& filename,
                      WebPluginInfo* info,
                      const PluginEntryPoints** entry_points);

  // In Windows plugins, the mime types are passed as a specially formatted list
  // of strings. This function parses those strings into a WebPluginMimeType
  // vector.
  // TODO(evan): move this code into plugin_list_win.
  static bool ParseMimeTypes(const std::string& mime_types,
                             const std::string& file_extensions,
                             const string16& mime_type_descriptions,
                             std::vector<WebPluginMimeType>* parsed_mime_types);

  // Get all the plugins.
  void GetPlugins(bool refresh, std::vector<WebPluginInfo>* plugins);

  // Get all the enabled plugins.
  void GetEnabledPlugins(bool refresh, std::vector<WebPluginInfo>* plugins);

  // Returns a list in |info| containing plugins that are found for
  // the given url and mime type (including disabled plugins, for
  // which |info->enabled| is false).  The mime type which corresponds
  // to the URL is optionally returned back in |actual_mime_types| (if
  // it is non-NULL), one for each of the plugin info objects found.
  // The |allow_wildcard| parameter controls whether this function
  // returns plugins which support wildcard mime types (* as the mime
  // type).  The |info| parameter is required to be non-NULL.  The
  // list is in order of "most desirable" to "least desirable",
  // meaning that the default plugin is at the end of the list.
  void GetPluginInfoArray(const GURL& url,
                          const std::string& mime_type,
                          bool allow_wildcard,
                          std::vector<WebPluginInfo>* info,
                          std::vector<std::string>* actual_mime_types);

  // Returns the first item from the list returned in GetPluginInfo in |info|.
  // Returns true if it found a match.  |actual_mime_type| may be NULL.
  bool GetPluginInfo(const GURL& url,
                     const std::string& mime_type,
                     bool allow_wildcard,
                     WebPluginInfo* info,
                     std::string* actual_mime_type);

  // Get plugin info by plugin path (including disabled plugins). Returns true
  // if the plugin is found and WebPluginInfo has been filled in |info|.
  bool GetPluginInfoByPath(const FilePath& plugin_path,
                           WebPluginInfo* info);

  // Populates the given vector with all available plugin groups.
  void GetPluginGroups(bool load_if_necessary,
                       std::vector<PluginGroup>* plugin_groups);

  // Returns the PluginGroup corresponding to the given WebPluginInfo. If no
  // such group exists, it is created and added to the cache.
  // Beware: when calling this from the Browser process, the group that the
  // returned pointer points to might disappear suddenly. This happens when
  // |RefreshPlugins()| is called and then |LoadPlugins()| is triggered by a
  // call to |GetPlugins()|, |GetEnabledPlugins()|, |GetPluginInfoArray()|,
  // |GetPluginInfoByPath()|, or |GetPluginGroups(true, _)|. It is the caller's
  // responsibility to make sure this doesn't happen.
  const PluginGroup* GetPluginGroup(const WebPluginInfo& web_plugin_info);

  // Returns the name of the PluginGroup with the given identifier.
  // If no such group exists, an empty string is returned.
  string16 GetPluginGroupName(const std::string& identifier);

  // Returns the identifier string of the PluginGroup corresponding to the given
  // WebPluginInfo. If no such group exists, it is created and added to the
  // cache.
  std::string GetPluginGroupIdentifier(const WebPluginInfo& web_plugin_info);

  // Load a specific plugin with full path.
  void LoadPlugin(const FilePath& filename,
                  ScopedVector<PluginGroup>* plugin_groups);

  // Enable a specific plugin, specified by path. Returns |true| iff a plugin
  // currently in the plugin list was actually enabled as a result; regardless
  // of return value, if a plugin is found in the future with the given name, it
  // will be enabled.
  bool EnablePlugin(const FilePath& filename);

  // Disable a specific plugin, specified by path. Returns |true| iff a plugin
  // currently in the plugin list was actually disabled as a result; regardless
  // of return value, if a plugin is found in the future with the given name, it
  // will be disabled.
  bool DisablePlugin(const FilePath& filename);

  // Enable/disable a plugin group, specified by group_name.  Returns |true| iff
  // a plugin currently in the plugin list was actually enabled/disabled as a
  // result; regardless of return value, if a plugin is found in the future with
  // the given name, it will be enabled/disabled.
  bool EnableGroup(bool enable, const string16& name);

  // Disable all plugins groups that are known to be outdated, according to
  // the information hardcoded in PluginGroup, to make sure that they can't
  // be loaded on a web page and instead show a UI to update to the latest
  // version.
  void DisableOutdatedPluginGroups();

  virtual ~PluginList();

 protected:
  // This constructor is used in unit tests to override the platform-dependent
  // real-world plugin group definitions with custom ones.
  PluginList(const PluginGroupDefinition* definitions, size_t num_definitions);

  // Adds the given WebPluginInfo to its corresponding group, creating it if
  // necessary, and returns the group.
  // Callers need to protect calls to this method by a lock themselves.
  PluginGroup* AddToPluginGroups(const WebPluginInfo& web_plugin_info,
                                 ScopedVector<PluginGroup>* plugin_groups);

 private:
  friend class PluginListTest;
  friend struct base::DefaultLazyInstanceTraits<PluginList>;
  FRIEND_TEST_ALL_PREFIXES(PluginGroupTest, PluginGroupDefinition);

  // Constructors are private for singletons
  PluginList();

  // Creates PluginGroups for the static group definitions, and adds them to
  // the list of PluginGroups.
  void AddHardcodedPluginGroups(ScopedVector<PluginGroup>* groups);

  // Implements all IO dependant operations of the LoadPlugins method so that
  // test classes can mock these out. Return value false means LoadPlugins
  // should not go on with the loading.
  virtual void LoadPluginsInternal(ScopedVector<PluginGroup>* plugin_groups);

  // Load all plugins from the default plugins directory
  void LoadPlugins(bool refresh);

  // Load all plugins from a specific directory.
  // |plugins| is updated with loaded plugin information.
  // |visited_plugins| is updated with paths to all plugins that were considered
  //   (including those we didn't load)
  void LoadPluginsFromDir(const FilePath& path,
                          ScopedVector<PluginGroup>* plugin_groups,
                          std::set<FilePath>* visited_plugins);

  // Returns true if we should load the given plugin, or false otherwise.
  // plugins is the list of plugins we have crawled in the current plugin
  // loading run.
  bool ShouldLoadPlugin(const WebPluginInfo& info,
                        ScopedVector<PluginGroup>* plugins);

  // Return whether a plug-in group with the given name should be disabled,
  // either because it already is on the list of disabled groups, or because it
  // is blacklisted by a policy. In the latter case, add the plugin group to the
  // list of disabled groups as well.
  bool ShouldDisableGroup(const string16& group_name);

  // Returns true if the plugin supports |mime_type|. |mime_type| should be all
  // lower case.
  bool SupportsType(const WebPluginInfo& plugin,
                    const std::string& mime_type,
                    bool allow_wildcard);

  // Returns true if the given plugin supports a given file extension.
  // |extension| should be all lower case. If |mime_type| is not NULL, it will
  // be set to the MIME type if found. The MIME type which corresponds to the
  // extension is optionally returned back.
  bool SupportsExtension(const WebPluginInfo& plugin,
                         const std::string& extension,
                         std::string* actual_mime_type);

  //
  // Platform functions
  //

  // Do any initialization.
  void PlatformInit();

  //
  // Command-line switches
  //

#if defined(OS_WIN)
  // true if we shouldn't load the new WMP plugin.
  bool dont_load_new_wmp_;

  // Loads plugins registered under HKCU\Software\MozillaPlugins and
  // HKLM\Software\MozillaPlugins.
  void LoadPluginsFromRegistry(ScopedVector<PluginGroup>* plugins,
                               std::set<FilePath>* visited_plugins);
#endif

  //
  // Internals
  //
  // Set to true when the LoadPlugins method has finished.
  bool plugins_loaded_;

  // If true, we reload plugins even if they've been loaded already.
  bool plugins_need_refresh_;

  // Extra plugin paths that we want to search when loading.
  std::vector<FilePath> extra_plugin_paths_;

  // Extra plugin directories that we want to search when loading.
  std::vector<FilePath> extra_plugin_dirs_;

  struct InternalPlugin {
    WebPluginInfo info;
    PluginEntryPoints entry_points;
  };
  // Holds information about internal plugins.
  std::vector<InternalPlugin> internal_plugins_;

  // If set to true outdated plugins are disabled in the end of LoadPlugins.
  bool disable_outdated_plugins_;

  // Hardcoded plugin group definitions.
  const PluginGroupDefinition* const group_definitions_;
  const size_t num_group_definitions_;

  // Holds the currently available plugin groups.
  ScopedVector<PluginGroup> plugin_groups_;

  // The set of plugins that have been scheduled for disabling once they get
  // loaded. This list is used in LoadPlugins and pruned after it. Contains
  // plugins that were either disabled by the user (prefs are loaded before
  // plugins) or disabled by a policy.
  std::set<FilePath> plugins_to_disable_;
  // Equivalent to the |plugins_to_disable_| this is the set of groups
  // scheduled for disabling once they appear. This list is never completely
  // pruned but all groups that do get created are removed from it. New groups
  // might get added if they should be pruned because of plugins getting removed
  // for example.
  std::set<string16> groups_to_disable_;

  // Need synchronization for the above members since this object can be
  // accessed on multiple threads.
  base::Lock lock_;

  // Set to true if the default plugin is enabled.
  bool default_plugin_enabled_;

  DISALLOW_COPY_AND_ASSIGN(PluginList);
};

}  // namespace npapi
}  // namespace webkit

#endif  // WEBKIT_PLUGINS_NPAPI_PLUGIN_LIST_H_
