// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/notifier/invalidation_util.h"

#include <sstream>

#include "google/cacheinvalidation/v2/types.h"
#include "google/cacheinvalidation/v2/types.pb.h"

namespace sync_notifier {

void RunAndDeleteClosure(invalidation::Closure* task) {
  task->Run();
  delete task;
}

bool RealModelTypeToObjectId(syncable::ModelType model_type,
                             invalidation::ObjectId* object_id) {
  std::string notification_type;
  if (!syncable::RealModelTypeToNotificationType(
          model_type, &notification_type)) {
    return false;
  }
  object_id->Init(ipc::invalidation::ObjectSource::CHROME_SYNC,
                  notification_type);
  return true;
}

bool ObjectIdToRealModelType(const invalidation::ObjectId& object_id,
                             syncable::ModelType* model_type) {
  return
      syncable::NotificationTypeToRealModelType(
          object_id.name(), model_type);
}

std::string ObjectIdToString(
    const invalidation::ObjectId& object_id) {
  std::stringstream ss;
  ss << "{ ";
  ss << "name: " << object_id.name() << ", ";
  ss << "source: " << object_id.source();
  ss << " }";
  return ss.str();
}

std::string InvalidationToString(
    const invalidation::Invalidation& invalidation) {
  std::stringstream ss;
  ss << "{ ";
  ss << "object_id: " << ObjectIdToString(invalidation.object_id()) << ", ";
  ss << "version: " << invalidation.version();
  ss << " }";
  return ss.str();
}

}  // namespace sync_notifier
