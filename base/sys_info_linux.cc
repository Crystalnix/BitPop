// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/sys_info.h"

#include <limits>

#include "base/file_util.h"
#include "base/logging.h"
#include "base/string_number_conversions.h"

namespace base {

int64 SysInfo::AmountOfPhysicalMemory() {
  long pages = sysconf(_SC_PHYS_PAGES);
  long page_size = sysconf(_SC_PAGE_SIZE);
  if (pages == -1 || page_size == -1) {
    NOTREACHED();
    return 0;
  }

  return static_cast<int64>(pages) * page_size;
}

// static
size_t SysInfo::MaxSharedMemorySize() {
  static int64 limit;
  static bool limit_valid = false;
  if (!limit_valid) {
    std::string contents;
    file_util::ReadFileToString(FilePath("/proc/sys/kernel/shmmax"), &contents);
    DCHECK(!contents.empty());
    if (!contents.empty() && contents[contents.length() - 1] == '\n') {
      contents.erase(contents.length() - 1);
    }
    if (base::StringToInt64(contents, &limit)) {
      DCHECK(limit >= 0);
      DCHECK(static_cast<uint64>(limit) <= std::numeric_limits<size_t>::max());
      limit_valid = true;
    } else {
      NOTREACHED();
      return 0;
    }
  }
  return static_cast<size_t>(limit);
}

}  // namespace base
