// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_GLUE_MODEL_ASSOCIATOR_H_
#define CHROME_BROWSER_SYNC_GLUE_MODEL_ASSOCIATOR_H_
#pragma once

#include "base/basictypes.h"
#include "base/synchronization/lock.h"
#include "chrome/browser/sync/syncable/model_type.h"

class SyncError;

namespace sync_api {
class BaseNode;
}

namespace browser_sync {

// This represents the fundamental operations used for model association that
// are common to all ModelAssociators and do not depend on types of the models
// being associated.
class AssociatorInterface {
 public:
  virtual ~AssociatorInterface() {}

  // Iterates through both the sync and the chrome model looking for
  // matched pairs of items. After successful completion, the models
  // should be identical and corresponding. Returns true on
  // success. On failure of this step, we should abort the sync
  // operation and report an error to the user.
  // TODO(zea): return a SyncError instead of passing one in.
  virtual bool AssociateModels(SyncError* error) = 0;

  // Clears all the associations between the chrome and sync models.
  // TODO(zea): return a SyncError instead of passing one in.
  virtual bool DisassociateModels(SyncError* error) = 0;

  // The has_nodes out parameter is set to true if the sync model has
  // nodes other than the permanent tagged nodes.  The method may
  // return false if an error occurred.
  virtual bool SyncModelHasUserCreatedNodes(bool* has_nodes) = 0;

  // Calling this method while AssociateModels() is in progress will
  // cause the method to exit early with a "false" return value.  This
  // is useful for aborting model associations for shutdown.  This
  // method is only implemented for model associators that are invoked
  // off the main thread.
  virtual void AbortAssociation() = 0;

  // Returns whether the datatype is ready for encryption/decryption if the
  // sync service requires it.
  // TODO(zea): This should be implemented automatically for each datatype, see
  // http://crbug.com/76232.
  virtual bool CryptoReadyIfNecessary() = 0;
};

// In addition to the generic methods, association can refer to operations
// that depend on the types of the actual IDs we are associating and the
// underlying node type in the browser.  We collect these into a templatized
// interface that encapsulates everything you need to implement to have a model
// associator for a specific data type.
// This template is appropriate for data types where a Node* makes sense for
// referring to a particular item.  If we encounter a type that does not fit
// in this world, we may want to have several PerDataType templates.
template <class Node, class IDType>
class PerDataTypeAssociatorInterface : public AssociatorInterface {
 public:
  virtual ~PerDataTypeAssociatorInterface() {}
  // Returns sync id for the given chrome model id.
  // Returns sync_api::kInvalidId if the sync node is not found for the given
  // chrome id.
  virtual int64 GetSyncIdFromChromeId(const IDType& id) = 0;

  // Returns the chrome node for the given sync id.
  // Returns NULL if no node is found for the given sync id.
  virtual const Node* GetChromeNodeFromSyncId(int64 sync_id) = 0;

  // Initializes the given sync node from the given chrome node id.
  // Returns false if no sync node was found for the given chrome node id or
  // if the initialization of sync node fails.
  virtual bool InitSyncNodeFromChromeId(const IDType& node_id,
                                        sync_api::BaseNode* sync_node) = 0;

  // Associates the given chrome node with the given sync id.
  virtual void Associate(const Node* node, int64 sync_id) = 0;

  // Remove the association that corresponds to the given sync id.
  virtual void Disassociate(int64 sync_id) = 0;
};

template <class Node, class IDType>
class AbortablePerDataTypeAssociatorInterface
    : public PerDataTypeAssociatorInterface<Node, IDType> {
 public:
  AbortablePerDataTypeAssociatorInterface() : pending_abort_(false) {}

  // Implementation of AssociatorInterface methods.
  virtual void AbortAssociation() {
    base::AutoLock lock(pending_abort_lock_);
    pending_abort_ = true;
  }

 protected:
  // Overridable by tests.
  virtual bool IsAbortPending() {
    base::AutoLock lock(pending_abort_lock_);
    return pending_abort_;
  }

  // Lock to ensure exclusive access to the pending_abort_ flag.
  base::Lock pending_abort_lock_;
  // Set to true if there's a pending abort.
  bool pending_abort_;
};

}  // namespace browser_sync

#endif  // CHROME_BROWSER_SYNC_GLUE_MODEL_ASSOCIATOR_H_
