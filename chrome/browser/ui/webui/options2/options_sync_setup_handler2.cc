// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/options2/options_sync_setup_handler2.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "chrome/browser/sync/profile_sync_service_factory.h"
#include "content/public/browser/web_ui.h"

namespace options2 {

OptionsSyncSetupHandler::OptionsSyncSetupHandler(
    ProfileManager* profile_manager) : SyncSetupHandler2(profile_manager) {
}

OptionsSyncSetupHandler::~OptionsSyncSetupHandler() {
}

void OptionsSyncSetupHandler::StepWizardForShowSetupUI() {
  ProfileSyncService* service(ProfileSyncServiceFactory::
      GetInstance()->GetForProfile(Profile::FromWebUI(web_ui())));
  DCHECK(service);

  // We should bring up either a login or a configure flow based on the state of
  // sync.
  if (service->HasSyncSetupCompleted()) {
    if (service->IsPassphraseRequiredForDecryption()) {
      service->get_wizard().Step(SyncSetupWizard::ENTER_PASSPHRASE);
    } else {
      service->get_wizard().Step(SyncSetupWizard::CONFIGURE);
    }
  } else {
    service->get_wizard().Step(SyncSetupWizard::GetLoginState());
  }
}

void OptionsSyncSetupHandler::ShowSetupUI() {
  ProfileSyncService* service(ProfileSyncServiceFactory::
      GetInstance()->GetForProfile(Profile::FromWebUI(web_ui())));
  DCHECK(service);

  // The user is trying to manually load a syncSetup URL.  We should bring up
  // either a login or a configure flow based on the state of sync.
  if (service->HasSyncSetupCompleted()) {
    if (service->IsPassphraseRequiredForDecryption()) {
      service->get_wizard().Step(SyncSetupWizard::ENTER_PASSPHRASE);
    } else {
      service->get_wizard().Step(SyncSetupWizard::CONFIGURE);
    }
  } else {
    service->get_wizard().Step(SyncSetupWizard::GetLoginState());
  }

  // Show the Sync Setup page.
  scoped_ptr<Value> page(Value::CreateStringValue("syncSetup"));
  web_ui()->CallJavascriptFunction("OptionsPage.navigateToPage", *page);
}

}  // namespace options2
