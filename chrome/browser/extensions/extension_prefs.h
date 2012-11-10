// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_PREFS_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_PREFS_H_

#include <set>
#include <string>
#include <vector>

#include "base/memory/linked_ptr.h"
#include "base/time.h"
#include "base/values.h"
#include "chrome/browser/extensions/api/content_settings/content_settings_store.h"
#include "chrome/browser/extensions/extension_prefs_scope.h"
#include "chrome/browser/extensions/extension_scoped_prefs.h"
#include "chrome/browser/extensions/management_policy.h"
#include "chrome/browser/media_gallery/media_galleries_preferences.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/url_pattern_set.h"
#include "chrome/common/string_ordinal.h"

class ExtensionPrefValueMap;
class ExtensionSorting;
class PrefService;
class URLPatternSet;

namespace extensions {
class ExtensionPrefsUninstallExtension;
struct ExtensionOmniboxSuggestion;

// Class for managing global and per-extension preferences.
//
// This class distinguishes the following kinds of preferences:
// - global preferences:
//       internal state for the extension system in general, not associated
//       with an individual extension, such as lastUpdateTime.
// - per-extension preferences:
//       meta-preferences describing properties of the extension like
//       installation time, whether the extension is enabled, etc.
// - extension controlled preferences:
//       browser preferences that an extension controls. For example, an
//       extension could use the proxy API to specify the browser's proxy
//       preference. Extension-controlled preferences are stored in
//       PrefValueStore::extension_prefs(), which this class populates and
//       maintains as the underlying extensions change.
class ExtensionPrefs : public ContentSettingsStore::Observer,
                       public ManagementPolicy::Provider,
                       public ExtensionScopedPrefs {
 public:
  // Key name for a preference that keeps track of per-extension settings. This
  // is a dictionary object read from the Preferences file, keyed off of
  // extension ids.
  static const char kExtensionsPref[];

  typedef std::vector<linked_ptr<ExtensionInfo> > ExtensionsInfo;

  // Vector containing identifiers for preferences.
  typedef std::set<std::string> PrefKeySet;

  // Vector containing identifiers for extensions.
  typedef std::vector<std::string> ExtensionIdSet;

  // This enum is used for the launch type the user wants to use for an
  // application.
  // Do not remove items or re-order this enum as it is used in preferences
  // and histograms.
  enum LaunchType {
    LAUNCH_PINNED,
    LAUNCH_REGULAR,
    LAUNCH_FULLSCREEN,
    LAUNCH_WINDOW,

    // Launch an app in the in the way a click on the NTP would,
    // if no user pref were set.  Update this constant to change
    // the default for the NTP and chrome.management.launchApp().
    LAUNCH_DEFAULT = LAUNCH_REGULAR
  };

  // Does not assume ownership of |prefs| and |extension_pref_value_map|.
  // Note that you must call Init() to finalize construction.
  ExtensionPrefs(PrefService* prefs,
                 const FilePath& root_dir,
                 ExtensionPrefValueMap* extension_pref_value_map);
  virtual ~ExtensionPrefs();

  // If |extensions_disabled| is true, extension controlled preferences and
  // content settings do not become effective.
  void Init(bool extensions_disabled);

  // Returns a copy of the Extensions prefs.
  // TODO(erikkay) Remove this so that external consumers don't need to be
  // aware of the internal structure of the preferences.
  base::DictionaryValue* CopyCurrentExtensions();

  // Returns true if the specified external extension was uninstalled by the
  // user.
  bool IsExternalExtensionUninstalled(const std::string& id) const;

  // Checks whether |extension_id| is disabled. If there's no state pref for
  // the extension, this will return false. Generally you should use
  // ExtensionService::IsExtensionEnabled instead.
  bool IsExtensionDisabled(const std::string& id) const;

  // Get the order that the browser actions appear in the toolbar.
  std::vector<std::string> GetToolbarOrder();

  // Set the order that the browser actions appear in the toolbar.
  void SetToolbarOrder(const std::vector<std::string>& extension_ids);

  // Called when an extension is installed, so that prefs get created.
  // If |page_ordinal| is an invalid ordinal, then a page will be found
  // for the App.
  void OnExtensionInstalled(const Extension* extension,
                            Extension::State initial_state,
                            bool from_webstore,
                            const StringOrdinal& page_ordinal);

  // Called when an extension is uninstalled, so that prefs get cleaned up.
  void OnExtensionUninstalled(const std::string& extension_id,
                              const Extension::Location& location,
                              bool external_uninstall);

  // Called to change the extension's state when it is enabled/disabled.
  void SetExtensionState(const std::string& extension_id, Extension::State);

  // Returns all installed extensions
  void GetExtensions(ExtensionIdSet* out);

  // Returns all installed extensions from |extension_prefs|. This is exposed
  // for ProtectedPrefsWatcher because it needs access to the extension ID list
  // before the ExtensionService is initialized.
  static ExtensionIdSet GetExtensionsFrom(
      const base::DictionaryValue* extension_prefs);

  // Getter and setter for browser action visibility.
  bool GetBrowserActionVisibility(const Extension* extension);
  void SetBrowserActionVisibility(const Extension* extension,
     bool visible);

  // Did the extension ask to escalate its permission during an upgrade?
  bool DidExtensionEscalatePermissions(const std::string& id);

  // If |did_escalate| is true, the preferences for |extension| will be set to
  // require the install warning when the user tries to enable.
  void SetDidExtensionEscalatePermissions(
      const Extension* extension,
      bool did_escalate);

  // Getter and setters for disabled reason.
  Extension::DisableReason GetDisableReason(
      const std::string& extension_id);
  void SetDisableReason(const std::string& extension_id,
                        Extension::DisableReason disable_reason);
  void RemoveDisableReason(const std::string& extension_id);

  // Returns the version string for the currently installed extension, or
  // the empty string if not found.
  std::string GetVersionString(const std::string& extension_id);

  // Re-writes the extension manifest into the prefs.
  // Called to change the extension's manifest when it's re-localized.
  void UpdateManifest(const Extension* extension);

  // Returns extension path based on extension ID, or empty FilePath on error.
  FilePath GetExtensionPath(const std::string& extension_id);

  // Returns base extensions install directory.
  const FilePath& install_directory() const { return install_directory_; }

  // Updates the prefs based on the blacklist.
  void UpdateBlacklist(const std::set<std::string>& blacklist_set);

  // Based on extension id, checks prefs to see if it is blacklisted.
  bool IsExtensionBlacklisted(const std::string& id);

  // Based on extension id, checks prefs to see if it is orphaned.
  bool IsExtensionOrphaned(const std::string& id);

  // Whether the user has acknowledged an external extension.
  bool IsExternalExtensionAcknowledged(const std::string& extension_id);
  void AcknowledgeExternalExtension(const std::string& extension_id);

  // Whether the user has acknowledged a blacklisted extension.
  bool IsBlacklistedExtensionAcknowledged(const std::string& extension_id);
  void AcknowledgeBlacklistedExtension(const std::string& extension_id);

  // Whether the user has acknowledged an orphaned extension.
  bool IsOrphanedExtensionAcknowledged(const std::string& extension_id);
  void AcknowledgeOrphanedExtension(const std::string& extension_id);

  // Returns true if the extension notification code has already run for the
  // first time for this profile. Currently we use this flag to mean that any
  // extensions that would trigger notifications should get silently
  // acknowledged. This is a fuse. Calling it the first time returns false.
  // Subsequent calls return true. It's not possible through an API to ever
  // reset it. Don't call it unless you mean it!
  bool SetAlertSystemFirstRun();

  // The oauth client id used for app notification setup, if any.
  std::string GetAppNotificationClientId(const std::string& extension_id) const;
  void SetAppNotificationClientId(const std::string& extension_id,
                                  const std::string& oauth_client_id);

  // Whether app notifications are disabled for the given app.
  bool IsAppNotificationDisabled(const std::string& extension_id) const;
  void SetAppNotificationDisabled(const std::string& extension_id, bool value);

  // ManagementPolicy::Provider
  // These methods apply admin policy to extensions.
  virtual std::string GetDebugPolicyProviderName() const OVERRIDE;
  virtual bool UserMayLoad(const Extension* extension,
                           string16* error) const OVERRIDE;
  virtual bool UserMayModifySettings(const Extension* extension,
                                     string16* error) const OVERRIDE;
  virtual bool MustRemainEnabled(const Extension* extension,
                                 string16* error) const OVERRIDE;

  // Checks if extensions are blacklisted by default, by policy.
  // The ManagementPolicy::Provider methods also take this into account, and
  // should be used instead when the extension ID is known.
  bool ExtensionsBlacklistedByDefault() const;

  // Returns the last value set via SetLastPingDay. If there isn't such a
  // pref, the returned Time will return true for is_null().
  base::Time LastPingDay(const std::string& extension_id) const;

  // The time stored is based on the server's perspective of day start time, not
  // the client's.
  void SetLastPingDay(const std::string& extension_id, const base::Time& time);

  // Similar to the 2 above, but for the extensions blacklist.
  base::Time BlacklistLastPingDay() const;
  void SetBlacklistLastPingDay(const base::Time& time);

  // Similar to LastPingDay/SetLastPingDay, but for sending "days since active"
  // ping.
  base::Time LastActivePingDay(const std::string& extension_id);
  void SetLastActivePingDay(const std::string& extension_id,
                            const base::Time& time);

  // A bit we use for determining if we should send the "days since active"
  // ping. A value of true means the item has been active (launched) since the
  // last update check.
  bool GetActiveBit(const std::string& extension_id);
  void SetActiveBit(const std::string& extension_id, bool active);

  // Returns the granted permission set for the extension with |extension_id|,
  // and NULL if no preferences were found for |extension_id|.
  // This passes ownership of the returned set to the caller.
  PermissionSet* GetGrantedPermissions(const std::string& extension_id);

  // Adds |permissions| to the granted permissions set for the extension with
  // |extension_id|. The new granted permissions set will be the union of
  // |permissions| and the already granted permissions.
  void AddGrantedPermissions(const std::string& extension_id,
                             const PermissionSet* permissions);

  // As above, but subtracts the given |permissions| from the granted set.
  void RemoveGrantedPermissions(const std::string& extension_id,
                                const PermissionSet* permissions);

  // Gets the active permission set for the specified extension. This may
  // differ from the permissions in the manifest due to the optional
  // permissions API. This passes ownership of the set to the caller.
  PermissionSet* GetActivePermissions(const std::string& extension_id);

  // Sets the active |permissions| for the extension with |extension_id|.
  void SetActivePermissions(const std::string& extension_id,
                            const PermissionSet* permissions);

  // Returns the list of events that the given extension has registered for.
  std::set<std::string> GetRegisteredEvents(const std::string& extension_id);
  void SetRegisteredEvents(const std::string& extension_id,
                           const std::set<std::string>& events);

  // Adds a filter to an event.
  void AddFilterToEvent(const std::string& event_name,
                        const std::string& extension_id,
                        const DictionaryValue* filter);

  // Removes a filter from an event.
  void RemoveFilterFromEvent(const std::string& event_name,
                             const std::string& extension_id,
                             const DictionaryValue* filter);

  // Returns the dictionary of event filters that the given extension has
  // registered.
  const DictionaryValue* GetFilteredEvents(
      const std::string& extension_id) const;

  // Controls the omnibox default suggestion as set by the extension.
  ExtensionOmniboxSuggestion GetOmniboxDefaultSuggestion(
      const std::string& extension_id);
  void SetOmniboxDefaultSuggestion(
      const std::string& extension_id,
      const ExtensionOmniboxSuggestion& suggestion);

  // Returns true if the user enabled this extension to be loaded in incognito
  // mode.
  bool IsIncognitoEnabled(const std::string& extension_id);
  void SetIsIncognitoEnabled(const std::string& extension_id, bool enabled);

  // Returns true if the user has chosen to allow this extension to inject
  // scripts into pages with file URLs.
  bool AllowFileAccess(const std::string& extension_id);
  void SetAllowFileAccess(const std::string& extension_id, bool allow);
  bool HasAllowFileAccessSetting(const std::string& extension_id) const;

  // Get the launch type preference.  If no preference is set, return
  // |default_pref_value|.
  LaunchType GetLaunchType(const std::string& extension_id,
                           LaunchType default_pref_value);

  void SetLaunchType(const std::string& extension_id, LaunchType launch_type);

  // Find the right launch container based on the launch type.
  // If |extension|'s prefs do not have a launch type set, then
  // use |default_pref_value|.
  extension_misc::LaunchContainer GetLaunchContainer(
      const Extension* extension,
      LaunchType default_pref_value);

  // Set and retrieve permissions for media galleries as identified by the
  // gallery id.
  void SetMediaGalleryPermission(const std::string& extension_id,
                                 MediaGalleryPrefId gallery, bool has_access);
  std::vector<MediaGalleryPermission> GetMediaGalleryPermissions(
      const std::string& extension_id);
  void RemoveMediaGalleryPermissions(MediaGalleryPrefId gallery_id);

  // Saves ExtensionInfo for each installed extension with the path to the
  // version directory and the location. Blacklisted extensions won't be saved
  // and neither will external extensions the user has explicitly uninstalled.
  // Caller takes ownership of returned structure.
  ExtensionsInfo* GetInstalledExtensionsInfo();

  // Returns the ExtensionInfo from the prefs for the given extension. If the
  // extension is not present, NULL is returned.
  ExtensionInfo* GetInstalledExtensionInfo(const std::string& extension_id);

  // We've downloaded an updated .crx file for the extension, but are waiting
  // for idle time to install it.
  void SetIdleInstallInfo(const std::string& extension_id,
                          const FilePath& crx_path,
                          const std::string& version,
                          const base::Time& fetch_time);

  // Removes any idle install information we have for the given |extension_id|.
  // Returns true if there was info to remove; false otherwise.
  bool RemoveIdleInstallInfo(const std::string& extension_id);

  // If we have idle install information for |extension_id|, this puts it into
  // the out parameters and returns true. Otherwise returns false.
  bool GetIdleInstallInfo(const std::string& extension_id,
                          FilePath* crx_path,
                          std::string* version,
                          base::Time* fetch_time);

  // Returns the extension id's that have idle install information.
  std::set<std::string> GetIdleInstallInfoIds();

  // We allow the web store to set a string containing login information when a
  // purchase is made, so that when a user logs into sync with a different
  // account we can recognize the situation. The Get function returns true if
  // there was previously stored data (placing it in |result|), or false
  // otherwise. The Set will overwrite any previous login.
  bool GetWebStoreLogin(std::string* result);
  void SetWebStoreLogin(const std::string& login);

  // Returns true if the user repositioned the app on the app launcher via drag
  // and drop.
  bool WasAppDraggedByUser(const std::string& extension_id);

  // Sets a flag indicating that the user repositioned the app on the app
  // launcher by drag and dropping it.
  void SetAppDraggedByUser(const std::string& extension_id);

  // The extension's update URL data.  If not empty, the ExtensionUpdater
  // will append a ap= parameter to the URL when checking if a new version
  // of the extension is available.
  void SetUpdateUrlData(const std::string& extension_id,
                        const std::string& data);
  std::string GetUpdateUrlData(const std::string& extension_id);

  // Sets a preference value that is controlled by the extension. In other
  // words, this is not a pref value *about* the extension but something
  // global the extension wants to override.
  // Takes ownership of |value|.
  void SetExtensionControlledPref(const std::string& extension_id,
                                  const std::string& pref_key,
                                  ExtensionPrefsScope scope,
                                  base::Value* value);

  void RemoveExtensionControlledPref(const std::string& extension_id,
                                     const std::string& pref_key,
                                     ExtensionPrefsScope scope);

  // Returns true if currently no extension with higher precedence controls the
  // preference.
  bool CanExtensionControlPref(const std::string& extension_id,
                               const std::string& pref_key,
                               bool incognito);

  // Returns true if extension |extension_id| currently controls the
  // preference. If |from_incognito| is not NULL, looks at incognito preferences
  // first, and |from_incognito| is set to true if the effective pref value is
  // coming from the incognito preferences, false if it is coming from the
  // normal ones.
  bool DoesExtensionControlPref(const std::string& extension_id,
                                const std::string& pref_key,
                                bool* from_incognito);

  // Returns true if there is an extension which controls the preference value
  //  for |pref_key| *and* it is specific to incognito mode.
  bool HasIncognitoPrefValue(const std::string& pref_key);

  // Clears incognito session-only content settings for all extensions.
  void ClearIncognitoSessionOnlyContentSettings();

  // Returns true if the extension was installed from the Chrome Web Store.
  bool IsFromWebStore(const std::string& extension_id) const;

  // Returns true if the extension was installed from an App generated from a
  // bookmark.
  bool IsFromBookmark(const std::string& extension_id) const;

  // Helper method to acquire the installation time of an extension.
  // Returns base::Time() if the installation time could not be parsed or
  // found.
  base::Time GetInstallTime(const std::string& extension_id) const;

  static void RegisterUserPrefs(PrefService* prefs);

  ContentSettingsStore* content_settings_store() {
    return content_settings_store_.get();
  }

  // The underlying PrefService.
  PrefService* pref_service() const { return prefs_; }

  // The underlying ExtensionSorting.
  ExtensionSorting* extension_sorting() const {
    return extension_sorting_.get();
  }

  // Describes the URLs that are able to install extensions. See
  // prefs::kExtensionAllowedInstallSites for more information.
  URLPatternSet GetAllowedInstallSites();

 protected:
  // For unit testing. Enables injecting an artificial clock that is used
  // to query the current time, when an extension is installed.
  virtual base::Time GetCurrentTime() const;

 private:
  friend class ExtensionPrefsUninstallExtension;  // Unit test.

  // extensions::ContentSettingsStore::Observer methods:
  virtual void OnContentSettingChanged(const std::string& extension_id,
                                       bool incognito) OVERRIDE;

  // ExtensionScopedPrefs methods:
  virtual void UpdateExtensionPref(const std::string& id,
                                   const std::string& key,
                                   base::Value* value) OVERRIDE;
  virtual void DeleteExtensionPrefs(const std::string& id) OVERRIDE;
  virtual bool ReadExtensionPrefBoolean(
      const std::string& extension_id,
      const std::string& pref_key) const OVERRIDE;
  virtual bool ReadExtensionPrefInteger(const std::string& extension_id,
                                        const std::string& pref_key,
                                        int* out_value) const OVERRIDE;
  virtual bool ReadExtensionPrefList(
      const std::string& extension_id,
      const std::string& pref_key,
      const base::ListValue** out_value) const OVERRIDE;
  virtual bool ReadExtensionPrefString(const std::string& extension_id,
                                       const std::string& pref_key,
                                       std::string* out_value) const OVERRIDE;

  // Converts absolute paths in the pref to paths relative to the
  // install_directory_.
  void MakePathsRelative();

  // Converts internal relative paths to be absolute. Used for export to
  // consumers who expect full paths.
  void MakePathsAbsolute(base::DictionaryValue* dict);

  // Reads a boolean pref from |ext| with key |pref_key|.
  // Return false if the value is false or |pref_key| does not exist.
  static bool ReadBooleanFromPref(const base::DictionaryValue* ext,
                                  const std::string& pref_key);

  // Reads an integer pref from |ext| with key |pref_key|.
  // Return false if the value does not exist.
  static bool ReadIntegerFromPref(const base::DictionaryValue* ext,
                                  const std::string& pref_key,
                                  int* out_value);

  // Interprets the list pref, |pref_key| in |extension_id|'s preferences, as a
  // URLPatternSet. The |valid_schemes| specify how to parse the URLPatterns.
  bool ReadExtensionPrefURLPatternSet(const std::string& extension_id,
                                      const std::string& pref_key,
                                      URLPatternSet* result,
                                      int valid_schemes);

  // Converts |new_value| to a list of strings and sets the |pref_key| pref
  // belonging to |extension_id|.
  void SetExtensionPrefURLPatternSet(const std::string& extension_id,
                                     const std::string& pref_key,
                                     const URLPatternSet& new_value);

  // Interprets |pref_key| in |extension_id|'s preferences as an
  // PermissionSet, and passes ownership of the set to the caller.
  PermissionSet* ReadExtensionPrefPermissionSet(const std::string& extension_id,
                                                const std::string& pref_key);

  // Converts the |new_value| to its value and sets the |pref_key| pref
  // belonging to |extension_id|.
  void SetExtensionPrefPermissionSet(const std::string& extension_id,
                                     const std::string& pref_key,
                                     const PermissionSet* new_value);

  // Returns a dictionary for extension |id|'s prefs or NULL if it doesn't
  // exist.
  const base::DictionaryValue* GetExtensionPref(const std::string& id) const;

  // Loads the preferences controlled by the specified extension from their
  // dictionary and sets them in the |pref_value_map_|.
  void LoadExtensionControlledPrefs(const std::string& id,
                                    ExtensionPrefsScope scope);

  // Checks if kPrefBlacklist is set to true in the DictionaryValue.
  // Return false if the value is false or kPrefBlacklist does not exist.
  // This is used to decide if an extension is blacklisted.
  static bool IsBlacklistBitSet(const base::DictionaryValue* ext);

  // Fix missing preference entries in the extensions that are were introduced
  // in a later Chrome version.
  void FixMissingPrefs(const ExtensionIdSet& extension_ids);

  // Installs the persistent extension preferences into |prefs_|'s extension
  // pref store. Does nothing if |extensions_disabled| is true.
  void InitPrefStore(bool extensions_disabled);

  // Migrates the permissions data in the pref store.
  void MigratePermissions(const ExtensionIdSet& extension_ids);

  // Checks whether there is a state pref for the extension and if so, whether
  // it matches |check_state|.
  bool DoesExtensionHaveState(const std::string& id,
                              Extension::State check_state) const;

  // The pref service specific to this set of extension prefs. Owned by profile.
  PrefService* prefs_;

  // Base extensions install directory.
  FilePath install_directory_;

  // Weak pointer, owned by Profile.
  ExtensionPrefValueMap* extension_pref_value_map_;

  // Contains all the logic for handling the order for various extension
  // properties.
  scoped_ptr<ExtensionSorting> extension_sorting_;

  scoped_refptr<ContentSettingsStore> content_settings_store_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionPrefs);
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_PREFS_H_
