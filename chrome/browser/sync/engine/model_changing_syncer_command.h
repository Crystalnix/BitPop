// Copyright (c) 2006-2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_ENGINE_MODEL_CHANGING_SYNCER_COMMAND_H_
#define CHROME_BROWSER_SYNC_ENGINE_MODEL_CHANGING_SYNCER_COMMAND_H_
#pragma once

#include "chrome/browser/sync/engine/syncer_command.h"

namespace browser_sync {
namespace sessions {
class SyncSession;
}

// An abstract SyncerCommand which dispatches its Execute step to the
// model-safe worker thread.  Classes derived from ModelChangingSyncerCommand
// instead of SyncerCommand must implement ModelChangingExecuteImpl instead of
// ExecuteImpl, but otherwise, the contract is the same.
//
// A command should derive from ModelChangingSyncerCommand instead of
// SyncerCommand whenever the operation might change any client-visible
// fields on any syncable::Entry.  If the operation involves creating a
// WriteTransaction, this is a sign that ModelChangingSyncerCommand is likely
// necessary.
class ModelChangingSyncerCommand : public SyncerCommand {
 public:
  ModelChangingSyncerCommand() : work_session_(NULL) { }
  virtual ~ModelChangingSyncerCommand() { }

  // SyncerCommand implementation. Sets work_session to session.
  virtual void ExecuteImpl(sessions::SyncSession* session);

  // wrapper so implementations don't worry about storing work_session
  void StartChangingModel() {
    ModelChangingExecuteImpl(work_session_);
  }

  // Sometimes, a command has work to do that needs to touch global state
  // belonging to multiple ModelSafeGroups, but in a way that is known to be
  // safe.  This will be called once, prior to ModelChangingExecuteImpl,
  // *without* a ModelSafeGroup restriction in place on the SyncSession.
  // Returns true on success, false on failure.
  // TODO(tim): Remove this (bug 36594).
  virtual bool ModelNeutralExecuteImpl(sessions::SyncSession* session);

  // Abstract method to be implemented by subclasses to handle logic that
  // operates on the model.  This is invoked with a SyncSession ModelSafeGroup
  // restriction in place so that bits of state belonging to data types
  // running on an unsafe thread are siloed away.
  virtual void ModelChangingExecuteImpl(sessions::SyncSession* session) = 0;

 private:
  // ExecuteImpl is expected to be run by SyncerCommand to set work_session.
  // StartChangingModel is called to start this command running.
  // Implementations will implement ModelChangingExecuteImpl and not
  // worry about storing the session or setting it. They are given work_session.
  sessions::SyncSession* work_session_;

  DISALLOW_COPY_AND_ASSIGN(ModelChangingSyncerCommand);
};

}  // namespace browser_sync

#endif  // CHROME_BROWSER_SYNC_ENGINE_MODEL_CHANGING_SYNCER_COMMAND_H_
