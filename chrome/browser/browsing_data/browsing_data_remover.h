// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_BROWSING_DATA_BROWSING_DATA_REMOVER_H_
#define CHROME_BROWSER_BROWSING_DATA_BROWSING_DATA_REMOVER_H_

#include <set>

#include "base/gtest_prod_util.h"
#include "base/memory/ref_counted.h"
#include "base/observer_list.h"
#include "base/sequenced_task_runner_helpers.h"
#include "base/synchronization/waitable_event_watcher.h"
#include "base/time.h"
#include "chrome/browser/cancelable_request.h"
#include "chrome/browser/pepper_flash_settings_manager.h"
#include "chrome/browser/prefs/pref_member.h"
#include "content/public/browser/dom_storage_context.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "googleurl/src/gurl.h"
#include "webkit/dom_storage/dom_storage_context.h"
#include "webkit/quota/quota_types.h"

class ExtensionSpecialStoragePolicy;
class IOThread;
class Profile;

namespace content {
class PluginDataRemover;
}

namespace disk_cache {
class Backend;
}

namespace net {
class URLRequestContextGetter;
}

namespace quota {
class QuotaManager;
}

// BrowsingDataRemover is responsible for removing data related to browsing:
// visits in url database, downloads, cookies ...

class BrowsingDataRemover : public content::NotificationObserver,
                            public base::WaitableEventWatcher::Delegate,
                            public PepperFlashSettingsManager::Client {
 public:
  // Time period ranges available when doing browsing data removals.
  enum TimePeriod {
    LAST_HOUR = 0,
    LAST_DAY,
    LAST_WEEK,
    FOUR_WEEKS,
    EVERYTHING
  };

  // Mask used for Remove.
  enum RemoveDataMask {
    REMOVE_APPCACHE = 1 << 0,
    REMOVE_CACHE = 1 << 1,
    REMOVE_COOKIES = 1 << 2,
    REMOVE_DOWNLOADS = 1 << 3,
    REMOVE_FILE_SYSTEMS = 1 << 4,
    REMOVE_FORM_DATA = 1 << 5,
    // In addition to visits, REMOVE_HISTORY removes keywords and last session.
    REMOVE_HISTORY = 1 << 6,
    REMOVE_INDEXEDDB = 1 << 7,
    REMOVE_LOCAL_STORAGE = 1 << 8,
    REMOVE_PLUGIN_DATA = 1 << 9,
    REMOVE_PASSWORDS = 1 << 10,
    REMOVE_WEBSQL = 1 << 11,
    REMOVE_SERVER_BOUND_CERTS = 1 << 12,
    REMOVE_CONTENT_LICENSES = 1 << 13,

    // "Site data" includes cookies, appcache, file systems, indexedDBs, local
    // storage, webSQL, and plugin data.
    REMOVE_SITE_DATA = REMOVE_APPCACHE | REMOVE_COOKIES | REMOVE_FILE_SYSTEMS |
                       REMOVE_INDEXEDDB | REMOVE_LOCAL_STORAGE |
                       REMOVE_PLUGIN_DATA | REMOVE_WEBSQL |
                       REMOVE_SERVER_BOUND_CERTS
  };

  // When BrowsingDataRemover successfully removes data, a notification of type
  // NOTIFICATION_BROWSING_DATA_REMOVED is triggered with a Details object of
  // this type.
  struct NotificationDetails {
    NotificationDetails();
    NotificationDetails(const NotificationDetails& details);
    NotificationDetails(base::Time removal_begin,
                       int removal_mask,
                       int origin_set_mask);
    ~NotificationDetails();

    // The beginning of the removal time range.
    base::Time removal_begin;

    // The removal mask (see the RemoveDataMask enum for details).
    int removal_mask;

    // The origin set mask (see BrowsingDataHelper::OriginSetMask for details).
    int origin_set_mask;
  };

  // Observer is notified when the removal is done. Done means keywords have
  // been deleted, cache cleared and all other tasks scheduled.
  class Observer {
   public:
    virtual void OnBrowsingDataRemoverDone() = 0;

   protected:
    virtual ~Observer() {}
  };

  // Creates a BrowsingDataRemover to remove browser data from the specified
  // profile in the specified time range. Use Remove to initiate the removal.
  BrowsingDataRemover(Profile* profile, base::Time delete_begin,
                      base::Time delete_end);

  // Creates a BrowsingDataRemover to remove browser data from the specified
  // profile in the specified time range.
  BrowsingDataRemover(Profile* profile, TimePeriod time_period,
                      base::Time delete_end);

  // Removes the specified items related to browsing for all origins that match
  // the provided |origin_set_mask| (see BrowsingDataHelper::OriginSetMask).
  void Remove(int remove_mask, int origin_set_mask);

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Called when history deletion is done.
  void OnHistoryDeletionDone();

  // Quota managed data uses a different bitmask for types than
  // BrowsingDataRemover uses. This method generates that mask.
  static int GenerateQuotaClientMask(int remove_mask);

  // Used for testing.
  void OverrideQuotaManagerForTesting(quota::QuotaManager* quota_manager);

  static bool is_removing() { return removing_; }

 private:
  // The clear API needs to be able to toggle removing_ in order to test that
  // only one BrowsingDataRemover instance can be called at a time.
  FRIEND_TEST_ALL_PREFIXES(ExtensionBrowsingDataTest, OneAtATime);

  // The BrowsingDataRemover tests need to be able to access the implementation
  // of Remove(), as it exposes details that aren't yet available in the public
  // API. As soon as those details are exposed via new methods, this should be
  // removed.
  //
  // TODO(mkwst): See http://crbug.com/113621
  friend class BrowsingDataRemoverTest;

  enum CacheState {
    STATE_NONE,
    STATE_CREATE_MAIN,
    STATE_CREATE_MEDIA,
    STATE_DELETE_MAIN,
    STATE_DELETE_MEDIA,
    STATE_DONE
  };

  // BrowsingDataRemover deletes itself (using DeleteHelper) and is not supposed
  // to be deleted by other objects so make destructor private and DeleteHelper
  // a friend.
  friend class base::DeleteHelper<BrowsingDataRemover>;
  virtual ~BrowsingDataRemover();

  // content::NotificationObserver method. Callback when TemplateURLService has
  // finished loading. Deletes the entries from the model, and if we're not
  // waiting on anything else notifies observers and deletes this
  // BrowsingDataRemover.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // WaitableEventWatcher implementation.
  // Called when plug-in data has been cleared. Invokes NotifyAndDeleteIfDone.
  virtual void OnWaitableEventSignaled(
      base::WaitableEvent* waitable_event) OVERRIDE;

  // PepperFlashSettingsManager::Client implementation.
  virtual void OnDeauthorizeContentLicensesCompleted(uint32 request_id,
                                                     bool success) OVERRIDE;

  // Removes the specified items related to browsing for a specific host. If the
  // provided |origin| is empty, data is removed for all origins. The
  // |origin_set_mask| parameter defines the set of origins from which data
  // should be removed (protected, unprotected, or both).
  void RemoveImpl(int remove_mask,
                  const GURL& origin,
                  int origin_set_mask);

  // If we're not waiting on anything, notifies observers and deletes this
  // object.
  void NotifyAndDeleteIfDone();

  // Callback when the network history has been deleted. Invokes
  // NotifyAndDeleteIfDone.
  void ClearedNetworkHistory();

  // Invoked on the IO thread to clear the HostCache, speculative data about
  // subresources on visited sites, and initial navigation history.
  void ClearNetworkingHistory(IOThread* io_thread);

  // Callback when the cache has been deleted. Invokes NotifyAndDeleteIfDone.
  void ClearedCache();

  // Invoked on the IO thread to delete from the cache.
  void ClearCacheOnIOThread();

  // Performs the actual work to delete the cache.
  void DoClearCache(int rv);

#if !defined(DISABLE_NACL)
  // Callback for when the NaCl cache has been deleted. Invokes
  // NotifyAndDeleteIfDone.
  void ClearedNaClCache();

  // Invokes the ClearedNaClCache on the UI thread.
  void ClearedNaClCacheOnIOThread();

  // Invoked on the IO thread to delete the NaCl cache.
  void ClearNaClCacheOnIOThread();
#endif

  // Invoked on the UI thread to delete local storage.
  void ClearLocalStorageOnUIThread();

  // Callback to deal with the list gathered in ClearLocalStorageOnUIThread.
  void OnGotLocalStorageUsageInfo(
      const std::vector<dom_storage::DomStorageContext::UsageInfo>& infos);

  // Callback on deletion of local storage data. Invokes NotifyAndDeleteIfDone.
  void OnLocalStorageCleared();

  // Invoked on the IO thread to delete all storage types managed by the quota
  // system: AppCache, Databases, FileSystems.
  void ClearQuotaManagedDataOnIOThread();

  // Callback to respond to QuotaManager::GetOriginsModifiedSince, which is the
  // core of 'ClearQuotaManagedDataOnIOThread'.
  void OnGotQuotaManagedOrigins(const std::set<GURL>& origins,
                                quota::StorageType type);

  // Callback responding to deletion of a single quota managed origin's
  // persistent data
  void OnQuotaManagedOriginDeletion(const GURL& origin,
                                    quota::StorageType type,
                                    quota::QuotaStatusCode);

  // Called to check whether all temporary and persistent origin data that
  // should be deleted has been deleted. If everything's good to go, invokes
  // OnQuotaManagedDataDeleted on the UI thread.
  void CheckQuotaManagedDataDeletionStatus();

  // Completion handler that runs on the UI thread once persistent data has been
  // deleted. Updates the waiting flag and invokes NotifyAndDeleteIfDone.
  void OnQuotaManagedDataDeleted();

  // Callback when Cookies has been deleted. Invokes NotifyAndDeleteIfDone.
  void OnClearedCookies(int num_deleted);

  // Invoked on the IO thread to delete cookies.
  void ClearCookiesOnIOThread(net::URLRequestContextGetter* rq_context);

  // Invoked on the IO thread to delete server bound certs.
  void ClearServerBoundCertsOnIOThread(
      net::URLRequestContextGetter* rq_context);

  // Callback when server bound certs have been deleted. Invokes
  // NotifyAndDeleteIfDone.
  void OnClearedServerBoundCerts();

  // Calculate the begin time for the deletion range specified by |time_period|.
  base::Time CalculateBeginDeleteTime(TimePeriod time_period);

  // Returns true if we're all done.
  bool AllDone();

  // Setter for removing_; DCHECKs that we can only start removing if we're not
  // already removing, and vice-versa.
  static void set_removing(bool removing);

  content::NotificationRegistrar registrar_;

  // Profile we're to remove from.
  Profile* profile_;

  // The QuotaManager is owned by the profile; we can use a raw pointer here,
  // and rely on the profile to destroy the object whenever it's reasonable.
  quota::QuotaManager* quota_manager_;

  // The DOMStorageContext is owned by the profile; we'll store a raw pointer.
  content::DOMStorageContext* dom_storage_context_;

  // 'Protected' origins are not subject to data removal.
  scoped_refptr<ExtensionSpecialStoragePolicy> special_storage_policy_;

  // Start time to delete from.
  const base::Time delete_begin_;

  // End time to delete to.
  base::Time delete_end_;

  // True if Remove has been invoked.
  static bool removing_;

  CacheState next_cache_state_;
  disk_cache::Backend* cache_;

  // Used to delete data from HTTP cache.
  scoped_refptr<net::URLRequestContextGetter> main_context_getter_;
  scoped_refptr<net::URLRequestContextGetter> media_context_getter_;

  // Used to delete plugin data.
  scoped_ptr<content::PluginDataRemover> plugin_data_remover_;
  base::WaitableEventWatcher watcher_;

  // Used to deauthorize content licenses for Pepper Flash.
  scoped_ptr<PepperFlashSettingsManager> pepper_flash_settings_manager_;
  uint32 deauthorize_content_licenses_request_id_;

  // True if we're waiting for various data to be deleted.
  // These may only be accessed from UI thread in order to avoid races!
  bool waiting_for_clear_cache_;
  bool waiting_for_clear_nacl_cache_;
  // Non-zero if waiting for cookies to be cleared.
  int waiting_for_clear_cookies_count_;
  bool waiting_for_clear_history_;
  bool waiting_for_clear_local_storage_;
  bool waiting_for_clear_networking_history_;
  bool waiting_for_clear_server_bound_certs_;
  bool waiting_for_clear_plugin_data_;
  bool waiting_for_clear_quota_managed_data_;
  bool waiting_for_clear_content_licenses_;

  // Tracking how many origins need to be deleted, and whether we're finished
  // gathering origins.
  int quota_managed_origins_to_delete_count_;
  int quota_managed_storage_types_to_delete_count_;

  // The removal mask for the current removal operation.
  int remove_mask_;

  // The origin for the current removal operation.
  GURL remove_origin_;

  // From which types of origins should we remove data?
  int origin_set_mask_;

  ObserverList<Observer> observer_list_;

  // Used if we need to clear history.
  CancelableRequestConsumer request_consumer_;

  DISALLOW_COPY_AND_ASSIGN(BrowsingDataRemover);
};

#endif  // CHROME_BROWSER_BROWSING_DATA_BROWSING_DATA_REMOVER_H_
