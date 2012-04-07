// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_UTIL_UNRECOVERABLE_ERROR_INFO_H_
#define CHROME_BROWSER_SYNC_UTIL_UNRECOVERABLE_ERROR_INFO_H_
// TODO(lipalani): Figure out the right location for this class so it is
// accessible outside of sync engine as well.
#pragma once

#include <string>

#include "base/location.h"

namespace browser_sync {

class UnrecoverableErrorInfo {
 public:
  UnrecoverableErrorInfo();
  UnrecoverableErrorInfo(
    const tracked_objects::Location& location,
    const std::string& message);
  ~UnrecoverableErrorInfo();

  void Reset(const tracked_objects::Location& location,
             const std::string& message);

  bool IsSet() const;

  const tracked_objects::Location& location() const;
  const std::string& message() const;

 private:
  tracked_objects::Location location_;
  std::string message_;
  bool is_set_;
};

}  // namespace browser_sync

#endif  // CHROME_BROWSER_SYNC_UTIL_UNRECOVERABLE_ERROR_INFO_H_
