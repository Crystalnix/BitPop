// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/glue/autofill_data_type_controller.h"

#include "base/bind.h"
#include "base/metrics/histogram.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sync/profile_sync_components_factory.h"
#include "chrome/browser/webdata/web_data_service.h"
#include "chrome/browser/webdata/web_data_service_factory.h"
#include "chrome/common/chrome_notification_types.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_source.h"
#include "sync/api/sync_error.h"

using content::BrowserThread;

namespace browser_sync {

AutofillDataTypeController::AutofillDataTypeController(
    ProfileSyncComponentsFactory* profile_sync_factory,
    Profile* profile,
    ProfileSyncService* sync_service)
    : NewNonFrontendDataTypeController(
        profile_sync_factory, profile, sync_service) {
}

syncer::ModelType AutofillDataTypeController::type() const {
  return syncer::AUTOFILL;
}

syncer::ModelSafeGroup AutofillDataTypeController::model_safe_group()
    const {
  return syncer::GROUP_DB;
}

void AutofillDataTypeController::Observe(
    int notification_type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK_EQ(chrome::NOTIFICATION_WEB_DATABASE_LOADED, notification_type);
  DCHECK_EQ(MODEL_STARTING, state());
  notification_registrar_.RemoveAll();
  OnModelLoaded();
}

AutofillDataTypeController::~AutofillDataTypeController() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
}

bool AutofillDataTypeController::PostTaskOnBackendThread(
    const tracked_objects::Location& from_here,
    const base::Closure& task) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  return BrowserThread::PostTask(BrowserThread::DB, from_here, task);
}

bool AutofillDataTypeController::StartModels() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK_EQ(MODEL_STARTING, state());

  web_data_service_ = WebDataServiceFactory::GetForProfile(
      profile(), Profile::IMPLICIT_ACCESS);
  if (web_data_service_->IsDatabaseLoaded()) {
    return true;
  } else {
    notification_registrar_.Add(
        this, chrome::NOTIFICATION_WEB_DATABASE_LOADED,
        content::Source<WebDataService>(web_data_service_.get()));
    return false;
  }
}

void AutofillDataTypeController::StopModels() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(state() == STOPPING || state() == NOT_RUNNING || state() == DISABLED);
  DVLOG(1) << "AutofillDataTypeController::StopModels() : State = " << state();
  notification_registrar_.RemoveAll();
}

}  // namespace browser_sync
