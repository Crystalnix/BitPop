// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/quota/mock_special_storage_policy.h"

namespace quota {

MockSpecialStoragePolicy::MockSpecialStoragePolicy() {}
MockSpecialStoragePolicy::~MockSpecialStoragePolicy() {}

bool MockSpecialStoragePolicy::IsStorageProtected(const GURL& origin) {
  return protected_.find(origin) != protected_.end();
}

bool MockSpecialStoragePolicy::IsStorageUnlimited(const GURL& origin) {
  return unlimited_.find(origin) != unlimited_.end();
}

bool MockSpecialStoragePolicy::IsFileHandler(const std::string& extension_id) {
  return file_handlers_.find(extension_id) != file_handlers_.end();
}

}  // namespace quota
