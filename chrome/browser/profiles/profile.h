// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This class gathers state related to a single user profile.

#ifndef CHROME_BROWSER_PROFILES_PROFILE_H_
#define CHROME_BROWSER_PROFILES_PROFILE_H_

#include <string>

#include "base/basictypes.h"
#include "base/hash_tables.h"
#include "base/logging.h"
#include "chrome/browser/net/pref_proxy_config_tracker.h"
#include "chrome/browser/ui/webui/chrome_url_data_manager_factory.h"
#include "content/public/browser/browser_context.h"

class BookmarkModel;
class ChromeAppCacheService;
class ChromeURLDataManager;
class ExtensionProcessManager;
class ExtensionService;
class ExtensionSpecialStoragePolicy;
class FaviconService;
class GAIAInfoUpdateService;
class HistoryService;
class HostContentSettingsMap;
class PasswordStore;
class PrefService;
class PromoCounter;
class ProtocolHandlerRegistry;
class TestingProfile;
class VisitedLinkMaster;
class WebDataService;

namespace android {
class TabContentsProvider;
}

namespace base {
class Time;
}

namespace chrome_browser_net {
class Predictor;
}

namespace chromeos {
class LibCrosServiceLibraryImpl;
class ResetDefaultProxyConfigServiceTask;
}

namespace content {
class WebUI;
}

namespace extensions {
class EventRouter;
class UserScriptMaster;
}

namespace fileapi {
class FileSystemContext;
}

namespace history {
class TopSites;
class ShortcutsBackend;
}

namespace net {
class SSLConfigService;
}

namespace policy {
class PolicyService;
}

class Profile : public content::BrowserContext {
 public:
  // Profile services are accessed with the following parameter. This parameter
  // defines what the caller plans to do with the service.
  // The caller is responsible for not performing any operation that would
  // result in persistent implicit records while using an OffTheRecord profile.
  // This flag allows the profile to perform an additional check.
  //
  // It also gives us an opportunity to perform further checks in the future. We
  // could, for example, return an history service that only allow some specific
  // methods.
  enum ServiceAccessType {
    // The caller plans to perform a read or write that takes place as a result
    // of the user input. Use this flag when the operation you are doing can be
    // performed while incognito. (ex: creating a bookmark)
    //
    // Since EXPLICIT_ACCESS means "as a result of a user action", this request
    // always succeeds.
    EXPLICIT_ACCESS,

    // The caller plans to call a method that will permanently change some data
    // in the profile, as part of Chrome's implicit data logging. Use this flag
    // when you are about to perform an operation which is incompatible with the
    // incognito mode.
    IMPLICIT_ACCESS
  };

  enum CreateStatus {
    // Profile services were not created.
    CREATE_STATUS_FAIL,
    // Profile created but before initializing extensions and promo resources.
    CREATE_STATUS_CREATED,
    // Profile is created, extensions and promo resources are initialized.
    CREATE_STATUS_INITIALIZED,
  };

  enum CreateMode {
    CREATE_MODE_SYNCHRONOUS,
    CREATE_MODE_ASYNCHRONOUS
  };

  class Delegate {
   public:
    // Called when creation of the profile is finished.
    virtual void OnProfileCreated(Profile* profile,
                                  bool success,
                                  bool is_new_profile) = 0;
  };

  // Key used to bind profile to the widget with which it is associated.
  static const char* const kProfileKey;

  Profile();
  virtual ~Profile() {}

  // Profile prefs are registered as soon as the prefs are loaded for the first
  // time.
  static void RegisterUserPrefs(PrefService* prefs);

  // Create a new profile given a path. If |create_mode| is
  // CREATE_MODE_ASYNCHRONOUS then the profile is initialized asynchronously.
  static Profile* CreateProfile(const FilePath& path,
                                Delegate* delegate,
                                CreateMode create_mode);

  // Returns the profile corresponding to the given browser context.
  static Profile* FromBrowserContext(content::BrowserContext* browser_context);

  // Returns the profile corresponding to the given WebUI.
  static Profile* FromWebUI(content::WebUI* web_ui);

  // content::BrowserContext implementation ------------------------------------

  // Typesafe upcast.
  virtual TestingProfile* AsTestingProfile();

  // Returns the name associated with this profile. This name is displayed in
  // the browser frame.
  virtual std::string GetProfileName() = 0;

  // Return the incognito version of this profile. The returned pointer
  // is owned by the receiving profile. If the receiving profile is off the
  // record, the same profile is returned.
  //
  // WARNING: This will create the OffTheRecord profile if it doesn't already
  // exist. If this isn't what you want, you need to check
  // HasOffTheRecordProfile() first.
  virtual Profile* GetOffTheRecordProfile() = 0;

  // Destroys the incognito profile.
  virtual void DestroyOffTheRecordProfile() = 0;

  // True if an incognito profile exists.
  virtual bool HasOffTheRecordProfile() = 0;

  // Return the original "recording" profile. This method returns this if the
  // profile is not incognito.
  virtual Profile* GetOriginalProfile() = 0;

  // Returns a pointer to the TopSites (thumbnail manager) instance
  // for this profile.
  virtual history::TopSites* GetTopSites() = 0;

  // Variant of GetTopSites that doesn't force creation.
  virtual history::TopSites* GetTopSitesWithoutCreating() = 0;

  // Retrieves a pointer to the VisitedLinkMaster associated with this
  // profile.  The VisitedLinkMaster is lazily created the first time
  // that this method is called.
  virtual VisitedLinkMaster* GetVisitedLinkMaster() = 0;

  // DEPRECATED. Instead, use ExtensionSystem::extension_service().
  // Retrieves a pointer to the ExtensionService associated with this
  // profile. The ExtensionService is created at startup.
  // TODO(yoz): remove this accessor (bug 104095).
  virtual ExtensionService* GetExtensionService() = 0;

  // DEPRECATED. Instead, use ExtensionSystem::user_script_master().
  // Retrieves a pointer to the extensions::UserScriptMaster associated with
  // this profile.  The extensions::UserScriptMaster is lazily created the first
  // time that this method is called.
  // TODO(yoz): remove this accessor (bug 104095).
  virtual extensions::UserScriptMaster* GetUserScriptMaster() = 0;

  // DEPRECATED. Instead, use ExtensionSystem::process_manager().
  // Retrieves a pointer to the ExtensionProcessManager associated with this
  // profile.  The instance is created at startup.
  // TODO(yoz): remove this accessor (bug 104095).
  virtual ExtensionProcessManager* GetExtensionProcessManager() = 0;

  // DEPRECATED. Instead, use ExtensionSystem::event_router().
  // Accessor. The instance is created at startup.
  // TODO(yoz): remove this accessor (bug 104095).
  virtual extensions::EventRouter* GetExtensionEventRouter() = 0;

  // Accessor. The instance is created upon first access.
  virtual ExtensionSpecialStoragePolicy*
      GetExtensionSpecialStoragePolicy() = 0;

  // Retrieves a pointer to the FaviconService associated with this
  // profile.  The FaviconService is lazily created the first time
  // that this method is called.
  //
  // Although FaviconService is refcounted, this will not addref, and callers
  // do not need to do any reference counting as long as they keep the pointer
  // only for the local scope (which they should do anyway since the browser
  // process may decide to shut down).
  //
  // |access| defines what the caller plans to do with the service. See
  // the ServiceAccessType definition above.
  virtual FaviconService* GetFaviconService(ServiceAccessType access) = 0;

  // Accessor. The instance is created upon first access.
  virtual GAIAInfoUpdateService* GetGAIAInfoUpdateService() = 0;

  // Retrieves a pointer to the HistoryService associated with this
  // profile.  The HistoryService is lazily created the first time
  // that this method is called.
  //
  // Although HistoryService is refcounted, this will not addref, and callers
  // do not need to do any reference counting as long as they keep the pointer
  // only for the local scope (which they should do anyway since the browser
  // process may decide to shut down).
  //
  // |access| defines what the caller plans to do with the service. See
  // the ServiceAccessType definition above.
  virtual HistoryService* GetHistoryService(ServiceAccessType access) = 0;

  // Similar to GetHistoryService(), but won't create the history service if it
  // doesn't already exist.
  virtual HistoryService* GetHistoryServiceWithoutCreating() = 0;

  // Returns the PolicyService that provides policies for this profile.
  virtual policy::PolicyService* GetPolicyService() = 0;

  // Retrieves a pointer to the PrefService that manages the preferences
  // for this user profile.
  virtual PrefService* GetPrefs() = 0;

  // Retrieves a pointer to the PrefService that manages the preferences
  // for OffTheRecord Profiles.  This PrefService is lazily created the first
  // time that this method is called.
  virtual PrefService* GetOffTheRecordPrefs() = 0;

  // Returns the main request context.
  virtual net::URLRequestContextGetter* GetRequestContext() = 0;

  // Returns the request context used for extension-related requests.  This
  // is only used for a separate cookie store currently.
  virtual net::URLRequestContextGetter* GetRequestContextForExtensions() = 0;

  // Returns the request context used within an installed app that has
  // requested isolated storage.
  virtual net::URLRequestContextGetter* GetRequestContextForIsolatedApp(
      const std::string& app_id) = 0;

  // Returns the SSLConfigService for this profile.
  virtual net::SSLConfigService* GetSSLConfigService() = 0;

  // Returns the Hostname <-> Content settings map for this profile.
  virtual HostContentSettingsMap* GetHostContentSettingsMap() = 0;

  // Returns the BookmarkModel, creating if not yet created.
  virtual BookmarkModel* GetBookmarkModel() = 0;

  // Returns the ProtocolHandlerRegistry, creating if not yet created.
  // TODO(smckay): replace this with access via ProtocolHandlerRegistryFactory.
  virtual ProtocolHandlerRegistry* GetProtocolHandlerRegistry() = 0;

  // Return whether 2 profiles are the same. 2 profiles are the same if they
  // represent the same profile. This can happen if there is pointer equality
  // or if one profile is the incognito version of another profile (or vice
  // versa).
  virtual bool IsSameProfile(Profile* profile) = 0;

  // Returns the time the profile was started. This is not the time the profile
  // was created, rather it is the time the user started chrome and logged into
  // this profile. For the single profile case, this corresponds to the time
  // the user started chrome.
  virtual base::Time GetStartTime() const = 0;

  // Marks the profile as cleanly shutdown.
  //
  // NOTE: this is invoked internally on a normal shutdown, but is public so
  // that it can be invoked when the user logs out/powers down (WM_ENDSESSION).
  virtual void MarkAsCleanShutdown() = 0;

  // Start up service that gathers data from a promo resource feed.
  virtual void InitPromoResources() = 0;

  // Returns the last directory that was chosen for uploading or opening a file.
  virtual FilePath last_selected_directory() = 0;
  virtual void set_last_selected_directory(const FilePath& path) = 0;

#if defined(OS_CHROMEOS)
  enum AppLocaleChangedVia {
    // Caused by chrome://settings change.
    APP_LOCALE_CHANGED_VIA_SETTINGS,
    // Locale has been reverted via LocaleChangeGuard.
    APP_LOCALE_CHANGED_VIA_REVERT,
    // From login screen.
    APP_LOCALE_CHANGED_VIA_LOGIN,
    // Source unknown.
    APP_LOCALE_CHANGED_VIA_UNKNOWN
  };

  // Changes application locale for a profile.
  virtual void ChangeAppLocale(
      const std::string& locale, AppLocaleChangedVia via) = 0;

  // Called after login.
  virtual void OnLogin() = 0;

  // Creates ChromeOS's EnterpriseExtensionListener.
  virtual void SetupChromeOSEnterpriseExtensionObserver() = 0;

  // Initializes Chrome OS's preferences.
  virtual void InitChromeOSPreferences() = 0;
#endif  // defined(OS_CHROMEOS)

  // Returns the helper object that provides the proxy configuration service
  // access to the the proxy configuration possibly defined by preferences.
  virtual PrefProxyConfigTracker* GetProxyConfigTracker() = 0;

  // Returns the Predictor object used for dns prefetch.
  virtual chrome_browser_net::Predictor* GetNetworkPredictor() = 0;

  // Deletes all network related data since |time|. It deletes transport
  // security state since |time| and it also delete HttpServerProperties data.
  // The implementation is free to run this on a background thread, so when this
  // method returns data is not guaranteed to be deleted.
  virtual void ClearNetworkingHistorySince(base::Time time) = 0;

  // Returns the home page for this profile.
  virtual GURL GetHomePage() = 0;

  // Returns whether or not the profile was created by a version of Chrome
  // more recent (or equal to) the one specified.
  virtual bool WasCreatedByVersionOrLater(const std::string& version) = 0;

  std::string GetDebugName();

  // Returns whether it is a guest session.
  static bool IsGuestSession();

  // Did the user restore the last session? This is set by SessionRestore.
  void set_restored_last_session(bool restored_last_session) {
    restored_last_session_ = restored_last_session;
  }
  bool restored_last_session() const {
    return restored_last_session_;
  }

  // Stop sending accessibility events until ResumeAccessibilityEvents().
  // Calls to Pause nest; no events will be sent until the number of
  // Resume calls matches the number of Pause calls received.
  void PauseAccessibilityEvents() {
    accessibility_pause_level_++;
  }

  void ResumeAccessibilityEvents() {
    DCHECK_GT(accessibility_pause_level_, 0);
    accessibility_pause_level_--;
  }

  bool ShouldSendAccessibilityEvents() {
    return 0 == accessibility_pause_level_;
  }

  // Checks whether sync is configurable by the user. Returns false if sync is
  // disabled or controlled by configuration management.
  bool IsSyncAccessible();

  // Send NOTIFICATION_PROFILE_DESTROYED for this Profile, if it has not
  // already been sent. It is necessary because most Profiles are destroyed by
  // ProfileDestroyer, but in tests, some are not.
  void MaybeSendDestroyedNotification();

  // Creates an OffTheRecordProfile which points to this Profile.
  Profile* CreateOffTheRecordProfile();

 protected:
  // TODO(erg, willchan): Remove friendship once |ProfileIOData| is made into
  //     a |ProfileKeyedService|.
  friend class ChromeURLDataManagerFactory;
  friend class OffTheRecordProfileImpl;

  // Returns a callback to a method returning a |ChromeURLDataManagerBackend|.
  // Used to create a |ChromeURLDataManager| for this |Profile|.
  // TODO(erg, willchan): Remove this once |ProfileIOData| is made into a
  //     |ProfileKeyedService|.
  virtual base::Callback<ChromeURLDataManagerBackend*(void)>
      GetChromeURLDataManagerBackendGetter() const = 0;

 private:
  bool restored_last_session_;

  // Used to prevent the notification that this Profile is destroyed from
  // being sent twice.
  bool sent_destroyed_notification_;

  // Accessibility events will only be propagated when the pause
  // level is zero.  PauseAccessibilityEvents and ResumeAccessibilityEvents
  // increment and decrement the level, respectively, rather than set it to
  // true or false, so that calls can be nested.
  int accessibility_pause_level_;
};

#if defined(COMPILER_GCC)
namespace BASE_HASH_NAMESPACE {

template<>
struct hash<Profile*> {
  std::size_t operator()(Profile* const& p) const {
    return reinterpret_cast<std::size_t>(p);
  }
};

}  // namespace BASE_HASH_NAMESPACE
#endif

#endif  // CHROME_BROWSER_PROFILES_PROFILE_H_
