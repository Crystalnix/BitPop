// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_DRIVE_DRIVE_TEST_UTIL_H_
#define CHROME_BROWSER_CHROMEOS_DRIVE_DRIVE_TEST_UTIL_H_

#include "base/memory/scoped_ptr.h"
#include "chrome/browser/chromeos/drive/drive_resource_metadata.h"
#include "chrome/browser/google_apis/gdata_errorcode.h"
#include "chrome/browser/google_apis/test_util.h"

class FilePath;

namespace base {
class Value;
}

namespace drive {

class DriveCacheEntry;
class DriveEntryProto;
class DriveFileSystem;

typedef std::vector<DriveEntryProto> DriveEntryProtoVector;

namespace test_util {

// This is a bitmask of cache states in DriveCacheEntry. Used only in tests.
enum TestDriveCacheState {
  TEST_CACHE_STATE_NONE       = 0,
  TEST_CACHE_STATE_PINNED     = 1 << 0,
  TEST_CACHE_STATE_PRESENT    = 1 << 1,
  TEST_CACHE_STATE_DIRTY      = 1 << 2,
  TEST_CACHE_STATE_MOUNTED    = 1 << 3,
  TEST_CACHE_STATE_PERSISTENT = 1 << 4,
};

// Converts |cache_state| which is a bit mask of TestDriveCacheState, to a
// DriveCacheEntry.
DriveCacheEntry ToCacheEntry(int cache_state);

// Returns true if the cache state of the given two cache entries are equal.
bool CacheStatesEqual(const DriveCacheEntry& a, const DriveCacheEntry& b);

// Copies |error| to |output|. Used to run asynchronous functions that take
// FileOperationCallback from tests.
void CopyErrorCodeFromFileOperationCallback(DriveFileError* output,
                                            DriveFileError error);

// Copies |error| and |moved_file_path| to |out_error| and |out_file_path|.
// Used to run asynchronous functions that take FileMoveCallback from tests.
void CopyResultsFromFileMoveCallback(DriveFileError* out_error,
                                     FilePath* out_file_path,
                                     DriveFileError error,
                                     const FilePath& moved_file_path);

// Copies |error| and |entry_proto| to |out_error| and |out_entry_proto|
// respectively. Used to run asynchronous functions that take
// GetEntryInfoCallback from tests.
void CopyResultsFromGetEntryInfoCallback(
    DriveFileError* out_error,
    scoped_ptr<DriveEntryProto>* out_entry_proto,
    DriveFileError error,
    scoped_ptr<DriveEntryProto> entry_proto);

// Copies |error| and |entries| to |out_error| and |out_entries|
// respectively. Used to run asynchronous functions that take
// GetEntryInfoCallback from tests.
void CopyResultsFromReadDirectoryCallback(
    DriveFileError* out_error,
    scoped_ptr<DriveEntryProtoVector>* out_entries,
    DriveFileError error,
    scoped_ptr<DriveEntryProtoVector> entries);

// Copies |error|, |drive_file_path|, and |entry_proto| to |out_error|,
// |out_drive_file_path|, and |out_entry_proto| respectively. Used to run
// asynchronous functions that take GetEntryInfoWithFilePathCallback from
// tests.
void CopyResultsFromGetEntryInfoWithFilePathCallback(
    DriveFileError* out_error,
    FilePath* out_drive_file_path,
    scoped_ptr<DriveEntryProto>* out_entry_proto,
    DriveFileError error,
    const FilePath& drive_file_path,
    scoped_ptr<DriveEntryProto> entry_proto);

// Copies |result| to |out_result|. Used to run asynchronous functions
// that take GetEntryInfoPairCallback from tests.
void CopyResultsFromGetEntryInfoPairCallback(
    scoped_ptr<EntryInfoPairResult>* out_result,
    scoped_ptr<EntryInfoPairResult> result);

// Copies |success| to |out_success|. Used to run asynchronous functions that
// take InitializeCacheCallback from tests.
void CopyResultFromInitializeCacheCallback(bool* out_success,
                                           bool success);

// Copies results from DriveCache methods. Used to run asynchronous functions
// that take GetFileFromCacheCallback from tests.
void CopyResultsFromGetFileFromCacheCallback(DriveFileError* out_error,
                                             FilePath* out_cache_file_path,
                                             DriveFileError error,
                                             const FilePath& cache_file_path);

// Copies results from DriveCache methods. Used to run asynchronous functions
// that take GetCacheEntryCallback from tests.
void CopyResultsFromGetCacheEntryCallback(bool* out_success,
                                          DriveCacheEntry* out_cache_entry,
                                          bool success,
                                          const DriveCacheEntry& cache_entry);

// Loads a test json file as root ("/drive") element from a test file stored
// under chrome/test/data/chromeos. Returns true on success.
bool LoadChangeFeed(const std::string& relative_path,
                    DriveFileSystem* file_system,
                    bool is_delta_feed,
                    int64 root_feed_changestamp);

}  // namespace test_util
}  // namespace drive

#endif  // CHROME_BROWSER_CHROMEOS_DRIVE_DRIVE_TEST_UTIL_H_
