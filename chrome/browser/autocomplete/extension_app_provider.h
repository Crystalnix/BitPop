// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
//
// This file contains the Extension App autocomplete provider. The provider
// is responsible for keeping track of which Extension Apps are installed and
// their URLs.  An instance of it gets created and managed by the autocomplete
// controller.
//
// For more information on the autocomplete system in general, including how
// the autocomplete controller and autocomplete providers work, see
// chrome/browser/autocomplete.h.

#ifndef CHROME_BROWSER_AUTOCOMPLETE_EXTENSION_APP_PROVIDER_H_
#define CHROME_BROWSER_AUTOCOMPLETE_EXTENSION_APP_PROVIDER_H_
#pragma once

#include <utility>
#include <vector>

#include "base/compiler_specific.h"
#include "base/gtest_prod_util.h"
#include "chrome/browser/autocomplete/autocomplete.h"
#include "chrome/browser/autocomplete/autocomplete_match.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"

class ExtensionAppProvider : public AutocompleteProvider,
                             public content::NotificationObserver {
 public:
  ExtensionAppProvider(ACProviderListener* listener, Profile* profile);

  // Only used for testing.
  void AddExtensionAppForTesting(const string16& app_name,
                                 const string16& url);

  // AutocompleteProvider implementation:
  virtual void Start(const AutocompleteInput& input,
                     bool minimal_changes) OVERRIDE;

 private:
  FRIEND_TEST_ALL_PREFIXES(ExtensionAppProviderTest, CreateMatchSanitize);

  // An ExtensionApp is a pair of Extension Name and the Launch URL.
  typedef std::pair<string16, string16> ExtensionApp;
  typedef std::vector<ExtensionApp> ExtensionApps;

  virtual ~ExtensionAppProvider();

  // Construct a match for the specified parameters.
  AutocompleteMatch CreateAutocompleteMatch(const AutocompleteInput& input,
                                            const string16& name,
                                            const string16& url,
                                            size_t name_match_index,
                                            size_t url_match_index);

  // Fetch the current app list and cache it locally.
  void RefreshAppList();

  // Register for install/uninstall notification so we can update our cache.
  void RegisterForNotifications();

  // Calculate the relevance of the match.
  int CalculateRelevance(AutocompleteInput::Type type,
                         int input_length,
                         int target_length,
                         const GURL& url);

  // content::NotificationObserver implementation:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  content::NotificationRegistrar registrar_;

  // Our cache of ExtensionApp objects (name + url) representing the extension
  // apps we know about.
  ExtensionApps extension_apps_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionAppProvider);
};

#endif  // CHROME_BROWSER_AUTOCOMPLETE_EXTENSION_APP_PROVIDER_H_
