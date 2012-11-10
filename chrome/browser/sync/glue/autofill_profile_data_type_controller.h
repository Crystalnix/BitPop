// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_GLUE_AUTOFILL_PROFILE_DATA_TYPE_CONTROLLER_H_
#define CHROME_BROWSER_SYNC_GLUE_AUTOFILL_PROFILE_DATA_TYPE_CONTROLLER_H_

#include "base/compiler_specific.h"
#include "base/memory/ref_counted.h"
#include "chrome/browser/autofill/personal_data_manager_observer.h"
#include "chrome/browser/sync/glue/new_non_frontend_data_type_controller.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"

class PersonalDataManager;
class WebDataService;

namespace browser_sync {

class AutofillProfileDataTypeController
    : public NewNonFrontendDataTypeController,
      public content::NotificationObserver,
      public PersonalDataManagerObserver {
 public:
  AutofillProfileDataTypeController(
      ProfileSyncComponentsFactory* profile_sync_factory,
      Profile* profile,
      ProfileSyncService* sync_service);

  // NewNonFrontendDataTypeController implementation.
  virtual syncer::ModelType type() const OVERRIDE;
  virtual syncer::ModelSafeGroup model_safe_group() const OVERRIDE;

  // content::NotificationObserver implementation.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // PersonalDataManagerObserver implementation:
  virtual void OnPersonalDataChanged() OVERRIDE;

 protected:
  virtual ~AutofillProfileDataTypeController();

  // NewNonFrontendDataTypeController implementation.
  virtual bool PostTaskOnBackendThread(
      const tracked_objects::Location& from_here,
      const base::Closure& task) OVERRIDE;
  virtual bool StartModels() OVERRIDE;
  virtual void StopModels() OVERRIDE;

 private:
  PersonalDataManager* personal_data_;
  scoped_refptr<WebDataService> web_data_service_;
  content::NotificationRegistrar notification_registrar_;

  DISALLOW_COPY_AND_ASSIGN(AutofillProfileDataTypeController);
};

}  // namespace browser_sync

#endif  // CHROME_BROWSER_SYNC_GLUE_AUTOFILL_PROFILE_DATA_TYPE_CONTROLLER_H_
