// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/engine/clear_data_command.h"

#include <string>

#include "chrome/browser/sync/engine/syncer.h"
#include "chrome/browser/sync/engine/syncer_proto_util.h"
#include "chrome/browser/sync/engine/syncproto.h"
#include "chrome/browser/sync/sessions/sync_session.h"
#include "chrome/browser/sync/syncable/directory_manager.h"

using syncable::ScopedDirLookup;

namespace browser_sync {

using sessions::StatusController;
using sessions::SyncSession;
using std::string;
using syncable::FIRST_REAL_MODEL_TYPE;
using syncable::MODEL_TYPE_COUNT;


ClearDataCommand::ClearDataCommand() {}
ClearDataCommand::~ClearDataCommand() {}

SyncerError ClearDataCommand::ExecuteImpl(SyncSession* session) {
  ClientToServerMessage client_to_server_message;
  ClientToServerResponse client_to_server_response;

  client_to_server_message.set_share(session->context()->account_name());
  client_to_server_message.set_message_contents(
      ClientToServerMessage::CLEAR_DATA);

  client_to_server_message.mutable_clear_user_data();

  ScopedDirLookup dir(session->context()->directory_manager(),
                      session->context()->account_name());
  if (!dir.good()) {
    LOG(ERROR) << "Scoped dir lookup failed!";
    return DIRECTORY_LOOKUP_FAILED;
  }

  SyncerProtoUtil::AddRequestBirthday(dir, &client_to_server_message);

  DVLOG(1) << "Clearing server data";

  SyncerError result = SyncerProtoUtil::PostClientToServerMessage(
      client_to_server_message,
      &client_to_server_response,
      session);

  DVLOG(1) << SyncerProtoUtil::ClientToServerResponseDebugString(
      client_to_server_response);

  // TODO(lipalani): This code is wrong.  The response error codes it checks
  // have been obsoleted.  The only reason it hasn't caused problems is that
  // this code is unreachable.  We should do something to clean up this mess.
  // See also: crbug.com/71616.
  //
  // Clear pending indicates that the server has received our clear message
  if (result != SYNCER_OK || !client_to_server_response.has_error_code() ||
      client_to_server_response.error_code() != sync_pb::SyncEnums::SUCCESS) {
    // On failure, subsequent requests to the server will cause it to attempt
    // to resume the clear.  The client will handle disabling of sync in
    // response to a store birthday error from the server.
    SyncEngineEvent event(SyncEngineEvent::CLEAR_SERVER_DATA_FAILED);
    session->context()->NotifyListeners(event);

    LOG(ERROR) << "Error posting ClearData.";

    return result;
  }

  SyncEngineEvent event(SyncEngineEvent::CLEAR_SERVER_DATA_SUCCEEDED);
  session->context()->NotifyListeners(event);

  session->delegate()->OnShouldStopSyncingPermanently();

  DVLOG(1) << "ClearData succeeded.";
  return SYNCER_OK;
}

}  // namespace browser_sync
