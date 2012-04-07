// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Defines the Chrome Extensions Clear API functions, which entail
// clearing browsing data, and clearing the browser's cache (which, let's be
// honest, are the same thing), as specified in the extension API JSON.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_CLEAR_API_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_CLEAR_API_H_
#pragma once

#include <string>

#include "chrome/browser/browsing_data_remover.h"
#include "chrome/browser/extensions/extension_function.h"

class PluginPrefs;

namespace extension_clear_api_constants {

// Keys.
extern const char kAppCacheKey[];
extern const char kCacheKey[];
extern const char kCookiesKey[];
extern const char kDownloadsKey[];
extern const char kFileSystemsKey[];
extern const char kFormDataKey[];
extern const char kHistoryKey[];
extern const char kIndexedDBKey[];
extern const char kPluginDataKey[];
extern const char kLocalStorageKey[];
extern const char kPasswordsKey[];
extern const char kWebSQLKey[];

// Errors!
extern const char kOneAtATimeError[];

}  // namespace extension_clear_api_constants

// This serves as a base class from which the browsing data API functions will
// inherit. Each needs to be an observer of BrowsingDataRemover events, and each
// will handle those events in the same way (by calling the passed-in callback
// function).
//
// Each child class must implement GetRemovalMask(), which returns the bitmask
// of data types to remove.
class BrowsingDataExtensionFunction : public AsyncExtensionFunction,
                                      public BrowsingDataRemover::Observer {
 public:
  // BrowsingDataRemover::Observer interface method.
  virtual void OnBrowsingDataRemoverDone() OVERRIDE;

  // AsyncExtensionFunction interface method.
  virtual bool RunImpl() OVERRIDE;

 protected:
  // Children should override this method to provide the proper removal mask
  // based on the API call they represent.
  virtual int GetRemovalMask() const = 0;

 private:
  // Updates the removal bitmask according to whether removing plugin data is
  // supported or not.
  void CheckRemovingPluginDataSupported(
      scoped_refptr<PluginPrefs> plugin_prefs);

  // Called when we're ready to start removing data.
  void StartRemoving();

  base::Time remove_since_;
  int removal_mask_;
};

class ClearAppCacheFunction : public BrowsingDataExtensionFunction {
 public:
  ClearAppCacheFunction() {}
  virtual ~ClearAppCacheFunction() {}

 protected:
  // BrowsingDataTypeExtensionFunction interface method.
  virtual int GetRemovalMask() const OVERRIDE;

  DECLARE_EXTENSION_FUNCTION_NAME("experimental.clear.appcache")
};

class ClearBrowsingDataFunction : public BrowsingDataExtensionFunction {
 public:
  ClearBrowsingDataFunction() {}
  virtual ~ClearBrowsingDataFunction() {}

 protected:
  // BrowsingDataExtensionFunction interface method.
  virtual int GetRemovalMask() const OVERRIDE;

  DECLARE_EXTENSION_FUNCTION_NAME("experimental.clear.browsingData")
};

class ClearCacheFunction : public BrowsingDataExtensionFunction {
 public:
  ClearCacheFunction() {}
  virtual ~ClearCacheFunction() {}

 protected:
  // BrowsingDataTypeExtensionFunction interface method.
  virtual int GetRemovalMask() const OVERRIDE;

  DECLARE_EXTENSION_FUNCTION_NAME("experimental.clear.cache")
};

class ClearCookiesFunction : public BrowsingDataExtensionFunction {
 public:
  ClearCookiesFunction() {}
  virtual ~ClearCookiesFunction() {}

 protected:
  // BrowsingDataTypeExtensionFunction interface method.
  virtual int GetRemovalMask() const OVERRIDE;

  DECLARE_EXTENSION_FUNCTION_NAME("experimental.clear.cookies")
};

class ClearDownloadsFunction : public BrowsingDataExtensionFunction {
 public:
  ClearDownloadsFunction() {}
  virtual ~ClearDownloadsFunction() {}

 protected:
  // BrowsingDataTypeExtensionFunction interface method.
  virtual int GetRemovalMask() const OVERRIDE;

  DECLARE_EXTENSION_FUNCTION_NAME("experimental.clear.downloads")
};

class ClearFileSystemsFunction : public BrowsingDataExtensionFunction {
 public:
  ClearFileSystemsFunction() {}
  virtual ~ClearFileSystemsFunction() {}

 protected:
  // BrowsingDataTypeExtensionFunction interface method.
  virtual int GetRemovalMask() const OVERRIDE;

  DECLARE_EXTENSION_FUNCTION_NAME("experimental.clear.fileSystems")
};

class ClearFormDataFunction : public BrowsingDataExtensionFunction {
 public:
  ClearFormDataFunction() {}
  virtual ~ClearFormDataFunction() {}

 protected:
  // BrowsingDataTypeExtensionFunction interface method.
  virtual int GetRemovalMask() const OVERRIDE;

  DECLARE_EXTENSION_FUNCTION_NAME("experimental.clear.formData")
};

class ClearHistoryFunction : public BrowsingDataExtensionFunction {
 public:
  ClearHistoryFunction() {}
  virtual ~ClearHistoryFunction() {}

 protected:
  // BrowsingDataTypeExtensionFunction interface method.
  virtual int GetRemovalMask() const OVERRIDE;

  DECLARE_EXTENSION_FUNCTION_NAME("experimental.clear.history")
};

class ClearIndexedDBFunction : public BrowsingDataExtensionFunction {
 public:
  ClearIndexedDBFunction() {}
  virtual ~ClearIndexedDBFunction() {}

 protected:
  // BrowsingDataTypeExtensionFunction interface method.
  virtual int GetRemovalMask() const OVERRIDE;

  DECLARE_EXTENSION_FUNCTION_NAME("experimental.clear.indexedDB")
};

class ClearLocalStorageFunction : public BrowsingDataExtensionFunction {
 public:
  ClearLocalStorageFunction() {}
  virtual ~ClearLocalStorageFunction() {}

 protected:
  // BrowsingDataTypeExtensionFunction interface method.
  virtual int GetRemovalMask() const OVERRIDE;

  DECLARE_EXTENSION_FUNCTION_NAME("experimental.clear.localStorage")
};

class ClearOriginBoundCertsFunction : public BrowsingDataExtensionFunction {
 public:
  ClearOriginBoundCertsFunction() {}
  virtual ~ClearOriginBoundCertsFunction() {}

 protected:
  // BrowsingDataTypeExtensionFunction interface method.
  virtual int GetRemovalMask() const OVERRIDE;

  DECLARE_EXTENSION_FUNCTION_NAME("experimental.clear.originBoundCerts")
};

class ClearPluginDataFunction : public BrowsingDataExtensionFunction {
 public:
  ClearPluginDataFunction() {}
  virtual ~ClearPluginDataFunction() {}

 protected:
  // BrowsingDataTypeExtensionFunction interface method.
  virtual int GetRemovalMask() const OVERRIDE;

  DECLARE_EXTENSION_FUNCTION_NAME("experimental.clear.pluginData")
};

class ClearPasswordsFunction : public BrowsingDataExtensionFunction {
 public:
  ClearPasswordsFunction() {}
  virtual ~ClearPasswordsFunction() {}

 protected:
  // BrowsingDataTypeExtensionFunction interface method.
  virtual int GetRemovalMask() const OVERRIDE;

  DECLARE_EXTENSION_FUNCTION_NAME("experimental.clear.passwords")
};

class ClearWebSQLFunction : public BrowsingDataExtensionFunction {
 public:
  ClearWebSQLFunction() {}
  virtual ~ClearWebSQLFunction() {}

 protected:
  // BrowsingDataTypeExtensionFunction interface method.
  virtual int GetRemovalMask() const OVERRIDE;

  DECLARE_EXTENSION_FUNCTION_NAME("experimental.clear.webSQL")
};
#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_CLEAR_API_H_
