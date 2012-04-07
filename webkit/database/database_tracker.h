// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_DATABASE_DATABASE_TRACKER_H_
#define WEBKIT_DATABASE_DATABASE_TRACKER_H_

#include <map>
#include <set>
#include <utility>

#include "base/file_path.h"
#include "base/gtest_prod_util.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/observer_list.h"
#include "base/platform_file.h"
#include "base/string16.h"
#include "base/string_util.h"
#include "base/time.h"
#include "net/base/completion_callback.h"
#include "webkit/database/database_connections.h"

namespace base {
class MessageLoopProxy;
}

namespace sql {
class Connection;
class MetaTable;
}

namespace quota {
class QuotaManagerProxy;
class SpecialStoragePolicy;
}

namespace webkit_database {

extern const FilePath::CharType kDatabaseDirectoryName[];
extern const FilePath::CharType kTrackerDatabaseFileName[];

class DatabasesTable;

// This class is used to store information about all databases in an origin.
class OriginInfo {
 public:
  OriginInfo();
  OriginInfo(const OriginInfo& origin_info);
  ~OriginInfo();

  const string16& GetOrigin() const { return origin_; }
  int64 TotalSize() const { return total_size_; }
  void GetAllDatabaseNames(std::vector<string16>* databases) const;
  int64 GetDatabaseSize(const string16& database_name) const;
  string16 GetDatabaseDescription(const string16& database_name) const;

 protected:
  typedef std::map<string16, std::pair<int64, string16> > DatabaseInfoMap;

  OriginInfo(const string16& origin, int64 total_size);

  string16 origin_;
  int64 total_size_;
  DatabaseInfoMap database_info_;
};

// This class manages the main database and keeps track of open databases.
//
// The data in this class is not thread-safe, so all methods of this class
// should be called on the same thread. The only exceptions are the ctor(),
// the dtor() and the database_directory() and quota_manager_proxy() getters.
//
// Furthermore, some methods of this class have to read/write data from/to
// the disk. Therefore, in a multi-threaded application, all methods of this
// class should be called on the thread dedicated to file operations (file
// thread in the browser process, for example), if such a thread exists.
class DatabaseTracker
    : public base::RefCountedThreadSafe<DatabaseTracker> {
 public:
  class Observer {
   public:
    virtual void OnDatabaseSizeChanged(const string16& origin_identifier,
                                       const string16& database_name,
                                       int64 database_size) = 0;
    virtual void OnDatabaseScheduledForDeletion(
        const string16& origin_identifier,
        const string16& database_name) = 0;
    virtual ~Observer() {}
  };

  DatabaseTracker(const FilePath& profile_path,
                  bool is_incognito,
                  bool clear_local_state_on_exit,
                  quota::SpecialStoragePolicy* special_storage_policy,
                  quota::QuotaManagerProxy* quota_manager_proxy,
                  base::MessageLoopProxy* db_tracker_thread);

  void DatabaseOpened(const string16& origin_identifier,
                      const string16& database_name,
                      const string16& database_details,
                      int64 estimated_size,
                      int64* database_size);
  void DatabaseModified(const string16& origin_identifier,
                        const string16& database_name);
  void DatabaseClosed(const string16& origin_identifier,
                      const string16& database_name);
  void CloseDatabases(const DatabaseConnections& connections);

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  void CloseTrackerDatabaseAndClearCaches();

  const FilePath& DatabaseDirectory() const { return db_dir_; }
  FilePath GetFullDBFilePath(const string16& origin_identifier,
                             const string16& database_name);

  // virtual for unit-testing only
  virtual bool GetOriginInfo(const string16& origin_id, OriginInfo* info);
  virtual bool GetAllOriginIdentifiers(std::vector<string16>* origin_ids);
  virtual bool GetAllOriginsInfo(std::vector<OriginInfo>* origins_info);

  // Safe to call on any thread.
  quota::QuotaManagerProxy* quota_manager_proxy() const {
    return quota_manager_proxy_.get();
  }

  bool IsDatabaseScheduledForDeletion(const string16& origin_identifier,
                                      const string16& database_name);

  // Deletes a single database. Returns net::OK on success, net::FAILED on
  // failure, or net::ERR_IO_PENDING and |callback| is invoked upon completion,
  // if non-NULL.
  int DeleteDatabase(const string16& origin_identifier,
                     const string16& database_name,
                     const net::CompletionCallback& callback);

  // Delete any databases that have been touched since the cutoff date that's
  // supplied, omitting any that match IDs within |protected_origins|.
  // Returns net::OK on success, net::FAILED if not all databases could be
  // deleted, and net::ERR_IO_PENDING and |callback| is invoked upon completion,
  // if non-NULL. Protected origins, according the the SpecialStoragePolicy,
  // are not deleted by this method.
  int DeleteDataModifiedSince(const base::Time& cutoff,
                              const net::CompletionCallback& callback);

  // Delete all databases that belong to the given origin. Returns net::OK on
  // success, net::FAILED if not all databases could be deleted, and
  // net::ERR_IO_PENDING and |callback| is invoked upon completion, if non-NULL.
  // virtual for unit testing only
  virtual int DeleteDataForOrigin(const string16& origin_identifier,
                                  const net::CompletionCallback& callback);

  bool IsIncognitoProfile() const { return is_incognito_; }

  void GetIncognitoFileHandle(const string16& vfs_file_path,
                              base::PlatformFile* file_handle) const;
  void SaveIncognitoFileHandle(const string16& vfs_file_path,
                               const base::PlatformFile& file_handle);
  bool CloseIncognitoFileHandle(const string16& vfs_file_path);
  bool HasSavedIncognitoFileHandle(const string16& vfs_file_path) const;

  // Shutdown the database tracker, deleting database files if the tracker is
  // used for an incognito profile or |clear_local_state_on_exit_| is true.
  void Shutdown();
  void SetClearLocalStateOnExit(bool clear_local_state_on_exit);
  // Disables the exit-time deletion for all data (also session-only data).
  void SaveSessionState();

 private:
  friend class base::RefCountedThreadSafe<DatabaseTracker>;
  friend class MockDatabaseTracker;  // for testing

  typedef std::map<string16, std::set<string16> > DatabaseSet;
  typedef std::vector<std::pair<net::CompletionCallback, DatabaseSet> >
      PendingDeletionCallbacks;
  typedef std::map<string16, base::PlatformFile> FileHandlesMap;
  typedef std::map<string16, string16> OriginDirectoriesMap;

  class CachedOriginInfo : public OriginInfo {
   public:
    CachedOriginInfo() : OriginInfo(string16(), 0) {}
    void SetOrigin(const string16& origin) { origin_ = origin; }
    void SetDatabaseSize(const string16& database_name, int64 new_size) {
      int64 old_size = 0;
      if (database_info_.find(database_name) != database_info_.end())
        old_size = database_info_[database_name].first;
      database_info_[database_name].first = new_size;
      if (new_size != old_size)
        total_size_ += new_size - old_size;
    }
    void SetDatabaseDescription(const string16& database_name,
                                const string16& description) {
      database_info_[database_name].second = description;
    }
  };

  // virtual for unit-testing only.
  virtual ~DatabaseTracker();

  // Deletes the directory that stores all DBs in incognito mode, if it exists.
  void DeleteIncognitoDBDirectory();

  // If clear_all_databases is true, deletes all databases not protected by
  // special storage policy. Otherwise deletes session-only databases. Blocks
  // databases from being created/opened.
  void ClearLocalState(bool clear_all_databases);

  bool DeleteClosedDatabase(const string16& origin_identifier,
                            const string16& database_name);

  // Delete all files belonging to the given origin given that no database
  // connections within this origin are open, or if |force| is true, delete
  // the meta data and rename the associated directory.
  bool DeleteOrigin(const string16& origin_identifier, bool force);
  void DeleteDatabaseIfNeeded(const string16& origin_identifier,
                              const string16& database_name);

  bool LazyInit();
  bool UpgradeToCurrentVersion();
  void InsertOrUpdateDatabaseDetails(const string16& origin_identifier,
                                     const string16& database_name,
                                     const string16& database_details,
                                     int64 estimated_size);

  void ClearAllCachedOriginInfo();
  CachedOriginInfo* MaybeGetCachedOriginInfo(const string16& origin_identifier,
                                             bool create_if_needed);
  CachedOriginInfo* GetCachedOriginInfo(const string16& origin_identifier) {
    return MaybeGetCachedOriginInfo(origin_identifier, true);
  }

  int64 GetDBFileSize(const string16& origin_identifier,
                      const string16& database_name);
  int64 SeedOpenDatabaseInfo(const string16& origin_identifier,
                             const string16& database_name,
                             const string16& description);
  int64 UpdateOpenDatabaseInfoAndNotify(const string16& origin_identifier,
                                        const string16& database_name,
                                        const string16* opt_description);
  int64 UpdateOpenDatabaseSizeAndNotify(const string16& origin_identifier,
                                        const string16& database_name) {
    return UpdateOpenDatabaseInfoAndNotify(
        origin_identifier, database_name, NULL);
  }


  void ScheduleDatabaseForDeletion(const string16& origin_identifier,
                                   const string16& database_name);
  // Schedule a set of open databases for deletion. If non-null, callback is
  // invoked upon completion.
  void ScheduleDatabasesForDeletion(const DatabaseSet& databases,
                                    const net::CompletionCallback& callback);

  // Returns the directory where all DB files for the given origin are stored.
  string16 GetOriginDirectory(const string16& origin_identifier);

  bool is_initialized_;
  const bool is_incognito_;
  bool clear_local_state_on_exit_;
  bool save_session_state_;
  bool shutting_down_;
  const FilePath profile_path_;
  const FilePath db_dir_;
  scoped_ptr<sql::Connection> db_;
  scoped_ptr<DatabasesTable> databases_table_;
  scoped_ptr<sql::MetaTable> meta_table_;
  ObserverList<Observer, true> observers_;
  std::map<string16, CachedOriginInfo> origins_info_map_;
  DatabaseConnections database_connections_;

  // The set of databases that should be deleted but are still opened
  DatabaseSet dbs_to_be_deleted_;
  PendingDeletionCallbacks deletion_callbacks_;

  // Apps and Extensions can have special rights.
  scoped_refptr<quota::SpecialStoragePolicy> special_storage_policy_;

  scoped_refptr<quota::QuotaManagerProxy> quota_manager_proxy_;

  // The database tracker thread we're supposed to run file IO on.
  scoped_refptr<base::MessageLoopProxy> db_tracker_thread_;

  // When in incognito mode, store a DELETE_ON_CLOSE handle to each
  // main DB and journal file that was accessed. When the incognito profile
  // goes away (or when the browser crashes), all these handles will be
  // closed, and the files will be deleted.
  FileHandlesMap incognito_file_handles_;

  // In a non-incognito profile, all DBs in an origin are stored in a directory
  // named after the origin. In an incognito profile though, we do not want the
  // directory structure to reveal the origins visited by the user (in case the
  // browser process crashes and those directories are not deleted). So we use
  // this map to assign directory names that do not reveal this information.
  OriginDirectoriesMap incognito_origin_directories_;
  int incognito_origin_directories_generator_;

  FRIEND_TEST_ALL_PREFIXES(DatabaseTracker, TestHelper);
};

}  // namespace webkit_database

#endif  // WEBKIT_DATABASE_DATABASE_TRACKER_H_
