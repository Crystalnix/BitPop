// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_SETTINGS_WEAK_UNLIMITED_SETTINGS_STORAGE_H_
#define CHROME_BROWSER_EXTENSIONS_SETTINGS_WEAK_UNLIMITED_SETTINGS_STORAGE_H_
#pragma once

#include "base/compiler_specific.h"
#include "chrome/browser/extensions/settings/settings_storage.h"

namespace extensions {

// A SettingsStorage decorator which makes calls through |Set| ignore quota.
// "Weak" because ownership of the delegate isn't taken; this is designed to be
// temporarily attached to storage areas.
class WeakUnlimitedSettingsStorage : public SettingsStorage {
 public:
  // Ownership of |delegate| NOT taken.
  explicit WeakUnlimitedSettingsStorage(SettingsStorage* delegate);

  virtual ~WeakUnlimitedSettingsStorage();

  // SettingsStorage implementation.
  virtual size_t GetBytesInUse(const std::string& key) OVERRIDE;
  virtual size_t GetBytesInUse(const std::vector<std::string>& keys) OVERRIDE;
  virtual size_t GetBytesInUse() OVERRIDE;
  virtual ReadResult Get(const std::string& key) OVERRIDE;
  virtual ReadResult Get(const std::vector<std::string>& keys) OVERRIDE;
  virtual ReadResult Get() OVERRIDE;
  virtual WriteResult Set(
      WriteOptions options,
      const std::string& key,
      const Value& value) OVERRIDE;
  virtual WriteResult Set(
      WriteOptions options, const DictionaryValue& values) OVERRIDE;
  virtual WriteResult Remove(const std::string& key) OVERRIDE;
  virtual WriteResult Remove(const std::vector<std::string>& keys) OVERRIDE;
  virtual WriteResult Clear() OVERRIDE;

 private:
  // The delegate storage area, NOT OWNED.
  SettingsStorage* const delegate_;

  DISALLOW_COPY_AND_ASSIGN(WeakUnlimitedSettingsStorage);
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_SETTINGS_WEAK_UNLIMITED_SETTINGS_STORAGE_H_
