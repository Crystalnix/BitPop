// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/internal_api/write_node.h"

#include "base/json/json_writer.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/sync/engine/nigori_util.h"
#include "chrome/browser/sync/engine/syncapi_internal.h"
#include "chrome/browser/sync/internal_api/base_transaction.h"
#include "chrome/browser/sync/internal_api/write_transaction.h"
#include "chrome/browser/sync/protocol/app_specifics.pb.h"
#include "chrome/browser/sync/protocol/autofill_specifics.pb.h"
#include "chrome/browser/sync/protocol/bookmark_specifics.pb.h"
#include "chrome/browser/sync/protocol/extension_specifics.pb.h"
#include "chrome/browser/sync/protocol/password_specifics.pb.h"
#include "chrome/browser/sync/protocol/session_specifics.pb.h"
#include "chrome/browser/sync/protocol/theme_specifics.pb.h"
#include "chrome/browser/sync/protocol/typed_url_specifics.pb.h"
#include "chrome/browser/sync/syncable/syncable.h"
#include "chrome/browser/sync/util/cryptographer.h"

using browser_sync::Cryptographer;
using std::string;
using std::vector;
using syncable::kEncryptedString;
using syncable::SPECIFICS;

namespace sync_api {

static const char kDefaultNameForNewNodes[] = " ";

bool WriteNode::UpdateEntryWithEncryption(
    browser_sync::Cryptographer* cryptographer,
    const sync_pb::EntitySpecifics& new_specifics,
    syncable::MutableEntry* entry) {
  syncable::ModelType type = syncable::GetModelTypeFromSpecifics(new_specifics);
  DCHECK_GE(type, syncable::FIRST_REAL_MODEL_TYPE);
  const sync_pb::EntitySpecifics& old_specifics = entry->Get(SPECIFICS);
  const syncable::ModelTypeSet encrypted_types =
      cryptographer->GetEncryptedTypes();
  sync_pb::EntitySpecifics generated_specifics;
  if (!SpecificsNeedsEncryption(encrypted_types, new_specifics) ||
      !cryptographer->is_initialized()) {
    // No encryption required or we are unable to encrypt.
    generated_specifics.CopyFrom(new_specifics);
  } else {
    // Encrypt new_specifics into generated_specifics.
    if (VLOG_IS_ON(2)) {
      scoped_ptr<DictionaryValue> value(entry->ToValue());
      std::string info;
      base::JSONWriter::Write(value.get(), true, &info);
      DVLOG(2) << "Encrypting specifics of type "
               << syncable::ModelTypeToString(type)
               << " with content: "
               << info;
    }
    // Only copy over the old specifics if it is of the right type and already
    // encrypted. The first time we encrypt a node we start from scratch, hence
    // removing all the unencrypted data, but from then on we only want to
    // update the node if the data changes or the encryption key changes.
    if (syncable::GetModelTypeFromSpecifics(old_specifics) == type &&
        old_specifics.has_encrypted()) {
      generated_specifics.CopyFrom(old_specifics);
    } else {
      syncable::AddDefaultExtensionValue(type, &generated_specifics);
    }
    // Does not change anything if underlying encrypted blob was already up
    // to date and encrypted with the default key.
    if (!cryptographer->Encrypt(new_specifics,
                                generated_specifics.mutable_encrypted())) {
      NOTREACHED() << "Could not encrypt data for node of type "
                   << syncable::ModelTypeToString(type);
      return false;
    }
  }

  // It's possible this entry was encrypted but didn't properly overwrite the
  // non_unique_name (see crbug.com/96314).
  bool encrypted_without_overwriting_name = (old_specifics.has_encrypted() &&
      entry->Get(syncable::NON_UNIQUE_NAME) != kEncryptedString);

  // If we're encrypted but the name wasn't overwritten properly we still want
  // to rewrite the entry, irrespective of whether the specifics match.
  if (!encrypted_without_overwriting_name &&
      old_specifics.SerializeAsString() ==
          generated_specifics.SerializeAsString()) {
    DVLOG(2) << "Specifics of type " << syncable::ModelTypeToString(type)
             << " already match, dropping change.";
    return true;
  }

  if (generated_specifics.has_encrypted()) {
    // Overwrite the possibly sensitive non-specifics data.
    entry->Put(syncable::NON_UNIQUE_NAME, kEncryptedString);
    // For bookmarks we actually put bogus data into the unencrypted specifics,
    // else the server will try to do it for us.
    if (type == syncable::BOOKMARKS) {
      sync_pb::BookmarkSpecifics* bookmark_specifics =
          generated_specifics.MutableExtension(sync_pb::bookmark);
      if (!entry->Get(syncable::IS_DIR))
        bookmark_specifics->set_url(kEncryptedString);
      bookmark_specifics->set_title(kEncryptedString);
    }
  }
  entry->Put(syncable::SPECIFICS, generated_specifics);
  DVLOG(1) << "Overwriting specifics of type "
           << syncable::ModelTypeToString(type)
           << " and marking for syncing.";
  syncable::MarkForSyncing(entry);
  return true;
}

void WriteNode::SetIsFolder(bool folder) {
  if (entry_->Get(syncable::IS_DIR) == folder)
    return;  // Skip redundant changes.

  entry_->Put(syncable::IS_DIR, folder);
  MarkForSyncing();
}

void WriteNode::SetTitle(const std::wstring& title) {
  DCHECK_NE(GetModelType(), syncable::UNSPECIFIED);
  syncable::ModelType type = GetModelType();
  Cryptographer* cryptographer = GetTransaction()->GetCryptographer();
  bool needs_encryption = cryptographer->GetEncryptedTypes().Has(type);

  // If this datatype is encrypted and is not a bookmark, we disregard the
  // specified title in favor of kEncryptedString. For encrypted bookmarks the
  // NON_UNIQUE_NAME will still be kEncryptedString, but we store the real title
  // into the specifics. All strings compared are server legal strings.
  std::string new_legal_title;
  if (type != syncable::BOOKMARKS && needs_encryption) {
    new_legal_title = kEncryptedString;
  } else {
    SyncAPINameToServerName(WideToUTF8(title), &new_legal_title);
  }

  std::string current_legal_title;
  if (syncable::BOOKMARKS == type &&
      entry_->Get(syncable::SPECIFICS).has_encrypted()) {
    // Encrypted bookmarks only have their title in the unencrypted specifics.
    current_legal_title = GetBookmarkSpecifics().title();
  } else {
    // Non-bookmarks and legacy bookmarks (those with no title in their
    // specifics) store their title in NON_UNIQUE_NAME. Non-legacy bookmarks
    // store their title in specifics as well as NON_UNIQUE_NAME.
    current_legal_title = entry_->Get(syncable::NON_UNIQUE_NAME);
  }

  bool title_matches = (current_legal_title == new_legal_title);
  bool encrypted_without_overwriting_name = (needs_encryption &&
      entry_->Get(syncable::NON_UNIQUE_NAME) != kEncryptedString);

  // If the title matches and the NON_UNIQUE_NAME is properly overwritten as
  // necessary, nothing needs to change.
  if (title_matches && !encrypted_without_overwriting_name) {
    DVLOG(2) << "Title matches, dropping change.";
    return;
  }

  // For bookmarks, we also set the title field in the specifics.
  // TODO(zea): refactor bookmarks to not need this functionality.
  if (GetModelType() == syncable::BOOKMARKS) {
    sync_pb::EntitySpecifics specifics = GetEntitySpecifics();
    specifics.MutableExtension(sync_pb::bookmark)->set_title(new_legal_title);
    SetEntitySpecifics(specifics);  // Does it's own encryption checking.
  }

  // For bookmarks, this has to happen after we set the title in the specifics,
  // because the presence of a title in the NON_UNIQUE_NAME is what controls
  // the logic deciding whether this is an empty node or a legacy bookmark.
  // See BaseNode::GetUnencryptedSpecific(..).
  if (needs_encryption)
    entry_->Put(syncable::NON_UNIQUE_NAME, kEncryptedString);
  else
    entry_->Put(syncable::NON_UNIQUE_NAME, new_legal_title);

  DVLOG(1) << "Overwriting title of type "
           << syncable::ModelTypeToString(type)
           << " and marking for syncing.";
  MarkForSyncing();
}

void WriteNode::SetURL(const GURL& url) {
  sync_pb::BookmarkSpecifics new_value = GetBookmarkSpecifics();
  new_value.set_url(url.spec());
  SetBookmarkSpecifics(new_value);
}

void WriteNode::SetAppSpecifics(
    const sync_pb::AppSpecifics& new_value) {
  sync_pb::EntitySpecifics entity_specifics;
  entity_specifics.MutableExtension(sync_pb::app)->CopyFrom(new_value);
  SetEntitySpecifics(entity_specifics);
}

void WriteNode::SetAutofillSpecifics(
    const sync_pb::AutofillSpecifics& new_value) {
  sync_pb::EntitySpecifics entity_specifics;
  entity_specifics.MutableExtension(sync_pb::autofill)->CopyFrom(new_value);
  SetEntitySpecifics(entity_specifics);
}

void WriteNode::SetAutofillProfileSpecifics(
    const sync_pb::AutofillProfileSpecifics& new_value) {
  sync_pb::EntitySpecifics entity_specifics;
  entity_specifics.MutableExtension(sync_pb::autofill_profile)->
      CopyFrom(new_value);
  SetEntitySpecifics(entity_specifics);
}

void WriteNode::SetBookmarkSpecifics(
    const sync_pb::BookmarkSpecifics& new_value) {
  sync_pb::EntitySpecifics entity_specifics;
  entity_specifics.MutableExtension(sync_pb::bookmark)->CopyFrom(new_value);
  SetEntitySpecifics(entity_specifics);
}

void WriteNode::SetNigoriSpecifics(
    const sync_pb::NigoriSpecifics& new_value) {
  sync_pb::EntitySpecifics entity_specifics;
  entity_specifics.MutableExtension(sync_pb::nigori)->CopyFrom(new_value);
  SetEntitySpecifics(entity_specifics);
}

void WriteNode::SetPasswordSpecifics(
    const sync_pb::PasswordSpecificsData& data) {
  DCHECK_EQ(syncable::PASSWORDS, GetModelType());

  Cryptographer* cryptographer = GetTransaction()->GetCryptographer();

  // We have to do the idempotency check here (vs in UpdateEntryWithEncryption)
  // because Passwords have their encrypted data within the PasswordSpecifics,
  // vs within the EntitySpecifics like all the other types.
  const sync_pb::EntitySpecifics& old_specifics = GetEntry()->Get(SPECIFICS);
  sync_pb::EntitySpecifics entity_specifics;
  // Copy over the old specifics if they exist.
  if (syncable::GetModelTypeFromSpecifics(old_specifics) ==
          syncable::PASSWORDS) {
    entity_specifics.CopyFrom(old_specifics);
  } else {
    syncable::AddDefaultExtensionValue(syncable::PASSWORDS,
                                       &entity_specifics);
  }
  sync_pb::PasswordSpecifics* password_specifics =
      entity_specifics.MutableExtension(sync_pb::password);
  // This will only update password_specifics if the underlying unencrypted blob
  // was different from |data| or was not encrypted with the proper passphrase.
  if (!cryptographer->Encrypt(data, password_specifics->mutable_encrypted())) {
    NOTREACHED() << "Failed to encrypt password, possibly due to sync node "
                 << "corruption";
    return;
  }
  SetEntitySpecifics(entity_specifics);
}

void WriteNode::SetThemeSpecifics(
    const sync_pb::ThemeSpecifics& new_value) {
  sync_pb::EntitySpecifics entity_specifics;
  entity_specifics.MutableExtension(sync_pb::theme)->CopyFrom(new_value);
  SetEntitySpecifics(entity_specifics);
}

void WriteNode::SetSessionSpecifics(
    const sync_pb::SessionSpecifics& new_value) {
  sync_pb::EntitySpecifics entity_specifics;
  entity_specifics.MutableExtension(sync_pb::session)->CopyFrom(new_value);
  SetEntitySpecifics(entity_specifics);
}

void WriteNode::SetEntitySpecifics(
    const sync_pb::EntitySpecifics& new_value) {
  syncable::ModelType new_specifics_type =
      syncable::GetModelTypeFromSpecifics(new_value);
  DCHECK_NE(new_specifics_type, syncable::UNSPECIFIED);
  DVLOG(1) << "Writing entity specifics of type "
           << syncable::ModelTypeToString(new_specifics_type);
  // GetModelType() can be unspecified if this is the first time this
  // node is being initialized (see PutModelType()).  Otherwise, it
  // should match |new_specifics_type|.
  if (GetModelType() != syncable::UNSPECIFIED) {
    DCHECK_EQ(new_specifics_type, GetModelType());
  }
  browser_sync::Cryptographer* cryptographer =
      GetTransaction()->GetCryptographer();

  // Preserve unknown fields.
  const sync_pb::EntitySpecifics& old_specifics = entry_->Get(SPECIFICS);
  sync_pb::EntitySpecifics new_specifics;
  new_specifics.CopyFrom(new_value);
  new_specifics.mutable_unknown_fields()->MergeFrom(
      old_specifics.unknown_fields());

  // Will update the entry if encryption was necessary.
  if (!UpdateEntryWithEncryption(cryptographer, new_specifics, entry_)) {
    return;
  }
  if (entry_->Get(SPECIFICS).has_encrypted()) {
    // EncryptIfNecessary already updated the entry for us and marked for
    // syncing if it was needed. Now we just make a copy of the unencrypted
    // specifics so that if this node is updated, we do not have to decrypt the
    // old data. Note that this only modifies the node's local data, not the
    // entry itself.
    SetUnencryptedSpecifics(new_value);
  }

  DCHECK_EQ(new_specifics_type, GetModelType());
}

void WriteNode::ResetFromSpecifics() {
  SetEntitySpecifics(GetEntitySpecifics());
}

void WriteNode::SetTypedUrlSpecifics(
    const sync_pb::TypedUrlSpecifics& new_value) {
  sync_pb::EntitySpecifics entity_specifics;
  entity_specifics.MutableExtension(sync_pb::typed_url)->CopyFrom(new_value);
  SetEntitySpecifics(entity_specifics);
}

void WriteNode::SetExtensionSpecifics(
    const sync_pb::ExtensionSpecifics& new_value) {
  sync_pb::EntitySpecifics entity_specifics;
  entity_specifics.MutableExtension(sync_pb::extension)->CopyFrom(new_value);
  SetEntitySpecifics(entity_specifics);
}

void WriteNode::SetExternalId(int64 id) {
  if (GetExternalId() != id)
    entry_->Put(syncable::LOCAL_EXTERNAL_ID, id);
}

WriteNode::WriteNode(WriteTransaction* transaction)
    : entry_(NULL), transaction_(transaction) {
  DCHECK(transaction);
}

WriteNode::~WriteNode() {
  delete entry_;
}

// Find an existing node matching the ID |id|, and bind this WriteNode to it.
// Return true on success.
bool WriteNode::InitByIdLookup(int64 id) {
  DCHECK(!entry_) << "Init called twice";
  DCHECK_NE(id, kInvalidId);
  entry_ = new syncable::MutableEntry(transaction_->GetWrappedWriteTrans(),
                                      syncable::GET_BY_HANDLE, id);
  return (entry_->good() && !entry_->Get(syncable::IS_DEL) &&
          DecryptIfNecessary());
}

// Find a node by client tag, and bind this WriteNode to it.
// Return true if the write node was found, and was not deleted.
// Undeleting a deleted node is possible by ClientTag.
bool WriteNode::InitByClientTagLookup(syncable::ModelType model_type,
                                      const std::string& tag) {
  DCHECK(!entry_) << "Init called twice";
  if (tag.empty())
    return false;

  const std::string hash = GenerateSyncableHash(model_type, tag);

  entry_ = new syncable::MutableEntry(transaction_->GetWrappedWriteTrans(),
                                      syncable::GET_BY_CLIENT_TAG, hash);
  return (entry_->good() && !entry_->Get(syncable::IS_DEL) &&
          DecryptIfNecessary());
}

bool WriteNode::InitByTagLookup(const std::string& tag) {
  DCHECK(!entry_) << "Init called twice";
  if (tag.empty())
    return false;
  entry_ = new syncable::MutableEntry(transaction_->GetWrappedWriteTrans(),
                                      syncable::GET_BY_SERVER_TAG, tag);
  if (!entry_->good())
    return false;
  if (entry_->Get(syncable::IS_DEL))
    return false;
  syncable::ModelType model_type = GetModelType();
  DCHECK_EQ(syncable::NIGORI, model_type);
  return true;
}

void WriteNode::PutModelType(syncable::ModelType model_type) {
  // Set an empty specifics of the appropriate datatype.  The presence
  // of the specific extension will identify the model type.
  DCHECK(GetModelType() == model_type ||
         GetModelType() == syncable::UNSPECIFIED);  // Immutable once set.

  sync_pb::EntitySpecifics specifics;
  syncable::AddDefaultExtensionValue(model_type, &specifics);
  SetEntitySpecifics(specifics);
}

// Create a new node with default properties, and bind this WriteNode to it.
// Return true on success.
bool WriteNode::InitByCreation(syncable::ModelType model_type,
                               const BaseNode& parent,
                               const BaseNode* predecessor) {
  DCHECK(!entry_) << "Init called twice";
  // |predecessor| must be a child of |parent| or NULL.
  if (predecessor && predecessor->GetParentId() != parent.GetId()) {
    DCHECK(false);
    return false;
  }

  syncable::Id parent_id = parent.GetEntry()->Get(syncable::ID);

  // Start out with a dummy name.  We expect
  // the caller to set a meaningful name after creation.
  string dummy(kDefaultNameForNewNodes);

  entry_ = new syncable::MutableEntry(transaction_->GetWrappedWriteTrans(),
                                      syncable::CREATE, parent_id, dummy);

  if (!entry_->good())
    return false;

  // Entries are untitled folders by default.
  entry_->Put(syncable::IS_DIR, true);

  PutModelType(model_type);

  // Now set the predecessor, which sets IS_UNSYNCED as necessary.
  return PutPredecessor(predecessor);
}

// Create a new node with default properties and a client defined unique tag,
// and bind this WriteNode to it.
// Return true on success. If the tag exists in the database, then
// we will attempt to undelete the node.
// TODO(chron): Code datatype into hash tag.
// TODO(chron): Is model type ever lost?
bool WriteNode::InitUniqueByCreation(syncable::ModelType model_type,
                                     const BaseNode& parent,
                                     const std::string& tag) {
  DCHECK(!entry_) << "Init called twice";
  if (tag.empty()) {
    LOG(WARNING) << "InitUniqueByCreation failed due to empty tag.";
    return false;
  }

  const std::string hash = GenerateSyncableHash(model_type, tag);

  syncable::Id parent_id = parent.GetEntry()->Get(syncable::ID);

  // Start out with a dummy name.  We expect
  // the caller to set a meaningful name after creation.
  string dummy(kDefaultNameForNewNodes);

  // Check if we have this locally and need to undelete it.
  scoped_ptr<syncable::MutableEntry> existing_entry(
      new syncable::MutableEntry(transaction_->GetWrappedWriteTrans(),
                                 syncable::GET_BY_CLIENT_TAG, hash));

  if (existing_entry->good()) {
    if (existing_entry->Get(syncable::IS_DEL)) {
      // Rules for undelete:
      // BASE_VERSION: Must keep the same.
      // ID: Essential to keep the same.
      // META_HANDLE: Must be the same, so we can't "split" the entry.
      // IS_DEL: Must be set to false, will cause reindexing.
      //         This one is weird because IS_DEL is true for "update only"
      //         items. It should be OK to undelete an update only.
      // MTIME/CTIME: Seems reasonable to just leave them alone.
      // IS_UNSYNCED: Must set this to true or face database insurrection.
      //              We do this below this block.
      // IS_UNAPPLIED_UPDATE: Either keep it the same or also set BASE_VERSION
      //                      to SERVER_VERSION. We keep it the same here.
      // IS_DIR: We'll leave it the same.
      // SPECIFICS: Reset it.

      existing_entry->Put(syncable::IS_DEL, false);

      // Client tags are immutable and must be paired with the ID.
      // If a server update comes down with an ID and client tag combo,
      // and it already exists, always overwrite it and store only one copy.
      // We have to undelete entries because we can't disassociate IDs from
      // tags and updates.

      existing_entry->Put(syncable::NON_UNIQUE_NAME, dummy);
      existing_entry->Put(syncable::PARENT_ID, parent_id);
      entry_ = existing_entry.release();
    } else {
      return false;
    }
  } else {
    entry_ = new syncable::MutableEntry(transaction_->GetWrappedWriteTrans(),
                                        syncable::CREATE, parent_id, dummy);
    if (!entry_->good()) {
      return false;
    }

    // Only set IS_DIR for new entries. Don't bitflip undeleted ones.
    entry_->Put(syncable::UNIQUE_CLIENT_TAG, hash);
  }

  // We don't support directory and tag combinations.
  entry_->Put(syncable::IS_DIR, false);

  // Will clear specifics data.
  PutModelType(model_type);

  // Now set the predecessor, which sets IS_UNSYNCED as necessary.
  return PutPredecessor(NULL);
}

bool WriteNode::SetPosition(const BaseNode& new_parent,
                            const BaseNode* predecessor) {
  // |predecessor| must be a child of |new_parent| or NULL.
  if (predecessor && predecessor->GetParentId() != new_parent.GetId()) {
    DCHECK(false);
    return false;
  }

  syncable::Id new_parent_id = new_parent.GetEntry()->Get(syncable::ID);

  // Filter out redundant changes if both the parent and the predecessor match.
  if (new_parent_id == entry_->Get(syncable::PARENT_ID)) {
    const syncable::Id& old = entry_->Get(syncable::PREV_ID);
    if ((!predecessor && old.IsRoot()) ||
        (predecessor && (old == predecessor->GetEntry()->Get(syncable::ID)))) {
      return true;
    }
  }

  // Atomically change the parent. This will fail if it would
  // introduce a cycle in the hierarchy.
  if (!entry_->Put(syncable::PARENT_ID, new_parent_id))
    return false;

  // Now set the predecessor, which sets IS_UNSYNCED as necessary.
  return PutPredecessor(predecessor);
}

const syncable::Entry* WriteNode::GetEntry() const {
  return entry_;
}

const BaseTransaction* WriteNode::GetTransaction() const {
  return transaction_;
}

void WriteNode::Remove() {
  entry_->Put(syncable::IS_DEL, true);
  MarkForSyncing();
}

bool WriteNode::PutPredecessor(const BaseNode* predecessor) {
  syncable::Id predecessor_id = predecessor ?
      predecessor->GetEntry()->Get(syncable::ID) : syncable::Id();
  if (!entry_->PutPredecessor(predecessor_id))
    return false;
  // Mark this entry as unsynced, to wake up the syncer.
  MarkForSyncing();

  return true;
}

void WriteNode::SetFaviconBytes(const vector<unsigned char>& bytes) {
  sync_pb::BookmarkSpecifics new_value = GetBookmarkSpecifics();
  new_value.set_favicon(bytes.empty() ? NULL : &bytes[0], bytes.size());
  SetBookmarkSpecifics(new_value);
}

void WriteNode::MarkForSyncing() {
  syncable::MarkForSyncing(entry_);
}

} // namespace sync_api
