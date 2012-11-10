// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYNC_SYNCABLE_DIRECTORY_BACKING_STORE_H_
#define SYNC_SYNCABLE_DIRECTORY_BACKING_STORE_H_

#include <string>

#include "base/memory/scoped_ptr.h"
#include "base/threading/non_thread_safe.h"
#include "sql/connection.h"
#include "sql/statement.h"
#include "sync/internal_api/public/base/model_type.h"
#include "sync/syncable/dir_open_result.h"
#include "sync/syncable/directory.h"
#include "sync/syncable/metahandle_set.h"

namespace sync_pb {
class EntitySpecifics;
}

namespace syncer {
namespace syncable {

struct ColumnSpec;
typedef Directory::MetahandlesIndex MetahandlesIndex;

// Interface that provides persistence for a syncable::Directory object. You can
// load all the persisted data to prime a syncable::Directory on startup by
// invoking Load.  The only other thing you (or more correctly, a Directory) can
// do here is save any changes that have occurred since calling Load, which can
// be done periodically as often as desired.
//
// The DirectoryBackingStore will own an sqlite lock on its database for most of
// its lifetime.  You must not have two DirectoryBackingStore objects accessing
// the database simultaneously.  Because the lock exists at the database level,
// not even two separate browser instances would be able to acquire it
// simultaneously.
//
// This class is abstract so that we can extend it in interesting ways for use
// in tests.  The concrete class used in non-test scenarios is
// OnDiskDirectoryBackingStore.
class DirectoryBackingStore : public base::NonThreadSafe {
 public:
  explicit DirectoryBackingStore(const std::string& dir_name);
  virtual ~DirectoryBackingStore();

  // Loads and drops all currently persisted meta entries into |entry_bucket|
  // and loads appropriate persisted kernel info into |info_bucket|.
  //
  // This function can perform some cleanup tasks behind the scenes.  It will
  // clean up unused entries from the database and migrate to the latest
  // database version.  The caller can safely ignore these details.
  //
  // NOTE: On success (return value of OPENED), the buckets are populated with
  // newly allocated items, meaning ownership is bestowed upon the caller.
  virtual DirOpenResult Load(MetahandlesIndex* entry_bucket,
                             Directory::KernelLoadInfo* kernel_load_info) = 0;

  // Updates the on-disk store with the input |snapshot| as a database
  // transaction.  Does NOT open any syncable transactions as this would cause
  // opening transactions elsewhere to block on synchronous I/O.
  // DO NOT CALL THIS FROM MORE THAN ONE THREAD EVER.  Also, whichever thread
  // calls SaveChanges *must* be the thread that owns/destroys |this|.
  virtual bool SaveChanges(const Directory::SaveChangesSnapshot& snapshot);

 protected:
  // For test classes.
  DirectoryBackingStore(const std::string& dir_name,
                        sql::Connection* connection);

  // General Directory initialization and load helpers.
  bool InitializeTables();
  bool CreateTables();

  // Create 'share_info' or 'temp_share_info' depending on value of
  // is_temporary. Returns an sqlite
  bool CreateShareInfoTable(bool is_temporary);

  bool CreateShareInfoTableVersion71(bool is_temporary);
  // Create 'metas' or 'temp_metas' depending on value of is_temporary.
  bool CreateMetasTable(bool is_temporary);
  bool CreateModelsTable();
  bool CreateV71ModelsTable();

  // We don't need to load any synced and applied deleted entries, we can
  // in fact just purge them forever on startup.
  bool DropDeletedEntries();
  // Drops a table if it exists, harmless if the table did not already exist.
  bool SafeDropTable(const char* table_name);

  // Load helpers for entries and attributes.
  bool LoadEntries(MetahandlesIndex* entry_bucket);
  bool LoadInfo(Directory::KernelLoadInfo* info);

  // Save/update helpers for entries.  Return false if sqlite commit fails.
  bool SaveEntryToDB(const EntryKernel& entry);
  bool SaveNewEntryToDB(const EntryKernel& entry);
  bool UpdateEntryToDB(const EntryKernel& entry);

  DirOpenResult DoLoad(MetahandlesIndex* entry_bucket,
      Directory::KernelLoadInfo* kernel_load_info);

  // Close save_dbhandle_.  Broken out for testing.
  void EndSave();

  // Removes each entry whose metahandle is in |handles| from the database.
  // Does synchronous I/O.  Returns false on error.
  bool DeleteEntries(const MetahandleSet& handles);

  // Drop all tables in preparation for reinitialization.
  void DropAllTables();

  // Serialization helpers for ModelType.  These convert between
  // the ModelType enum and the values we persist in the database to identify
  // a model.  We persist a default instance of the specifics protobuf as the
  // ID, rather than the enum value.
  static ModelType ModelIdToModelTypeEnum(const void* data, int length);
  static std::string ModelTypeEnumToModelId(ModelType model_type);

  static std::string GenerateCacheGUID();

  // Runs an integrity check on the current database.  If the
  // integrity check fails, false is returned and error is populated
  // with an error message.
  bool CheckIntegrity(sqlite3* handle, std::string* error) const;

  // Migration utilities.
  bool RefreshColumns();
  bool SetVersion(int version);
  int GetVersion();

  bool MigrateToSpecifics(const char* old_columns,
                          const char* specifics_column,
                          void(*handler_function) (
                              sql::Statement* old_value_query,
                              int old_value_column,
                              sync_pb::EntitySpecifics* mutable_new_value));

  // Individual version migrations.
  bool MigrateVersion67To68();
  bool MigrateVersion68To69();
  bool MigrateVersion69To70();
  bool MigrateVersion70To71();
  bool MigrateVersion71To72();
  bool MigrateVersion72To73();
  bool MigrateVersion73To74();
  bool MigrateVersion74To75();
  bool MigrateVersion75To76();
  bool MigrateVersion76To77();
  bool MigrateVersion77To78();

  scoped_ptr<sql::Connection> db_;
  sql::Statement save_entry_statement_;
  std::string dir_name_;

  // Set to true if migration left some old columns around that need to be
  // discarded.
  bool needs_column_refresh_;

  DISALLOW_COPY_AND_ASSIGN(DirectoryBackingStore);
};

}  // namespace syncable
}  // namespace syncer

#endif  // SYNC_SYNCABLE_DIRECTORY_BACKING_STORE_H_
