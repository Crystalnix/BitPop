// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_GDATA_GDATA_UTIL_H_
#define CHROME_BROWSER_CHROMEOS_GDATA_GDATA_UTIL_H_

#include <string>
#include <vector>

#include "base/bind.h"
#include "base/memory/scoped_ptr.h"
#include "base/platform_file.h"
#include "base/time.h"
#include "chrome/browser/chromeos/gdata/gdata_errorcode.h"
#include "googleurl/src/gurl.h"

class FilePath;
class Profile;

namespace base {
class SequencedTaskRunner;
}  // namespace base

namespace tracked_objects {
class Location;
}  // tracked_objects

namespace gdata {
namespace util {

// Path constants.

// The extension for dirty files. The file names look like
// "<resource-id>.local".
const char kLocallyModifiedFileExtension[] = "local";
// The extension for mounted files. The file names look like
// "<resource-id>.<md5>.mounted".
const char kMountedArchiveFileExtension[] = "mounted";
const char kWildCard[] = "*";
// The path is used for creating a symlink in "pinned" directory for a file
// which is not yet fetched.
const char kSymLinkToDevNull[] = "/dev/null";

// Returns the GData mount point path, which looks like "/special/gdata".
const FilePath& GetGDataMountPointPath();

// Returns the GData mount path as string.
const std::string& GetGDataMountPointPathAsString();

// Returns the 'local' root of remote file system as "/special".
const FilePath& GetSpecialRemoteRootPath();

// Returns the gdata file resource url formatted as
// chrome://drive/<resource_id>/<file_name>.
GURL GetFileResourceUrl(const std::string& resource_id,
                        const std::string& file_name);

// Given a profile and a gdata_cache_path, return the file resource url.
void ModifyGDataFileResourceUrl(Profile* profile,
                                const FilePath& gdata_cache_path,
                                GURL* url);

// Returns true if the given path is under the GData mount point.
bool IsUnderGDataMountPoint(const FilePath& path);

// Extracts the GData path from the given path located under the GData mount
// point. Returns an empty path if |path| is not under the GData mount point.
// Examples: ExtractGDatPath("/special/drive/foo.txt") => "drive/foo.txt"
FilePath ExtractGDataPath(const FilePath& path);

// Inserts all possible cache paths for a given vector of paths on gdata mount
// point into the output vector |cache_paths|, and then invokes callback.
// Caller must ensure that |cache_paths| lives until the callback is invoked.
void InsertGDataCachePathsPermissions(
    Profile* profile_,
    scoped_ptr<std::vector<FilePath> > gdata_paths,
    std::vector<std::pair<FilePath, int> >* cache_paths,
    const base::Closure& callback);

// Returns true if gdata is currently active with the specified profile.
bool IsGDataAvailable(Profile* profile);

// Escapes a file name in GData cache.
// Replaces percent ('%'), period ('.') and slash ('/') with %XX (hex)
std::string EscapeCacheFileName(const std::string& filename);

// Unescapes a file path in GData cache.
// This is the inverse of EscapeCacheFileName.
std::string UnescapeCacheFileName(const std::string& filename);

// Extracts resource_id, md5, and extra_extension from cache path.
// Case 1: Pinned and outgoing symlinks only have resource_id.
// Example: path="/user/GCache/v1/pinned/pdf:a1b2" =>
//          resource_id="pdf:a1b2", md5="", extra_extension="";
// Case 2: Normal files have both resource_id and md5.
// Example: path="/user/GCache/v1/tmp/pdf:a1b2.01234567" =>
//          resource_id="pdf:a1b2", md5="01234567", extra_extension="";
// Case 3: Mounted files have all three parts.
// Example: path="/user/GCache/v1/persistent/pdf:a1b2.01234567.mounted" =>
//          resource_id="pdf:a1b2", md5="01234567", extra_extension="mounted".
void ParseCacheFilePath(const FilePath& path,
                        std::string* resource_id,
                        std::string* md5,
                        std::string* extra_extension);

// Returns true if Drive v2 API is enabled via commandline switch.
bool IsDriveV2ApiEnabled();

// Returns a PlatformFileError that corresponds to the GDataFileError provided.
base::PlatformFileError GDataFileErrorToPlatformError(GDataFileError error);

// Parses an RFC 3339 date/time into a base::Time, returning true on success.
// The time string must be in the format "yyyy-mm-ddThh:mm:ss.dddTZ" (TZ is
// either '+hh:mm', '-hh:mm', 'Z' (representing UTC), or an empty string).
bool GetTimeFromString(const base::StringPiece& raw_value, base::Time* time);

// Formats a base::Time as an RFC 3339 date/time (in UTC).
std::string FormatTimeAsString(const base::Time& time);
// Formats a base::Time as an RFC 3339 date/time (in localtime).
std::string FormatTimeAsStringLocaltime(const base::Time& time);

// Callback type for PrepareWritableFilePathAndRun.
typedef base::Callback<void (GDataFileError, const FilePath& path)>
    OpenFileCallback;

// Invokes |callback| on blocking thread pool, after converting virtual |path|
// string like "/special/drive/foo.txt" to the concrete local cache file path.
// After |callback| returns, the written content is synchronized to the server.
//
// If |path| is not a GData path, it is regarded as a local path and no path
// conversion takes place.
//
// Must be called from UI thread.
void PrepareWritableFileAndRun(Profile* profile,
                               const FilePath& path,
                               const OpenFileCallback& callback);

// Converts gdata error code into file platform error code.
GDataFileError GDataToGDataFileError(GDataErrorCode status);

// Wrapper around BrowserThread::PostTask to post a task to the blocking
// pool with the given sequence token.
void PostBlockingPoolSequencedTask(
    const tracked_objects::Location& from_here,
    base::SequencedTaskRunner* blocking_task_runner,
    const base::Closure& task);

// Similar to PostBlockingPoolSequencedTask() but this one takes a reply
// callback that runs on the calling thread.
void PostBlockingPoolSequencedTaskAndReply(
    const tracked_objects::Location& from_here,
    base::SequencedTaskRunner* blocking_task_runner,
    const base::Closure& request_task,
    const base::Closure& reply_task);

}  // namespace util
}  // namespace gdata

#endif  // CHROME_BROWSER_CHROMEOS_GDATA_GDATA_UTIL_H_
