// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_CREDENTIAL_CACHE_SERVICE_FACTORY_WIN_H_
#define CHROME_BROWSER_SYNC_CREDENTIAL_CACHE_SERVICE_FACTORY_WIN_H_

#include "base/compiler_specific.h"
#include "base/memory/singleton.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;

namespace syncer {

class CredentialCacheService;

class CredentialCacheServiceFactory : public ProfileKeyedServiceFactory {
 public:
  static syncer::CredentialCacheService* GetForProfile(Profile* profile);

  static CredentialCacheServiceFactory* GetInstance();

 private:
  friend struct DefaultSingletonTraits<CredentialCacheServiceFactory>;

  CredentialCacheServiceFactory();

  virtual ~CredentialCacheServiceFactory();

  // Returns true if |profile| uses the "Default" directory, and false if not.
  static bool IsDefaultProfile(Profile* profile);

  // ProfileKeyedServiceFactory implementation.
  virtual bool ServiceIsCreatedWithProfile() OVERRIDE;

  // ProfileKeyedServiceFactory implementation.
  virtual ProfileKeyedService* BuildServiceInstanceFor(
      Profile* profile) const OVERRIDE;
};

}  // namespace syncer

#endif  // CHROME_BROWSER_SYNC_CREDENTIAL_CACHE_SERVICE_FACTORY_WIN_H_
