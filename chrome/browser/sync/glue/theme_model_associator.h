// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_GLUE_THEME_MODEL_ASSOCIATOR_H_
#define CHROME_BROWSER_SYNC_GLUE_THEME_MODEL_ASSOCIATOR_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "chrome/browser/sync/glue/data_type_error_handler.h"
#include "chrome/browser/sync/glue/model_associator.h"
#include "sync/internal_api/public/base/model_type.h"

class ProfileSyncService;

namespace browser_sync {

// Contains all logic for associating the Chrome themes model and the
// sync themes model.
class ThemeModelAssociator : public AssociatorInterface {
 public:
  ThemeModelAssociator(ProfileSyncService* sync_service,
                       DataTypeErrorHandler* error_handler);
  virtual ~ThemeModelAssociator();

  // Used by profile_sync_test_util.h.
  static syncer::ModelType model_type() { return syncer::THEMES; }

  // AssociatorInterface implementation.
  virtual syncer::SyncError AssociateModels() OVERRIDE;
  virtual syncer::SyncError DisassociateModels() OVERRIDE;
  virtual bool SyncModelHasUserCreatedNodes(bool* has_nodes) OVERRIDE;
  virtual void AbortAssociation() OVERRIDE {
    // No implementation needed, this associator runs on the main
    // thread.
  }
  virtual bool CryptoReadyIfNecessary() OVERRIDE;

 private:
  ProfileSyncService* sync_service_;
  DataTypeErrorHandler* error_handler_;

  DISALLOW_COPY_AND_ASSIGN(ThemeModelAssociator);
};

}  // namespace browser_sync

#endif  // CHROME_BROWSER_SYNC_GLUE_THEME_MODEL_ASSOCIATOR_H_
