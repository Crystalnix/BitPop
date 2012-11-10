// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/glue/theme_model_associator.h"

#include "base/basictypes.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/sync/glue/sync_backend_host.h"
#include "chrome/browser/sync/glue/theme_util.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "sync/api/sync_error.h"
#include "sync/internal_api/public/read_node.h"
#include "sync/internal_api/public/read_transaction.h"
#include "sync/internal_api/public/write_node.h"
#include "sync/internal_api/public/write_transaction.h"
#include "sync/protocol/theme_specifics.pb.h"

namespace browser_sync {

namespace {

static const char kThemesTag[] = "google_chrome_themes";
static const char kCurrentThemeNodeTitle[] = "Current Theme";

static const char kNoThemesFolderError[] =
    "Server did not create the top-level themes node. We "
    "might be running against an out-of-date server.";

}  // namespace

ThemeModelAssociator::ThemeModelAssociator(
    ProfileSyncService* sync_service,
    DataTypeErrorHandler* error_handler)
    : sync_service_(sync_service),
      error_handler_(error_handler) {
  DCHECK(sync_service_);
}

ThemeModelAssociator::~ThemeModelAssociator() {}

syncer::SyncError ThemeModelAssociator::AssociateModels() {
  syncer::WriteTransaction trans(FROM_HERE, sync_service_->GetUserShare());
  syncer::ReadNode root(&trans);
  if (root.InitByTagLookup(kThemesTag) != syncer::BaseNode::INIT_OK) {
    return error_handler_->CreateAndUploadError(FROM_HERE,
                                                kNoThemesFolderError,
                                                model_type());
  }

  Profile* profile = sync_service_->profile();
  syncer::WriteNode node(&trans);
  // TODO(akalin): When we have timestamps, we may want to do
  // something more intelligent than preferring the sync data over our
  // local data.
  if (node.InitByClientTagLookup(syncer::THEMES, kCurrentThemeClientTag) ==
      syncer::BaseNode::INIT_OK) {
    // Update the current theme from the sync data.
    // TODO(akalin): If the sync data does not have
    // use_system_theme_by_default and we do, update that flag on the
    // sync data.
    sync_pb::ThemeSpecifics theme_specifics = node.GetThemeSpecifics();
    if (UpdateThemeSpecificsOrSetCurrentThemeIfNecessary(profile,
                                                         &theme_specifics))
      node.SetThemeSpecifics(theme_specifics);
  } else {
    // Set the sync data from the current theme.
    syncer::WriteNode node(&trans);
    syncer::WriteNode::InitUniqueByCreationResult result =
        node.InitUniqueByCreation(syncer::THEMES, root,
                                  kCurrentThemeClientTag);
    if (result != syncer::WriteNode::INIT_SUCCESS) {
      return error_handler_->CreateAndUploadError(
          FROM_HERE,
          "Could not create current theme node.",
          model_type());
    }
    node.SetIsFolder(false);
    node.SetTitle(UTF8ToWide(kCurrentThemeNodeTitle));
    sync_pb::ThemeSpecifics theme_specifics;
    GetThemeSpecificsFromCurrentTheme(profile, &theme_specifics);
    node.SetThemeSpecifics(theme_specifics);
  }
  return syncer::SyncError();
}

syncer::SyncError ThemeModelAssociator::DisassociateModels() {
  // We don't maintain any association state, so nothing to do.
  return syncer::SyncError();
}

bool ThemeModelAssociator::SyncModelHasUserCreatedNodes(bool* has_nodes) {
  DCHECK(has_nodes);
  *has_nodes = false;
  syncer::ReadTransaction trans(FROM_HERE, sync_service_->GetUserShare());
  syncer::ReadNode root(&trans);
  if (root.InitByTagLookup(kThemesTag) != syncer::BaseNode::INIT_OK) {
    LOG(ERROR) << kNoThemesFolderError;
    return false;
  }
  // The sync model has user created nodes iff the themes folder has
  // any children.
  *has_nodes = root.HasChildren();
  return true;
}

bool ThemeModelAssociator::CryptoReadyIfNecessary() {
  // We only access the cryptographer while holding a transaction.
  syncer::ReadTransaction trans(FROM_HERE, sync_service_->GetUserShare());
  const syncer::ModelTypeSet encrypted_types =
      syncer::GetEncryptedTypes(&trans);
  return !encrypted_types.Has(syncer::THEMES) ||
         sync_service_->IsCryptographerReady(&trans);
}

}  // namespace browser_sync
