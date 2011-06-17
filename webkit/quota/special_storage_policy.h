// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_QUOTA_SPECIAL_STORAGE_POLICY_H_
#define WEBKIT_QUOTA_SPECIAL_STORAGE_POLICY_H_

#include <string>

#include "base/memory/ref_counted.h"

class GURL;

namespace quota {

// Special rights are granted to 'extensions' and 'applications'. The
// storage subsystems query this interface to determine which origins
// have these rights. Chrome provides an impl that is cognizant of what
// is currently installed in the extensions system.
// An implementation must be thread-safe.
class SpecialStoragePolicy
    : public base::RefCountedThreadSafe<SpecialStoragePolicy> {
 public:
  SpecialStoragePolicy();

  // Protected storage is not subject to removal by the browsing data remover.
  virtual bool IsStorageProtected(const GURL& origin) = 0;

  // Unlimited storage is not subject to 'quotas'.
  virtual bool IsStorageUnlimited(const GURL& origin) = 0;

  // Checks if extension identified with |extension_id| is registered as
  // file handler.
  virtual bool IsFileHandler(const std::string& extension_id) = 0;

 protected:
  friend class base::RefCountedThreadSafe<SpecialStoragePolicy>;
  virtual ~SpecialStoragePolicy();
};

}  // namespace quota

#endif  // WEBKIT_QUOTA_SPECIAL_STORAGE_POLICY_H_
