// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_POLICY_CONFIG_DIR_POLICY_PROVIDER_H_
#define CHROME_BROWSER_POLICY_CONFIG_DIR_POLICY_PROVIDER_H_
#pragma once

#include "base/time.h"
#include "chrome/browser/policy/file_based_policy_provider.h"
#include "chrome/browser/policy/policy_map.h"

class FilePath;

namespace policy {

// Policy provider backed by JSON files in a configuration directory.
class ConfigDirPolicyProvider : public FileBasedPolicyProvider {
 public:
  ConfigDirPolicyProvider(const PolicyDefinitionList* policy_list,
                          PolicyLevel level,
                          PolicyScope scope,
                          const FilePath& config_dir);

 private:
  DISALLOW_COPY_AND_ASSIGN(ConfigDirPolicyProvider);
};

// A provider delegate implementation backed by a set of files in a given
// directory. The files should contain JSON-formatted policy settings. They are
// merged together and the result is returned via the ProviderDelegate
// interface. The files are consulted in lexicographic file name order, so the
// last value read takes precedence in case of preference key collisions.
class ConfigDirPolicyProviderDelegate
    : public FileBasedPolicyProvider::ProviderDelegate {
 public:
  ConfigDirPolicyProviderDelegate(const FilePath& config_dir,
                                  PolicyLevel level,
                                  PolicyScope scope);

  // FileBasedPolicyProvider::ProviderDelegate implementation.
  virtual PolicyMap* Load() OVERRIDE;
  virtual base::Time GetLastModification() OVERRIDE;

 private:
  // Policies loaded by this provider will have these attributes.
  PolicyLevel level_;
  PolicyScope scope_;

  DISALLOW_COPY_AND_ASSIGN(ConfigDirPolicyProviderDelegate);
};

}  // namespace policy

#endif  // CHROME_BROWSER_POLICY_CONFIG_DIR_POLICY_PROVIDER_H_
