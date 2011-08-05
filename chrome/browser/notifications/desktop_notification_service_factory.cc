// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/notifications/desktop_notification_service_factory.h"

#include "chrome/browser/browser_process.h"
#include "chrome/browser/notifications/desktop_notification_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_dependency_manager.h"
#include "content/browser/browser_thread.h"

// static
DesktopNotificationService* DesktopNotificationServiceFactory::GetForProfile(
    Profile* profile) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  return static_cast<DesktopNotificationService*>(
      GetInstance()->GetServiceForProfile(profile, true));
}

// static
DesktopNotificationServiceFactory* DesktopNotificationServiceFactory::
    GetInstance() {
  return Singleton<DesktopNotificationServiceFactory>::get();
}

DesktopNotificationServiceFactory::DesktopNotificationServiceFactory()
    : ProfileKeyedServiceFactory(ProfileDependencyManager::GetInstance()) {
}

DesktopNotificationServiceFactory::~DesktopNotificationServiceFactory() {
}

ProfileKeyedService* DesktopNotificationServiceFactory::BuildServiceInstanceFor(
    Profile* profile) const {
  DesktopNotificationService* service = new DesktopNotificationService(profile,
      g_browser_process->notification_ui_manager());

  return service;
}

bool DesktopNotificationServiceFactory::ServiceHasOwnInstanceInIncognito() {
  return true;
}
