// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_SYNC_PROMO_SYNC_PROMO_TRIAL_H_
#define CHROME_BROWSER_UI_WEBUI_SYNC_PROMO_SYNC_PROMO_TRIAL_H_

#include "base/basictypes.h"
#include "chrome/browser/ui/webui/sync_promo/sync_promo_ui.h"

class Profile;
namespace content {
class WebUI;
}

namespace sync_promo_trial {

enum StartupOverride {
  STARTUP_OVERRIDE_NONE,
  STARTUP_OVERRIDE_SHOW,
  STARTUP_OVERRIDE_HIDE,
};

// Activate the field trial.
void Activate();

// Returns the start up override value for any currently running sync promo
// trials.
StartupOverride GetStartupOverrideForCurrentTrial();

// Records that the user was shown the sync promo for any currently running sync
// promo trials. |web_ui| is the web UI where the promo was shown.
void RecordUserShownPromo(content::WebUI* web_ui);

// Records that the sync promo was not shown to the user (when it normally
// would have been shown) because of the current trial.
void RecordSyncPromoSuppressedForCurrentTrial();

// Records that the user signed into sync for any currently running sync promo
// trials. |web_ui| is the web UI where the user signed into sync.
void RecordUserSignedIn(content::WebUI* web_ui);

// Returns true if a sync promo trial is running that overrides the sync promo
// version. If such a trial is running then on return |version| will contain the
// version of the sync promo to show. |version| must not be NULL.
bool GetSyncPromoVersionForCurrentTrial(SyncPromoUI::Version* version);

}  // namespace sync_promo_trial

#endif  // CHROME_BROWSER_UI_WEBUI_SYNC_PROMO_SYNC_PROMO_TRIAL_H_
