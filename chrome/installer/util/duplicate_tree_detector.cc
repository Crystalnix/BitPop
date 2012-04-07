// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#include "chrome/installer/util/duplicate_tree_detector.h"

#include "base/file_util.h"
#include "base/logging.h"

namespace installer {

bool IsIdenticalFileHierarchy(const FilePath& src_path,
                              const FilePath& dest_path) {
  using file_util::FileEnumerator;
  base::PlatformFileInfo src_info;
  base::PlatformFileInfo dest_info;

  bool is_identical = false;
  if (file_util::GetFileInfo(src_path, &src_info) &&
      file_util::GetFileInfo(dest_path, &dest_info)) {
    // Both paths exist, check the types:
    if (!src_info.is_directory && !dest_info.is_directory) {
      // Two files are "identical" if the file sizes are equivalent.
      is_identical = src_info.size == dest_info.size;
    } else if (src_info.is_directory && dest_info.is_directory) {
      // Two directories are "identical" if dest_path contains entries that are
      // "identical" to all the entries in src_path.
      is_identical = true;

      FileEnumerator path_enum(
          src_path,
          false,  // Not recursive
          static_cast<FileEnumerator::FileType>(
              FileEnumerator::FILES | FileEnumerator::DIRECTORIES));
      for (FilePath path = path_enum.Next(); is_identical && !path.empty();
           path = path_enum.Next()) {
        is_identical =
            IsIdenticalFileHierarchy(path, dest_path.Append(path.BaseName()));
      }
    } else {
      // The two paths are of different types, so they cannot be identical.
      DCHECK(!is_identical);
    }
  }

  return is_identical;
}

}  // namespace installer
