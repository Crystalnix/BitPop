// Copyright (c) 2012 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/facebook_chat/facebook_bitpop_notification_service_factory.h"

#include "chrome/browser/facebook_chat/facebook_bitpop_notification.h"
#include "chrome/browser/profiles/profile_dependency_manager.h"

#if defined(OS_WIN)
#include "chrome/browser/ui/views/facebook_chat/facebook_bitpop_notification_win.h"
#elif defined (OS_MACOSX)
#include "chrome/browser/ui/cocoa/facebook_chat/facebook_bitpop_notification_mac.h"
#endif

// static
FacebookBitpopNotification* FacebookBitpopNotificationServiceFactory::GetForProfile(Profile* profile) {
  return static_cast<FacebookBitpopNotification*>(
      GetInstance()->GetServiceForProfile(profile, true));
}

// static
FacebookBitpopNotificationServiceFactory* FacebookBitpopNotificationServiceFactory::GetInstance() {
  return Singleton<FacebookBitpopNotificationServiceFactory>::get();
}

FacebookBitpopNotificationServiceFactory::FacebookBitpopNotificationServiceFactory()
    : ProfileKeyedServiceFactory("facebook_bitpop_notification", ProfileDependencyManager::GetInstance()) {
}

FacebookBitpopNotificationServiceFactory::~FacebookBitpopNotificationServiceFactory() {
}

ProfileKeyedService* FacebookBitpopNotificationServiceFactory::BuildServiceInstanceFor(
    Profile* profile) const {
#if defined(OS_WIN)
  return new FacebookBitpopNotificationWin(profile);
#elif defined (OS_MACOSX)
  return new FacebookBitpopNotificationMac(profile);
#else
  return NULL;
#endif
}