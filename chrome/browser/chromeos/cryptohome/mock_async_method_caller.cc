// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/cryptohome/mock_async_method_caller.h"

using ::testing::Invoke;
using ::testing::WithArgs;
using ::testing::_;

namespace cryptohome {

MockAsyncMethodCaller::MockAsyncMethodCaller()
    : success_(false), return_code_(cryptohome::MOUNT_ERROR_NONE) {
}

MockAsyncMethodCaller::~MockAsyncMethodCaller() {}

void MockAsyncMethodCaller::SetUp(bool success, MountError return_code) {
  success_ = success;
  return_code_ = return_code;
  ON_CALL(*this, AsyncCheckKey(_, _, _))
      .WillByDefault(
          WithArgs<2>(Invoke(this, &MockAsyncMethodCaller::DoCallback)));
  ON_CALL(*this, AsyncMigrateKey(_, _, _, _))
      .WillByDefault(
          WithArgs<3>(Invoke(this, &MockAsyncMethodCaller::DoCallback)));
  ON_CALL(*this, AsyncMount(_, _, _, _))
      .WillByDefault(
          WithArgs<3>(Invoke(this, &MockAsyncMethodCaller::DoCallback)));
  ON_CALL(*this, AsyncMountGuest(_))
      .WillByDefault(
          WithArgs<0>(Invoke(this, &MockAsyncMethodCaller::DoCallback)));
  ON_CALL(*this, AsyncRemove(_, _))
      .WillByDefault(
          WithArgs<1>(Invoke(this, &MockAsyncMethodCaller::DoCallback)));
}

void MockAsyncMethodCaller::DoCallback(Callback callback) {
  callback.Run(success_, return_code_);
}


}  // namespace cryptohome
