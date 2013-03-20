// Copyright (c) 2013 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2013 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/signin/signin_result_page_tracker_factory.h"

#include "chrome/browser/profiles/profile_dependency_manager.h"
#include "chrome/browser/signin/signin_result_page_tracker.h"


SigninResultPageTrackerFactory::SigninResultPageTrackerFactory()
    : ProfileKeyedServiceFactory("SigninResultPageTracker",
                                 ProfileDependencyManager::GetInstance()) {
}

SigninResultPageTrackerFactory::~SigninResultPageTrackerFactory() {}

// static
SigninResultPageTracker* SigninResultPageTrackerFactory::GetForProfile(
	Profile* profile) {
  return static_cast<SigninResultPageTracker*>(
    GetInstance()->GetServiceForProfile(profile, true));
}

// static
SigninResultPageTrackerFactory* SigninResultPageTrackerFactory::GetInstance() {
  return Singleton<SigninResultPageTrackerFactory>::get();
}

ProfileKeyedService* SigninResultPageTrackerFactory::BuildServiceInstanceFor(
    Profile* profile) const {
  SigninResultPageTracker* service = new SigninResultPageTracker();
  service->Initialize(profile);
  return service;
}
