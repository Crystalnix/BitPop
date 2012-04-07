// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTERNAL_POLICY_EXTENSION_LOADER_H_
#define CHROME_BROWSER_EXTENSIONS_EXTERNAL_POLICY_EXTENSION_LOADER_H_
#pragma once

#include "chrome/browser/extensions/external_extension_loader.h"

#include "base/compiler_specific.h"
#include "chrome/browser/prefs/pref_change_registrar.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"

class Profile;

// A specialization of the ExternalExtensionProvider that uses
// prefs::kExtensionInstallForceList to look up which external extensions are
// registered.
class ExternalPolicyExtensionLoader
    : public ExternalExtensionLoader,
      public content::NotificationObserver {
 public:
  explicit ExternalPolicyExtensionLoader(Profile* profile);

  // content::NotificationObserver implementation
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

 protected:
  virtual void StartLoading() OVERRIDE;

 private:
  friend class base::RefCountedThreadSafe<ExternalExtensionLoader>;

  virtual ~ExternalPolicyExtensionLoader() {}

  PrefChangeRegistrar pref_change_registrar_;
  content::NotificationRegistrar notification_registrar_;

  Profile* profile_;

  DISALLOW_COPY_AND_ASSIGN(ExternalPolicyExtensionLoader);
};

#endif  // CHROME_BROWSER_EXTENSIONS_EXTERNAL_POLICY_EXTENSION_LOADER_H_
