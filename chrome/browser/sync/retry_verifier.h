// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_RETRY_VERIFIER_H_
#define CHROME_BROWSER_SYNC_RETRY_VERIFIER_H_
#pragma once

#include "base/time.h"

namespace browser_sync {
namespace sessions {
struct SyncSessionSnapshot;
}  // namespace sessions
}  // namespace browser_sync

namespace browser_sync {
// The minimum and maximum wait times for a retry. The actual retry would take
// place somewhere in this range. The algorithm that calculates the retry wait
// time uses rand functions.
struct DelayInfo {
  int64 min_delay;
  int64 max_delay;
};

// Class to verify retries take place using the exponential backoff algorithm.
class RetryVerifier {
 public:
  static const int kMaxRetry = 5;
  RetryVerifier();
  ~RetryVerifier();
  int retry_count() const { return retry_count_; }

  // Initialize with the current sync session snapshot. Using the snapshot
  // we will figure out when the first retry sync happened.
  void Initialize(const browser_sync::sessions::SyncSessionSnapshot& snap);
  void VerifyRetryInterval(
      const browser_sync::sessions::SyncSessionSnapshot& snap);
  bool done() const { return done_; }
  bool Succeeded() const { return done() && success_; }

 private:
  int retry_count_;
  base::Time last_sync_time_;
  DelayInfo delay_table_[kMaxRetry];
  bool success_;
  bool done_;
  DISALLOW_COPY_AND_ASSIGN(RetryVerifier);
};
}  // namespace browser_sync
#endif  // CHROME_BROWSER_SYNC_RETRY_VERIFIER_H_
