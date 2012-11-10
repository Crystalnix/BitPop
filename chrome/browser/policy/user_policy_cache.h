// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_POLICY_USER_POLICY_CACHE_H_
#define CHROME_BROWSER_POLICY_USER_POLICY_CACHE_H_

#include <string>

#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "chrome/browser/policy/cloud_policy_cache_base.h"
#include "chrome/browser/policy/user_policy_disk_cache.h"

class FilePath;

namespace enterprise_management {

class CachedCloudPolicyResponse;
// <Old-style policy support> (see comment below)
class GenericValue;
// </Old-style policy support>

}  // namespace enterprise_management

namespace policy {

class PolicyMap;

// CloudPolicyCacheBase implementation that persists policy information
// into the file specified by the c'tor parameter |backing_file_path|.
class UserPolicyCache : public CloudPolicyCacheBase,
                        public UserPolicyDiskCache::Delegate {
 public:
  // |backing_file_path| is the path to the cache file.
  // |wait_for_policy_fetch| is true if the cache should be ready only after
  // an attempt was made to fetch user policy.
  UserPolicyCache(const FilePath& backing_file_path,
                  bool wait_for_policy_fetch);
  virtual ~UserPolicyCache();

  // CloudPolicyCacheBase implementation:
  virtual void Load() OVERRIDE;
  virtual bool SetPolicy(
      const enterprise_management::PolicyFetchResponse& policy) OVERRIDE;
  virtual void SetUnmanaged() OVERRIDE;
  virtual void SetFetchingDone() OVERRIDE;

 private:
  class DiskCache;

  // UserPolicyDiskCache::Delegate implementation:
  virtual void OnDiskCacheLoaded(
      UserPolicyDiskCache::LoadResult result,
      const enterprise_management::CachedCloudPolicyResponse&
          cached_response) OVERRIDE;

  // CloudPolicyCacheBase implementation:
  virtual bool DecodePolicyData(
      const enterprise_management::PolicyData& policy_data,
      PolicyMap* policies) OVERRIDE;

  // Checks if this cache is ready, and invokes SetReady() if so.
  void CheckIfReady();

  // <Old-style policy support>
  // The following member functions are needed to support old-style policy and
  // can be removed once all server-side components (CPanel, D3) have been
  // migrated to providing the new policy format.
  //
  // If |policies| is empty and |policy_data| contains a field named
  // "repeated GenericNamedValue named_value = 2;", the policies in that field
  // are added to |policies| as LEVEL_MANDATORY, SCOPE_USER policies.
  void MaybeDecodeOldstylePolicy(const std::string& policy_data,
                                 PolicyMap* policies);

  Value* DecodeIntegerValue(google::protobuf::int64 value) const;
  Value* DecodeValue(const enterprise_management::GenericValue& value) const;

  // </Old-style policy support>

  // Manages the cache file.
  scoped_refptr<UserPolicyDiskCache> disk_cache_;

  // Used for constructing the weak ptr passed to |disk_cache_|.
  base::WeakPtrFactory<UserPolicyDiskCache::Delegate> weak_ptr_factory_;

  // True if the disk cache has been loaded.
  bool disk_cache_ready_;

  // True if at least one attempt was made to refresh the cache with a freshly
  // fetched policy, or if there is no need to wait for that.
  bool fetch_ready_;

  DISALLOW_COPY_AND_ASSIGN(UserPolicyCache);
};

}  // namespace policy

#endif  // CHROME_BROWSER_POLICY_USER_POLICY_CACHE_H_
