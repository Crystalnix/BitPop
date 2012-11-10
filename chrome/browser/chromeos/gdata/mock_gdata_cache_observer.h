// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_GDATA_MOCK_GDATA_CACHE_OBSERVER_H_
#define CHROME_BROWSER_CHROMEOS_GDATA_MOCK_GDATA_CACHE_OBSERVER_H_

#include "chrome/browser/chromeos/gdata/gdata_cache.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace gdata {

// Mock for GDataCache::Observer.
class MockGDataCacheObserver : public GDataCache::Observer {
 public:
  MockGDataCacheObserver();
  virtual ~MockGDataCacheObserver();

  MOCK_METHOD2(OnCachePinned, void(const std::string& resource_id,
                                   const std::string& md5));
  MOCK_METHOD2(OnCacheUnpinned, void(const std::string& resource_id,
                                     const std::string& md5));
  MOCK_METHOD1(OnCacheCommitted, void(const std::string& resource_id));
};

}  // namespace gdata

#endif  // CHROME_BROWSER_CHROMEOS_GDATA_MOCK_GDATA_CACHE_OBSERVER_H_
