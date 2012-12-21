// Copyright (c) 2012 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/facebook_chat/facebook_chat_manager_service_factory.h"

#include "chrome/browser/facebook_chat/facebook_chat_manager.h"
#include "chrome/browser/profiles/profile_dependency_manager.h"

// static
FacebookChatManager* FacebookChatManagerServiceFactory::GetForProfile(Profile* profile) {
  return static_cast<FacebookChatManager*>(
      GetInstance()->GetServiceForProfile(profile, true));
}

// static
FacebookChatManagerServiceFactory* FacebookChatManagerServiceFactory::GetInstance() {
  return Singleton<FacebookChatManagerServiceFactory>::get();
}

FacebookChatManagerServiceFactory::FacebookChatManagerServiceFactory()
    : ProfileKeyedServiceFactory("facebook_chat_manager", ProfileDependencyManager::GetInstance()) {
}

FacebookChatManagerServiceFactory::~FacebookChatManagerServiceFactory() {
}

ProfileKeyedService* FacebookChatManagerServiceFactory::BuildServiceInstanceFor(
    Profile* profile) const {
  return new FacebookChatManager();
}