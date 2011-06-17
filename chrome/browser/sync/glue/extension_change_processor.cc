// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/glue/extension_change_processor.h"

#include <sstream>
#include <string>

#include "base/logging.h"
#include "base/stl_util-inl.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_sync_data.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sync/glue/extension_sync.h"
#include "chrome/browser/sync/glue/extension_util.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "chrome/browser/sync/protocol/extension_specifics.pb.h"
#include "chrome/common/extensions/extension.h"
#include "content/browser/browser_thread.h"
#include "content/common/notification_details.h"
#include "content/common/notification_source.h"

namespace browser_sync {

ExtensionChangeProcessor::ExtensionChangeProcessor(
    const ExtensionSyncTraits& traits,
    UnrecoverableErrorHandler* error_handler)
    : ChangeProcessor(error_handler),
      traits_(traits),
      profile_(NULL),
      extension_service_(NULL),
      user_share_(NULL) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(error_handler);
}

ExtensionChangeProcessor::~ExtensionChangeProcessor() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
}

// TODO(akalin): We need to make sure events we receive from either
// the browser or the syncapi are done in order; this is tricky since
// some events (e.g., extension installation) are done asynchronously.

void ExtensionChangeProcessor::Observe(NotificationType type,
                                       const NotificationSource& source,
                                       const NotificationDetails& details) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(running());
  DCHECK(profile_);
  if ((type != NotificationType::EXTENSION_INSTALLED) &&
      (type != NotificationType::EXTENSION_UNINSTALLED) &&
      (type != NotificationType::EXTENSION_LOADED) &&
      (type != NotificationType::EXTENSION_UPDATE_DISABLED) &&
      (type != NotificationType::EXTENSION_UNLOADED)) {
    LOG(DFATAL) << "Received unexpected notification of type "
                << type.value;
    return;
  }

  DCHECK_EQ(Source<Profile>(source).ptr(), profile_);
  if (type == NotificationType::EXTENSION_UNINSTALLED) {
    const UninstalledExtensionInfo* uninstalled_extension_info =
        Details<UninstalledExtensionInfo>(details).ptr();
    CHECK(uninstalled_extension_info);
    if (traits_.should_handle_extension_uninstall(
            *uninstalled_extension_info)) {
      const std::string& id = uninstalled_extension_info->extension_id;
      VLOG(1) << "Removing server data for uninstalled extension " << id
              << " of type " << uninstalled_extension_info->extension_type;
      RemoveServerData(traits_, id, user_share_);
    }
  } else {
    const Extension* extension = NULL;
    if (type == NotificationType::EXTENSION_UNLOADED) {
      extension = Details<UnloadedExtensionInfo>(details)->extension;
    } else {
      extension = Details<const Extension>(details).ptr();
    }
    CHECK(extension);
    VLOG(1) << "Updating server data for extension " << extension->id()
            << " (notification type = " << type.value << ")";
    if (!traits_.is_valid_and_syncable(*extension)) {
      return;
    }
    std::string error;
    if (!UpdateServerData(traits_, *extension, *extension_service_,
                          user_share_, &error)) {
      error_handler()->OnUnrecoverableError(FROM_HERE, error);
    }
  }
}

void ExtensionChangeProcessor::ApplyChangesFromSyncModel(
    const sync_api::BaseTransaction* trans,
    const sync_api::SyncManager::ChangeRecord* changes,
    int change_count) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (!running()) {
    return;
  }
  for (int i = 0; i < change_count; ++i) {
    const sync_api::SyncManager::ChangeRecord& change = changes[i];
    sync_pb::ExtensionSpecifics specifics;
    switch (change.action) {
      case sync_api::SyncManager::ChangeRecord::ACTION_ADD:
      case sync_api::SyncManager::ChangeRecord::ACTION_UPDATE: {
        sync_api::ReadNode node(trans);
        if (!node.InitByIdLookup(change.id)) {
          std::stringstream error;
          error << "Extension node lookup failed for change " << change.id
                << " of action type " << change.action;
          error_handler()->OnUnrecoverableError(FROM_HERE, error.str());
          return;
        }
        DCHECK_EQ(node.GetModelType(), traits_.model_type);
        specifics = (*traits_.extension_specifics_getter)(node);
        break;
      }
      case sync_api::SyncManager::ChangeRecord::ACTION_DELETE: {
        if (!(*traits_.extension_specifics_entity_getter)(
                change.specifics, &specifics)) {
          std::stringstream error;
          error << "Could not get extension specifics from deleted node "
                << change.id;
          error_handler()->OnUnrecoverableError(FROM_HERE, error.str());
          LOG(DFATAL) << error.str();
        }
        break;
      }
    }
    ExtensionSyncData sync_data;
    if (!GetExtensionSyncData(specifics, &sync_data)) {
      // TODO(akalin): Should probably recover or drop.
      std::string error =
          std::string("Invalid server specifics: ") +
          ExtensionSpecificsToString(specifics);
      error_handler()->OnUnrecoverableError(FROM_HERE, error);
      return;
    }
    sync_data.uninstalled =
        (change.action == sync_api::SyncManager::ChangeRecord::ACTION_DELETE);
    StopObserving();
    extension_service_->ProcessSyncData(sync_data,
                                        traits_.is_valid_and_syncable);
    StartObserving();
  }
}

void ExtensionChangeProcessor::StartImpl(Profile* profile) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  profile_ = profile;
  extension_service_ = profile_->GetExtensionService();
  user_share_ = profile_->GetProfileSyncService()->GetUserShare();
  DCHECK(profile_);
  DCHECK(extension_service_);
  DCHECK(user_share_);
  StartObserving();
}

void ExtensionChangeProcessor::StopImpl() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  StopObserving();
  profile_ = NULL;
  extension_service_ = NULL;
  user_share_ = NULL;
}

void ExtensionChangeProcessor::StartObserving() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(profile_);
  notification_registrar_.Add(
      this, NotificationType::EXTENSION_INSTALLED,
      Source<Profile>(profile_));
  notification_registrar_.Add(
      this, NotificationType::EXTENSION_UNINSTALLED,
      Source<Profile>(profile_));

  notification_registrar_.Add(
      this, NotificationType::EXTENSION_LOADED,
      Source<Profile>(profile_));
  // Despite the name, this notification is exactly like
  // EXTENSION_LOADED but with an initial state of DISABLED.
  notification_registrar_.Add(
      this, NotificationType::EXTENSION_UPDATE_DISABLED,
      Source<Profile>(profile_));

  notification_registrar_.Add(
      this, NotificationType::EXTENSION_UNLOADED,
      Source<Profile>(profile_));
}

void ExtensionChangeProcessor::StopObserving() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(profile_);
  VLOG(1) << "Unobserving all notifications";
  notification_registrar_.RemoveAll();
}

}  // namespace browser_sync
