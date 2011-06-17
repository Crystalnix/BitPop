// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_PREFERENCE_API_H__
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_PREFERENCE_API_H__
#pragma once

#include <string>

#include "chrome/browser/extensions/extension_function.h"
#include "chrome/browser/prefs/pref_change_registrar.h"
#include "content/common/notification_observer.h"

class ExtensionPreferenceEventRouter : public NotificationObserver {
 public:
  explicit ExtensionPreferenceEventRouter(Profile* profile);
  virtual ~ExtensionPreferenceEventRouter();

 private:
  // NotificationObserver implementation.
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

  void OnPrefChanged(PrefService* pref_service, const std::string& pref_key);

  // This method dispatches events to the extension message service.
  void DispatchEvent(const std::string& extension_id,
                     const std::string& event_name,
                     const std::string& json_args);

  PrefChangeRegistrar registrar_;
  PrefChangeRegistrar incognito_registrar_;

  // Weak, owns us (transitively via ExtensionService).
  Profile* profile_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionPreferenceEventRouter);
};

class Value;

class PrefTransformerInterface {
 public:
  virtual ~PrefTransformerInterface() {}

  // Converts the representation of a preference as seen by the extension
  // into a representation that is used in the pref stores of the browser.
  // Returns the pref store representation in case of success or sets
  // |error| and returns NULL otherwise.
  // The ownership of the returned value is passed to the caller.
  virtual Value* ExtensionToBrowserPref(const Value* extension_pref,
                                        std::string* error) = 0;

  // Converts the representation of the preference as stored in the browser
  // into a representation that is used by the extension.
  // Returns the extension representation in case of success or NULL otherwise.
  // The ownership of the returned value is passed to the caller.
  virtual Value* BrowserToExtensionPref(const Value* browser_pref) = 0;
};

class GetPreferenceFunction : public SyncExtensionFunction {
 public:
  virtual ~GetPreferenceFunction();
  virtual bool RunImpl();
  DECLARE_EXTENSION_FUNCTION_NAME("experimental.preferences.get")
};

class SetPreferenceFunction : public SyncExtensionFunction {
 public:
  virtual ~SetPreferenceFunction();
  virtual bool RunImpl();
  DECLARE_EXTENSION_FUNCTION_NAME("experimental.preferences.set")
};

class ClearPreferenceFunction : public SyncExtensionFunction {
 public:
  virtual ~ClearPreferenceFunction();
  virtual bool RunImpl();
  DECLARE_EXTENSION_FUNCTION_NAME("experimental.preferences.clear")
};

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_PREFERENCE_API_H__
