// Copyright (c) 2012 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/basictypes.h"
#include "base/memory/singleton.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class FacebookChatManager;
class Profile;

class FacebookChatManagerServiceFactory : public ProfileKeyedServiceFactory {
 public:
  static FacebookChatManager* GetForProfile(Profile* profile);

  static FacebookChatManagerServiceFactory* GetInstance();

 private:
  friend struct DefaultSingletonTraits<FacebookChatManagerServiceFactory>;

  FacebookChatManagerServiceFactory();
  virtual ~FacebookChatManagerServiceFactory();

  // ProfileKeyedServiceFactory:
  virtual ProfileKeyedService* BuildServiceInstanceFor(
    Profile* profile) const OVERRIDE;

  DISALLOW_COPY_AND_ASSIGN(FacebookChatManagerServiceFactory);
};