// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/fileapi/file_system_util.h"

#include "build/build_config.h"

#include "base/file_path.h"
#include "base/logging.h"
#include "base/sys_string_conversions.h"
#include "googleurl/src/gurl.h"
#include "net/base/escape.h"
#include "webkit/fileapi/file_system_types.h"

namespace fileapi {

const char kPersistentDir[] = "/persistent/";
const char kTemporaryDir[] = "/temporary/";
const char kExternalDir[] = "/external/";

const char kPersistentName[] = "Persistent";
const char kTemporaryName[] = "Temporary";
const char kExternalName[] = "External";

bool CrackFileSystemURL(const GURL& url, GURL* origin_url, FileSystemType* type,
                        FilePath* file_path) {
  GURL origin;
  FileSystemType file_system_type;

  if (url.scheme() != "filesystem")
    return false;

  std::string temp = url.path();
  // TODO(ericu) remove this code when that ceases to be true, which should be
  // soon.
  // On Windows, this will have backslashes for now.
  // url will look something like:
  //    filesystem:http://example.com/temporary/\dir\file.txt
  // temp will look something like:
  //    http://example.com/temporary/\dir\file.txt
  // On posix, url will look something like:
  //    filesystem:http://example.com/temporary/dir/file.txt
  // temp will look something like:
  //    http://example.com/temporary/dir/file.txt
  size_t pos = temp.find('\\');
  for (; pos != std::string::npos; pos = temp.find('\\', pos + 1)) {
    temp[pos] = '/';
  }
  // TODO(ericu): This should probably be done elsewhere after the stackable
  // layers are properly in.  We're supposed to reject any paths that contain
  // '..' segments, but the GURL constructor is helpfully resolving them for us.
  // Make sure there aren't any before we call it.
  pos = temp.find("..");
  for (; pos != std::string::npos; pos = temp.find("..", pos + 1)) {
    if ((pos == 0 || temp[pos - 1] == '/') &&
        (pos == temp.length() - 2 || temp[pos + 2] == '/'))
      return false;
  }

  // bare_url will look something like:
  //    http://example.com/temporary//dir/file.txt [on Windows; the double slash
  //    before dir will be single on posix].
  GURL bare_url(temp);

  // The input URL was malformed, bail out early.
  if (bare_url.path().empty())
    return false;

  origin = bare_url.GetOrigin();

  // The input URL was malformed, bail out early.
  if (origin.is_empty())
    return false;

  std::string path = UnescapeURLComponent(bare_url.path(),
      UnescapeRule::SPACES | UnescapeRule::URL_SPECIAL_CHARS);
  if (path.compare(0, strlen(kPersistentDir), kPersistentDir) == 0) {
    file_system_type = kFileSystemTypePersistent;
    path = path.substr(strlen(kPersistentDir));
  } else if (path.compare(0, strlen(kTemporaryDir), kTemporaryDir) == 0) {
    file_system_type = kFileSystemTypeTemporary;
    path = path.substr(strlen(kTemporaryDir));
  } else if (path.compare(0, strlen(kExternalDir), kExternalDir) == 0) {
    file_system_type = kFileSystemTypeExternal;
    path = path.substr(strlen(kExternalDir));
  } else {
    return false;
  }

  // Ensure the path is relative.
  while (!path.empty() && path[0] == '/')
    path.erase(0, 1);

  if (origin_url)
    *origin_url = origin;
  if (type)
    *type = file_system_type;
  if (file_path)
#if defined(OS_WIN)
    *file_path = FilePath(base::SysUTF8ToWide(path)).
        NormalizeWindowsPathSeparators();
#elif defined(OS_POSIX)
    *file_path = FilePath(path);
#endif

  return true;
}

GURL GetFileSystemRootURI(
    const GURL& origin_url, fileapi::FileSystemType type) {
  std::string path("filesystem:");
  path += origin_url.spec();
  switch (type) {
  case kFileSystemTypeTemporary:
    path += (kTemporaryDir + 1);  // We don't want the leading slash.
    break;
  case kFileSystemTypePersistent:
    path += (kPersistentDir + 1);  // We don't want the leading slash.
    break;
  case kFileSystemTypeExternal:
    path += (kExternalDir + 1);  // We don't want the leading slash.
    break;
  default:
    NOTREACHED();
    return GURL();
  }
  return GURL(path);
}

}  // namespace fileapi
