// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_FONT_SETTINGS_FONT_SETTINGS_API_FACTORY_H_
#define CHROME_BROWSER_EXTENSIONS_API_FONT_SETTINGS_FONT_SETTINGS_API_FACTORY_H_

#include "base/memory/singleton.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

namespace extensions {
class FontSettingsAPI;

class FontSettingsAPIFactory : ProfileKeyedServiceFactory {
 public:
  static FontSettingsAPI* GetForProfile(Profile* profile);

  static FontSettingsAPIFactory* GetInstance();

 private:
  friend struct DefaultSingletonTraits<FontSettingsAPIFactory>;

  FontSettingsAPIFactory();
  virtual ~FontSettingsAPIFactory();

  // ProfileKeyedBaseFactory implementation.
  virtual ProfileKeyedService* BuildServiceInstanceFor(
      Profile* profile) const OVERRIDE;
  virtual bool ServiceIsCreatedWithProfile() const OVERRIDE;
  virtual bool ServiceIsNULLWhileTesting() const OVERRIDE;
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_FONT_SETTINGS_FONT_SETTINGS_API_FACTORY_H_
