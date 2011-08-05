// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_SERVICE_CLOUD_PRINT_CLOUD_PRINT_TOKEN_STORE_H_
#define CHROME_SERVICE_CLOUD_PRINT_CLOUD_PRINT_TOKEN_STORE_H_
#pragma once

#include <string>

// This class serves as the single repository for cloud print auth tokens. This
// is only used within the CloudPrintProxyCoreThread.

#include "base/logging.h"
#include "base/threading/non_thread_safe.h"

class CloudPrintTokenStore : public base::NonThreadSafe {
 public:
  // Returns the CloudPrintTokenStore instance for this thread. Will be NULL
  // if no instance was created in this thread before.
  static CloudPrintTokenStore* current();

  CloudPrintTokenStore();
  ~CloudPrintTokenStore();

  void SetToken(const std::string& token, bool is_oauth);
  std::string token() const {
    DCHECK(CalledOnValidThread());
    return token_;
  }
  bool token_is_oauth() const {
    DCHECK(CalledOnValidThread());
    return token_is_oauth_;
  }

 private:
  std::string token_;
  bool token_is_oauth_;

  DISALLOW_COPY_AND_ASSIGN(CloudPrintTokenStore);
};

#endif  // CHROME_SERVICE_CLOUD_PRINT_CLOUD_PRINT_TOKEN_STORE_H_
