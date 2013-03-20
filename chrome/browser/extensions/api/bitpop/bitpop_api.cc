// Copyright (c) 2013 House of Life Property ltd. All rights reserved.
// Copyright (c) 2013 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/bitpop/bitpop_api.h"

#include "base/values.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "chrome/browser/sync/profile_sync_service_factory.h"
#include "chrome/browser/ui/browser_navigator.h"

bool BitpopGetSyncStatusFunction::RunImpl() {
	ProfileSyncService* service =
			ProfileSyncServiceFactory::GetForProfile(profile());
	SetResult(base::Value::CreateBooleanValue(service && service->HasSyncSetupCompleted()));
	return true;
}

bool BitpopLaunchFacebookSyncFunction::RunImpl() {
	Browser *browser = GetCurrentBrowser();
	if (browser) {
		chrome::NavigateParams params(browser, GURL("chrome://signin/?fb_login=1"),
																	content::PAGE_TRANSITION_LINK);
		params.disposition = NEW_FOREGROUND_TAB;
		chrome::Navigate(&params);
		return true;
	}
	return false;
}
