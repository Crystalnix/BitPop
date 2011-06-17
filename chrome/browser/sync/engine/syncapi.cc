// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/engine/syncapi.h"

#include <algorithm>
#include <bitset>
#include <iomanip>
#include <list>
#include <queue>
#include <string>
#include <vector>

#include "base/base64.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "base/observer_list.h"
#include "base/sha1.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/synchronization/lock.h"
#include "base/task.h"
#include "base/time.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/sync/engine/all_status.h"
#include "chrome/browser/sync/engine/change_reorder_buffer.h"
#include "chrome/browser/sync/engine/model_safe_worker.h"
#include "chrome/browser/sync/engine/nudge_source.h"
#include "chrome/browser/sync/engine/net/server_connection_manager.h"
#include "chrome/browser/sync/engine/net/syncapi_server_connection_manager.h"
#include "chrome/browser/sync/engine/syncer.h"
#include "chrome/browser/sync/engine/syncer_thread.h"
#include "chrome/browser/sync/engine/http_post_provider_factory.h"
#include "chrome/browser/sync/js_arg_list.h"
#include "chrome/browser/sync/js_backend.h"
#include "chrome/browser/sync/js_event_router.h"
#include "chrome/browser/sync/notifier/sync_notifier.h"
#include "chrome/browser/sync/notifier/sync_notifier_observer.h"
#include "chrome/browser/sync/protocol/app_specifics.pb.h"
#include "chrome/browser/sync/protocol/autofill_specifics.pb.h"
#include "chrome/browser/sync/protocol/bookmark_specifics.pb.h"
#include "chrome/browser/sync/protocol/extension_specifics.pb.h"
#include "chrome/browser/sync/protocol/nigori_specifics.pb.h"
#include "chrome/browser/sync/protocol/preference_specifics.pb.h"
#include "chrome/browser/sync/protocol/proto_value_conversions.h"
#include "chrome/browser/sync/protocol/service_constants.h"
#include "chrome/browser/sync/protocol/session_specifics.pb.h"
#include "chrome/browser/sync/protocol/sync.pb.h"
#include "chrome/browser/sync/protocol/theme_specifics.pb.h"
#include "chrome/browser/sync/protocol/typed_url_specifics.pb.h"
#include "chrome/browser/sync/sessions/sync_session.h"
#include "chrome/browser/sync/sessions/sync_session_context.h"
#include "chrome/browser/sync/syncable/autofill_migration.h"
#include "chrome/browser/sync/syncable/directory_change_listener.h"
#include "chrome/browser/sync/syncable/directory_manager.h"
#include "chrome/browser/sync/syncable/model_type_payload_map.h"
#include "chrome/browser/sync/syncable/model_type.h"
#include "chrome/browser/sync/syncable/nigori_util.h"
#include "chrome/browser/sync/syncable/syncable.h"
#include "chrome/browser/sync/util/crypto_helpers.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/deprecated/event_sys.h"
#include "chrome/common/net/gaia/gaia_authenticator.h"
#include "content/browser/browser_thread.h"
#include "net/base/network_change_notifier.h"

using base::TimeDelta;
using browser_sync::AllStatus;
using browser_sync::Cryptographer;
using browser_sync::KeyParams;
using browser_sync::ModelSafeRoutingInfo;
using browser_sync::ModelSafeWorker;
using browser_sync::ModelSafeWorkerRegistrar;
using browser_sync::ServerConnectionEvent;
using browser_sync::ServerConnectionEvent2;
using browser_sync::ServerConnectionEventListener;
using browser_sync::SyncEngineEvent;
using browser_sync::SyncEngineEventListener;
using browser_sync::Syncer;
using browser_sync::SyncerThread;
using browser_sync::kNigoriTag;
using browser_sync::sessions::SyncSessionContext;
using std::list;
using std::hex;
using std::string;
using std::vector;
using syncable::Directory;
using syncable::DirectoryManager;
using syncable::Entry;
using syncable::ModelTypeBitSet;
using syncable::OriginalEntries;
using syncable::WriterTag;
using syncable::SPECIFICS;
using sync_pb::AutofillProfileSpecifics;

typedef GoogleServiceAuthError AuthError;

static const int kThreadExitTimeoutMsec = 60000;
static const int kSSLPort = 443;
static const int kSyncerThreadDelayMsec = 250;

#if defined(OS_CHROMEOS)
static const int kChromeOSNetworkChangeReactionDelayHackMsec = 5000;
#endif  // OS_CHROMEOS

// We manage the lifetime of sync_api::SyncManager::SyncInternal ourselves.
DISABLE_RUNNABLE_METHOD_REFCOUNT(sync_api::SyncManager::SyncInternal);

namespace sync_api {

static const FilePath::CharType kBookmarkSyncUserSettingsDatabase[] =
    FILE_PATH_LITERAL("BookmarkSyncSettings.sqlite3");
static const char kDefaultNameForNewNodes[] = " ";

// The list of names which are reserved for use by the server.
static const char* kForbiddenServerNames[] = { "", ".", ".." };

//////////////////////////////////////////////////////////////////////////
// Static helper functions.

// Helper function to look up the int64 metahandle of an object given the ID
// string.
static int64 IdToMetahandle(syncable::BaseTransaction* trans,
                            const syncable::Id& id) {
  syncable::Entry entry(trans, syncable::GET_BY_ID, id);
  if (!entry.good())
    return kInvalidId;
  return entry.Get(syncable::META_HANDLE);
}

// Checks whether |name| is a server-illegal name followed by zero or more space
// characters.  The three server-illegal names are the empty string, dot, and
// dot-dot.  Very long names (>255 bytes in UTF-8 Normalization Form C) are
// also illegal, but are not considered here.
static bool IsNameServerIllegalAfterTrimming(const std::string& name) {
  size_t untrimmed_count = name.find_last_not_of(' ') + 1;
  for (size_t i = 0; i < arraysize(kForbiddenServerNames); ++i) {
    if (name.compare(0, untrimmed_count, kForbiddenServerNames[i]) == 0)
      return true;
  }
  return false;
}

static bool EndsWithSpace(const std::string& string) {
  return !string.empty() && *string.rbegin() == ' ';
}

// When taking a name from the syncapi, append a space if it matches the
// pattern of a server-illegal name followed by zero or more spaces.
static void SyncAPINameToServerName(const std::wstring& sync_api_name,
                                    std::string* out) {
  *out = WideToUTF8(sync_api_name);
  if (IsNameServerIllegalAfterTrimming(*out))
    out->append(" ");
}

// In the reverse direction, if a server name matches the pattern of a
// server-illegal name followed by one or more spaces, remove the trailing
// space.
static void ServerNameToSyncAPIName(const std::string& server_name,
                                    std::wstring* out) {
  int length_to_copy = server_name.length();
  if (IsNameServerIllegalAfterTrimming(server_name) &&
      EndsWithSpace(server_name))
    --length_to_copy;
  if (!UTF8ToWide(server_name.c_str(), length_to_copy, out)) {
    NOTREACHED() << "Could not convert server name from UTF8 to wide";
  }
}

UserShare::UserShare() {}

UserShare::~UserShare() {}

////////////////////////////////////
// BaseNode member definitions.

BaseNode::BaseNode() {}

BaseNode::~BaseNode() {}

std::string BaseNode::GenerateSyncableHash(
    syncable::ModelType model_type, const std::string& client_tag) {
  // blank PB with just the extension in it has termination symbol,
  // handy for delimiter
  sync_pb::EntitySpecifics serialized_type;
  syncable::AddDefaultExtensionValue(model_type, &serialized_type);
  std::string hash_input;
  serialized_type.AppendToString(&hash_input);
  hash_input.append(client_tag);

  std::string encode_output;
  CHECK(base::Base64Encode(base::SHA1HashString(hash_input), &encode_output));
  return encode_output;
}

sync_pb::PasswordSpecificsData* DecryptPasswordSpecifics(
    const sync_pb::EntitySpecifics& specifics, Cryptographer* crypto) {
  if (!specifics.HasExtension(sync_pb::password))
    return NULL;
  const sync_pb::PasswordSpecifics& password_specifics =
      specifics.GetExtension(sync_pb::password);
  if (!password_specifics.has_encrypted())
    return NULL;
  const sync_pb::EncryptedData& encrypted = password_specifics.encrypted();
  scoped_ptr<sync_pb::PasswordSpecificsData> data(
      new sync_pb::PasswordSpecificsData);
  if (!crypto->Decrypt(encrypted, data.get()))
    return NULL;
  return data.release();
}

bool BaseNode::DecryptIfNecessary(Entry* entry) {
  if (GetIsFolder()) return true;  // Ignore the top-level datatype folder.
  const sync_pb::EntitySpecifics& specifics =
      entry->Get(syncable::SPECIFICS);
  if (specifics.HasExtension(sync_pb::password)) {
    // Passwords have their own legacy encryption structure.
    scoped_ptr<sync_pb::PasswordSpecificsData> data(DecryptPasswordSpecifics(
        specifics, GetTransaction()->GetCryptographer()));
    if (!data.get())
      return false;
    password_data_.swap(data);
    return true;
  }

  // We assume any node with the encrypted field set has encrypted data.
  if (!specifics.has_encrypted())
    return true;

  const sync_pb::EncryptedData& encrypted =
      specifics.encrypted();
  std::string plaintext_data = GetTransaction()->GetCryptographer()->
      DecryptToString(encrypted);
  if (plaintext_data.length() == 0)
    return false;
  if (!unencrypted_data_.ParseFromString(plaintext_data)) {
    LOG(ERROR) << "Failed to decrypt encrypted node of type " <<
      syncable::ModelTypeToString(entry->GetModelType()) << ".";
    return false;
  }
  return true;
}

const sync_pb::EntitySpecifics& BaseNode::GetUnencryptedSpecifics(
    const syncable::Entry* entry) const {
  const sync_pb::EntitySpecifics& specifics = entry->Get(SPECIFICS);
  if (specifics.has_encrypted()) {
    DCHECK(syncable::GetModelTypeFromSpecifics(unencrypted_data_) !=
           syncable::UNSPECIFIED);
    return unencrypted_data_;
  } else {
    DCHECK(syncable::GetModelTypeFromSpecifics(unencrypted_data_) ==
           syncable::UNSPECIFIED);
    return specifics;
  }
}

int64 BaseNode::GetParentId() const {
  return IdToMetahandle(GetTransaction()->GetWrappedTrans(),
                        GetEntry()->Get(syncable::PARENT_ID));
}

int64 BaseNode::GetId() const {
  return GetEntry()->Get(syncable::META_HANDLE);
}

int64 BaseNode::GetModificationTime() const {
  return GetEntry()->Get(syncable::MTIME);
}

bool BaseNode::GetIsFolder() const {
  return GetEntry()->Get(syncable::IS_DIR);
}

std::wstring BaseNode::GetTitle() const {
  std::wstring result;
  ServerNameToSyncAPIName(GetEntry()->Get(syncable::NON_UNIQUE_NAME), &result);
  return result;
}

GURL BaseNode::GetURL() const {
  return GURL(GetBookmarkSpecifics().url());
}

int64 BaseNode::GetPredecessorId() const {
  syncable::Id id_string = GetEntry()->Get(syncable::PREV_ID);
  if (id_string.IsRoot())
    return kInvalidId;
  return IdToMetahandle(GetTransaction()->GetWrappedTrans(), id_string);
}

int64 BaseNode::GetSuccessorId() const {
  syncable::Id id_string = GetEntry()->Get(syncable::NEXT_ID);
  if (id_string.IsRoot())
    return kInvalidId;
  return IdToMetahandle(GetTransaction()->GetWrappedTrans(), id_string);
}

int64 BaseNode::GetFirstChildId() const {
  syncable::Directory* dir = GetTransaction()->GetLookup();
  syncable::BaseTransaction* trans = GetTransaction()->GetWrappedTrans();
  syncable::Id id_string =
      dir->GetFirstChildId(trans, GetEntry()->Get(syncable::ID));
  if (id_string.IsRoot())
    return kInvalidId;
  return IdToMetahandle(GetTransaction()->GetWrappedTrans(), id_string);
}

DictionaryValue* BaseNode::ToValue() const {
  DictionaryValue* node_info = new DictionaryValue();
  node_info->SetString("id", base::Int64ToString(GetId()));
  // TODO(akalin): Return time in a better format.
  node_info->SetString("modificationTime",
                       base::Int64ToString(GetModificationTime()));
  node_info->SetString("parentId", base::Int64ToString(GetParentId()));
  node_info->SetBoolean("isFolder", GetIsFolder());
  // TODO(akalin): Add a std::string accessor for the title.
  node_info->SetString("title", WideToUTF8(GetTitle()));
  node_info->Set("type", ModelTypeToValue(GetModelType()));
  // Specifics are already in the Entry value, so no need to duplicate
  // it here.
  node_info->SetString("externalId",
                       base::Int64ToString(GetExternalId()));
  node_info->SetString("predecessorId",
                       base::Int64ToString(GetPredecessorId()));
  node_info->SetString("successorId",
                       base::Int64ToString(GetSuccessorId()));
  node_info->SetString("firstChildId",
                       base::Int64ToString(GetFirstChildId()));
  node_info->Set("entry", GetEntry()->ToValue());
  return node_info;
}

void BaseNode::GetFaviconBytes(std::vector<unsigned char>* output) const {
  if (!output)
    return;
  const std::string& favicon = GetBookmarkSpecifics().favicon();
  output->assign(reinterpret_cast<const unsigned char*>(favicon.data()),
      reinterpret_cast<const unsigned char*>(favicon.data() +
                                             favicon.length()));
}

int64 BaseNode::GetExternalId() const {
  return GetEntry()->Get(syncable::LOCAL_EXTERNAL_ID);
}

const sync_pb::AppSpecifics& BaseNode::GetAppSpecifics() const {
  DCHECK_EQ(syncable::APPS, GetModelType());
  const sync_pb::EntitySpecifics& unencrypted =
      GetUnencryptedSpecifics(GetEntry());
  return unencrypted.GetExtension(sync_pb::app);
}

const sync_pb::AutofillSpecifics& BaseNode::GetAutofillSpecifics() const {
  DCHECK_EQ(syncable::AUTOFILL, GetModelType());
  const sync_pb::EntitySpecifics& unencrypted =
      GetUnencryptedSpecifics(GetEntry());
  return unencrypted.GetExtension(sync_pb::autofill);
}

const AutofillProfileSpecifics& BaseNode::GetAutofillProfileSpecifics() const {
  DCHECK_EQ(GetModelType(), syncable::AUTOFILL_PROFILE);
  const sync_pb::EntitySpecifics& unencrypted =
      GetUnencryptedSpecifics(GetEntry());
  return unencrypted.GetExtension(sync_pb::autofill_profile);
}

const sync_pb::BookmarkSpecifics& BaseNode::GetBookmarkSpecifics() const {
  DCHECK_EQ(syncable::BOOKMARKS, GetModelType());
  const sync_pb::EntitySpecifics& unencrypted =
      GetUnencryptedSpecifics(GetEntry());
  return unencrypted.GetExtension(sync_pb::bookmark);
}

const sync_pb::NigoriSpecifics& BaseNode::GetNigoriSpecifics() const {
  DCHECK_EQ(syncable::NIGORI, GetModelType());
  const sync_pb::EntitySpecifics& unencrypted =
      GetUnencryptedSpecifics(GetEntry());
  return unencrypted.GetExtension(sync_pb::nigori);
}

const sync_pb::PasswordSpecificsData& BaseNode::GetPasswordSpecifics() const {
  DCHECK_EQ(syncable::PASSWORDS, GetModelType());
  DCHECK(password_data_.get());
  return *password_data_;
}

const sync_pb::PreferenceSpecifics& BaseNode::GetPreferenceSpecifics() const {
  DCHECK_EQ(syncable::PREFERENCES, GetModelType());
  const sync_pb::EntitySpecifics& unencrypted =
      GetUnencryptedSpecifics(GetEntry());
  return unencrypted.GetExtension(sync_pb::preference);
}

const sync_pb::ThemeSpecifics& BaseNode::GetThemeSpecifics() const {
  DCHECK_EQ(syncable::THEMES, GetModelType());
  const sync_pb::EntitySpecifics& unencrypted =
      GetUnencryptedSpecifics(GetEntry());
  return unencrypted.GetExtension(sync_pb::theme);
}

const sync_pb::TypedUrlSpecifics& BaseNode::GetTypedUrlSpecifics() const {
  DCHECK_EQ(syncable::TYPED_URLS, GetModelType());
  const sync_pb::EntitySpecifics& unencrypted =
      GetUnencryptedSpecifics(GetEntry());
  return unencrypted.GetExtension(sync_pb::typed_url);
}

const sync_pb::ExtensionSpecifics& BaseNode::GetExtensionSpecifics() const {
  DCHECK_EQ(syncable::EXTENSIONS, GetModelType());
  const sync_pb::EntitySpecifics& unencrypted =
      GetUnencryptedSpecifics(GetEntry());
  return unencrypted.GetExtension(sync_pb::extension);
}

const sync_pb::SessionSpecifics& BaseNode::GetSessionSpecifics() const {
  DCHECK_EQ(syncable::SESSIONS, GetModelType());
  const sync_pb::EntitySpecifics& unencrypted =
      GetUnencryptedSpecifics(GetEntry());
  return unencrypted.GetExtension(sync_pb::session);
}

syncable::ModelType BaseNode::GetModelType() const {
  return GetEntry()->GetModelType();
}

////////////////////////////////////
// WriteNode member definitions
void WriteNode::EncryptIfNecessary(sync_pb::EntitySpecifics* unencrypted) {
  syncable::ModelType type = syncable::GetModelTypeFromSpecifics(*unencrypted);
  DCHECK_NE(type, syncable::UNSPECIFIED);
  DCHECK_NE(type, syncable::PASSWORDS);  // Passwords use their own encryption.
  DCHECK_NE(type, syncable::NIGORI);     // Nigori is encrypted separately.

  syncable::ModelTypeSet encrypted_types =
      GetEncryptedDataTypes(GetTransaction()->GetWrappedTrans());
  if (encrypted_types.count(type) == 0) {
    // This datatype does not require encryption.
    return;
  }

  if (unencrypted->has_encrypted()) {
    // This specifics is already encrypted, our work is done.
    LOG(WARNING) << "Attempted to encrypt an already encrypted entity"
      << " specifics of type " << syncable::ModelTypeToString(type)
      << ". Dropping.";
    return;
  }
  sync_pb::EntitySpecifics encrypted;
  syncable::AddDefaultExtensionValue(type, &encrypted);
  VLOG(2) << "Encrypted specifics of type " << syncable::ModelTypeToString(type)
          << " with content: " << unencrypted->SerializeAsString() << "\n";
  if (!GetTransaction()->GetCryptographer()->Encrypt(
      *unencrypted,
      encrypted.mutable_encrypted())) {
    LOG(ERROR) << "Could not encrypt data for node of type " <<
      syncable::ModelTypeToString(type);
    NOTREACHED();
  }
  unencrypted->CopyFrom(encrypted);
}

void WriteNode::SetIsFolder(bool folder) {
  if (entry_->Get(syncable::IS_DIR) == folder)
    return;  // Skip redundant changes.

  entry_->Put(syncable::IS_DIR, folder);
  MarkForSyncing();
}

void WriteNode::SetTitle(const std::wstring& title) {
  std::string server_legal_name;
  SyncAPINameToServerName(title, &server_legal_name);

  string old_name = entry_->Get(syncable::NON_UNIQUE_NAME);

  if (server_legal_name == old_name)
    return;  // Skip redundant changes.

  entry_->Put(syncable::NON_UNIQUE_NAME, server_legal_name);
  MarkForSyncing();
}

void WriteNode::SetURL(const GURL& url) {
  sync_pb::BookmarkSpecifics new_value = GetBookmarkSpecifics();
  new_value.set_url(url.spec());
  SetBookmarkSpecifics(new_value);
}

void WriteNode::SetAppSpecifics(
    const sync_pb::AppSpecifics& new_value) {
  DCHECK_EQ(syncable::APPS, GetModelType());
  PutAppSpecificsAndMarkForSyncing(new_value);
}

void WriteNode::SetAutofillSpecifics(
    const sync_pb::AutofillSpecifics& new_value) {
  DCHECK_EQ(syncable::AUTOFILL, GetModelType());
  PutAutofillSpecificsAndMarkForSyncing(new_value);
}

void WriteNode::PutAutofillSpecificsAndMarkForSyncing(
    const sync_pb::AutofillSpecifics& new_value) {
  sync_pb::EntitySpecifics entity_specifics;
  entity_specifics.MutableExtension(sync_pb::autofill)->CopyFrom(new_value);
  EncryptIfNecessary(&entity_specifics);
  PutSpecificsAndMarkForSyncing(entity_specifics);
}

void WriteNode::SetAutofillProfileSpecifics(
    const sync_pb::AutofillProfileSpecifics& new_value) {
  DCHECK_EQ(GetModelType(), syncable::AUTOFILL_PROFILE);
  PutAutofillProfileSpecificsAndMarkForSyncing(new_value);
}

void WriteNode::PutAutofillProfileSpecificsAndMarkForSyncing(
    const sync_pb::AutofillProfileSpecifics& new_value) {
  sync_pb::EntitySpecifics entity_specifics;
  entity_specifics.MutableExtension(sync_pb::autofill_profile)->CopyFrom(
      new_value);
  EncryptIfNecessary(&entity_specifics);
  PutSpecificsAndMarkForSyncing(entity_specifics);
}

void WriteNode::SetBookmarkSpecifics(
    const sync_pb::BookmarkSpecifics& new_value) {
  DCHECK_EQ(syncable::BOOKMARKS, GetModelType());
  PutBookmarkSpecificsAndMarkForSyncing(new_value);
}

void WriteNode::PutBookmarkSpecificsAndMarkForSyncing(
    const sync_pb::BookmarkSpecifics& new_value) {
  sync_pb::EntitySpecifics entity_specifics;
  entity_specifics.MutableExtension(sync_pb::bookmark)->CopyFrom(new_value);
  EncryptIfNecessary(&entity_specifics);
  PutSpecificsAndMarkForSyncing(entity_specifics);
}

void WriteNode::SetNigoriSpecifics(
    const sync_pb::NigoriSpecifics& new_value) {
  DCHECK_EQ(syncable::NIGORI, GetModelType());
  PutNigoriSpecificsAndMarkForSyncing(new_value);
}

void WriteNode::PutNigoriSpecificsAndMarkForSyncing(
    const sync_pb::NigoriSpecifics& new_value) {
  sync_pb::EntitySpecifics entity_specifics;
  entity_specifics.MutableExtension(sync_pb::nigori)->CopyFrom(new_value);
  PutSpecificsAndMarkForSyncing(entity_specifics);
}

void WriteNode::SetPasswordSpecifics(
    const sync_pb::PasswordSpecificsData& data) {
  DCHECK_EQ(syncable::PASSWORDS, GetModelType());

  Cryptographer* cryptographer = GetTransaction()->GetCryptographer();

  // Idempotency check to prevent unnecessary syncing: if the plaintexts match
  // and the old ciphertext is encrypted with the most current key, there's
  // nothing to do here.  Because each encryption is seeded with a different
  // random value, checking for equivalence post-encryption doesn't suffice.
  const sync_pb::EncryptedData& old_ciphertext =
      GetEntry()->Get(SPECIFICS).GetExtension(sync_pb::password).encrypted();
  scoped_ptr<sync_pb::PasswordSpecificsData> old_plaintext(
      DecryptPasswordSpecifics(GetEntry()->Get(SPECIFICS), cryptographer));
  if (old_plaintext.get() &&
      old_plaintext->SerializeAsString() == data.SerializeAsString() &&
      cryptographer->CanDecryptUsingDefaultKey(old_ciphertext)) {
    return;
  }

  sync_pb::PasswordSpecifics new_value;
  if (!cryptographer->Encrypt(data, new_value.mutable_encrypted())) {
    NOTREACHED();
  }
  PutPasswordSpecificsAndMarkForSyncing(new_value);
}

void WriteNode::SetPreferenceSpecifics(
    const sync_pb::PreferenceSpecifics& new_value) {
  DCHECK_EQ(syncable::PREFERENCES, GetModelType());
  PutPreferenceSpecificsAndMarkForSyncing(new_value);
}

void WriteNode::SetThemeSpecifics(
    const sync_pb::ThemeSpecifics& new_value) {
  DCHECK_EQ(syncable::THEMES, GetModelType());
  PutThemeSpecificsAndMarkForSyncing(new_value);
}

void WriteNode::SetSessionSpecifics(
    const sync_pb::SessionSpecifics& new_value) {
  DCHECK_EQ(syncable::SESSIONS, GetModelType());
  PutSessionSpecificsAndMarkForSyncing(new_value);
}

void WriteNode::ResetFromSpecifics() {
  sync_pb::EntitySpecifics new_data;
  new_data.CopyFrom(GetUnencryptedSpecifics(GetEntry()));
  EncryptIfNecessary(&new_data);
  PutSpecificsAndMarkForSyncing(new_data);
}

void WriteNode::PutPasswordSpecificsAndMarkForSyncing(
    const sync_pb::PasswordSpecifics& new_value) {
  sync_pb::EntitySpecifics entity_specifics;
  entity_specifics.MutableExtension(sync_pb::password)->CopyFrom(new_value);
  PutSpecificsAndMarkForSyncing(entity_specifics);
}

void WriteNode::PutPreferenceSpecificsAndMarkForSyncing(
    const sync_pb::PreferenceSpecifics& new_value) {
  sync_pb::EntitySpecifics entity_specifics;
  entity_specifics.MutableExtension(sync_pb::preference)->CopyFrom(new_value);
  EncryptIfNecessary(&entity_specifics);
  PutSpecificsAndMarkForSyncing(entity_specifics);
}

void WriteNode::SetTypedUrlSpecifics(
    const sync_pb::TypedUrlSpecifics& new_value) {
  DCHECK_EQ(syncable::TYPED_URLS, GetModelType());
  PutTypedUrlSpecificsAndMarkForSyncing(new_value);
}

void WriteNode::SetExtensionSpecifics(
    const sync_pb::ExtensionSpecifics& new_value) {
  DCHECK_EQ(syncable::EXTENSIONS, GetModelType());
  PutExtensionSpecificsAndMarkForSyncing(new_value);
}

void WriteNode::PutAppSpecificsAndMarkForSyncing(
    const sync_pb::AppSpecifics& new_value) {
  sync_pb::EntitySpecifics entity_specifics;
  entity_specifics.MutableExtension(sync_pb::app)->CopyFrom(new_value);
  EncryptIfNecessary(&entity_specifics);
  PutSpecificsAndMarkForSyncing(entity_specifics);
}

void WriteNode::PutThemeSpecificsAndMarkForSyncing(
    const sync_pb::ThemeSpecifics& new_value) {
  sync_pb::EntitySpecifics entity_specifics;
  entity_specifics.MutableExtension(sync_pb::theme)->CopyFrom(new_value);
  EncryptIfNecessary(&entity_specifics);
  PutSpecificsAndMarkForSyncing(entity_specifics);
}

void WriteNode::PutTypedUrlSpecificsAndMarkForSyncing(
    const sync_pb::TypedUrlSpecifics& new_value) {
  sync_pb::EntitySpecifics entity_specifics;
  entity_specifics.MutableExtension(sync_pb::typed_url)->CopyFrom(new_value);
  EncryptIfNecessary(&entity_specifics);
  PutSpecificsAndMarkForSyncing(entity_specifics);
}

void WriteNode::PutExtensionSpecificsAndMarkForSyncing(
    const sync_pb::ExtensionSpecifics& new_value) {
  sync_pb::EntitySpecifics entity_specifics;
  entity_specifics.MutableExtension(sync_pb::extension)->CopyFrom(new_value);
  EncryptIfNecessary(&entity_specifics);
  PutSpecificsAndMarkForSyncing(entity_specifics);
}

void WriteNode::PutSessionSpecificsAndMarkForSyncing(
    const sync_pb::SessionSpecifics& new_value) {
  sync_pb::EntitySpecifics entity_specifics;
  entity_specifics.MutableExtension(sync_pb::session)->CopyFrom(new_value);
  EncryptIfNecessary(&entity_specifics);
  PutSpecificsAndMarkForSyncing(entity_specifics);
}

void WriteNode::PutSpecificsAndMarkForSyncing(
    const sync_pb::EntitySpecifics& specifics) {
  // Skip redundant changes.
  if (specifics.SerializeAsString() ==
      entry_->Get(SPECIFICS).SerializeAsString()) {
    return;
  }
  entry_->Put(SPECIFICS, specifics);
  MarkForSyncing();
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
          DecryptIfNecessary(entry_));
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
          DecryptIfNecessary(entry_));
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
  PutSpecificsAndMarkForSyncing(specifics);
  DCHECK_EQ(model_type, GetModelType());
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
  PutPredecessor(predecessor);

  return true;
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
  PutPredecessor(NULL);

  return true;
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
  PutPredecessor(predecessor);

  return true;
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

void WriteNode::PutPredecessor(const BaseNode* predecessor) {
  syncable::Id predecessor_id = predecessor ?
      predecessor->GetEntry()->Get(syncable::ID) : syncable::Id();
  entry_->PutPredecessor(predecessor_id);
  // Mark this entry as unsynced, to wake up the syncer.
  MarkForSyncing();
}

void WriteNode::SetFaviconBytes(const vector<unsigned char>& bytes) {
  sync_pb::BookmarkSpecifics new_value = GetBookmarkSpecifics();
  new_value.set_favicon(bytes.empty() ? NULL : &bytes[0], bytes.size());
  SetBookmarkSpecifics(new_value);
}

void WriteNode::MarkForSyncing() {
  syncable::MarkForSyncing(entry_);
}

//////////////////////////////////////////////////////////////////////////
// ReadNode member definitions
ReadNode::ReadNode(const BaseTransaction* transaction)
    : entry_(NULL), transaction_(transaction) {
  DCHECK(transaction);
}

ReadNode::ReadNode() {
  entry_ = NULL;
  transaction_ = NULL;
}

ReadNode::~ReadNode() {
  delete entry_;
}

void ReadNode::InitByRootLookup() {
  DCHECK(!entry_) << "Init called twice";
  syncable::BaseTransaction* trans = transaction_->GetWrappedTrans();
  entry_ = new syncable::Entry(trans, syncable::GET_BY_ID, trans->root_id());
  if (!entry_->good())
    DCHECK(false) << "Could not lookup root node for reading.";
}

bool ReadNode::InitByIdLookup(int64 id) {
  DCHECK(!entry_) << "Init called twice";
  DCHECK_NE(id, kInvalidId);
  syncable::BaseTransaction* trans = transaction_->GetWrappedTrans();
  entry_ = new syncable::Entry(trans, syncable::GET_BY_HANDLE, id);
  if (!entry_->good())
    return false;
  if (entry_->Get(syncable::IS_DEL))
    return false;
  syncable::ModelType model_type = GetModelType();
  LOG_IF(WARNING, model_type == syncable::UNSPECIFIED ||
                  model_type == syncable::TOP_LEVEL_FOLDER)
      << "SyncAPI InitByIdLookup referencing unusual object.";
  return DecryptIfNecessary(entry_);
}

bool ReadNode::InitByClientTagLookup(syncable::ModelType model_type,
                                     const std::string& tag) {
  DCHECK(!entry_) << "Init called twice";
  if (tag.empty())
    return false;

  const std::string hash = GenerateSyncableHash(model_type, tag);

  entry_ = new syncable::Entry(transaction_->GetWrappedTrans(),
                               syncable::GET_BY_CLIENT_TAG, hash);
  return (entry_->good() && !entry_->Get(syncable::IS_DEL) &&
          DecryptIfNecessary(entry_));
}

const syncable::Entry* ReadNode::GetEntry() const {
  return entry_;
}

const BaseTransaction* ReadNode::GetTransaction() const {
  return transaction_;
}

bool ReadNode::InitByTagLookup(const std::string& tag) {
  DCHECK(!entry_) << "Init called twice";
  if (tag.empty())
    return false;
  syncable::BaseTransaction* trans = transaction_->GetWrappedTrans();
  entry_ = new syncable::Entry(trans, syncable::GET_BY_SERVER_TAG, tag);
  if (!entry_->good())
    return false;
  if (entry_->Get(syncable::IS_DEL))
    return false;
  syncable::ModelType model_type = GetModelType();
  LOG_IF(WARNING, model_type == syncable::UNSPECIFIED ||
                  model_type == syncable::TOP_LEVEL_FOLDER)
      << "SyncAPI InitByTagLookup referencing unusually typed object.";
  return DecryptIfNecessary(entry_);
}

//////////////////////////////////////////////////////////////////////////
// ReadTransaction member definitions
ReadTransaction::ReadTransaction(UserShare* share)
    : BaseTransaction(share),
      transaction_(NULL),
      close_transaction_(true) {
  transaction_ = new syncable::ReadTransaction(GetLookup(), __FILE__, __LINE__);
}

ReadTransaction::ReadTransaction(UserShare* share,
                                 syncable::BaseTransaction* trans)
    : BaseTransaction(share),
      transaction_(trans),
      close_transaction_(false) {}

ReadTransaction::~ReadTransaction() {
  if (close_transaction_) {
    delete transaction_;
  }
}

syncable::BaseTransaction* ReadTransaction::GetWrappedTrans() const {
  return transaction_;
}

//////////////////////////////////////////////////////////////////////////
// WriteTransaction member definitions
WriteTransaction::WriteTransaction(UserShare* share)
    : BaseTransaction(share),
      transaction_(NULL) {
  transaction_ = new syncable::WriteTransaction(GetLookup(), syncable::SYNCAPI,
                                                __FILE__, __LINE__);
}

WriteTransaction::~WriteTransaction() {
  delete transaction_;
}

syncable::BaseTransaction* WriteTransaction::GetWrappedTrans() const {
  return transaction_;
}

SyncManager::ChangeRecord::ChangeRecord()
    : id(kInvalidId), action(ACTION_ADD) {}

SyncManager::ChangeRecord::~ChangeRecord() {}

DictionaryValue* SyncManager::ChangeRecord::ToValue(
    const BaseTransaction* trans) const {
  DictionaryValue* value = new DictionaryValue();
  std::string action_str;
  switch (action) {
    case ACTION_ADD:
      action_str = "Add";
      break;
    case ACTION_DELETE:
      action_str = "Delete";
      break;
    case ACTION_UPDATE:
      action_str = "Update";
      break;
    default:
      NOTREACHED();
      action_str = "Unknown";
      break;
  }
  value->SetString("action", action_str);
  Value* node_value = NULL;
  if (action == ACTION_DELETE) {
    DictionaryValue* node_dict = new DictionaryValue();
    node_dict->SetString("id", base::Int64ToString(id));
    node_dict->Set("specifics",
                    browser_sync::EntitySpecificsToValue(specifics));
    if (extra.get()) {
      node_dict->Set("extra", extra->ToValue());
    }
    node_value = node_dict;
  } else {
    ReadNode node(trans);
    if (node.InitByIdLookup(id)) {
      node_value = node.ToValue();
    }
  }
  if (!node_value) {
    NOTREACHED();
    node_value = Value::CreateNullValue();
  }
  value->Set("node", node_value);
  return value;
}

bool BaseNode::ContainsString(const std::string& lowercase_query) const {
  DCHECK(GetEntry());
  // TODO(lipalani) - figure out what to do if the node is encrypted.
  const sync_pb::EntitySpecifics& specifics = GetEntry()->Get(SPECIFICS);
  std::string temp;
  // The protobuf serialized string contains the original strings. So
  // we will just serialize it and search it.
  specifics.SerializeToString(&temp);

  // Now convert to lower case.
  StringToLowerASCII(&temp);

  return temp.find(lowercase_query) != std::string::npos;
}

SyncManager::ExtraPasswordChangeRecordData::ExtraPasswordChangeRecordData() {}

SyncManager::ExtraPasswordChangeRecordData::ExtraPasswordChangeRecordData(
    const sync_pb::PasswordSpecificsData& data)
    : unencrypted_(data) {
}

SyncManager::ExtraPasswordChangeRecordData::~ExtraPasswordChangeRecordData() {}

DictionaryValue* SyncManager::ExtraPasswordChangeRecordData::ToValue() const {
  return browser_sync::PasswordSpecificsDataToValue(unencrypted_);
}

const sync_pb::PasswordSpecificsData&
    SyncManager::ExtraPasswordChangeRecordData::unencrypted() const {
  return unencrypted_;
}

namespace {

struct NotificationInfo {
  int total_count;
  std::string payload;

  NotificationInfo() : total_count(0) {}

  ~NotificationInfo() {}

  // Returned pointer owned by the caller.
  DictionaryValue* ToValue() const {
    DictionaryValue* value = new DictionaryValue();
    value->SetInteger("totalCount", total_count);
    value->SetString("payload", payload);
    return value;
  }
};

typedef std::map<syncable::ModelType, NotificationInfo> NotificationInfoMap;

// returned pointer is owned by the caller.
DictionaryValue* NotificationInfoToValue(
    const NotificationInfoMap& notification_info) {
  DictionaryValue* value = new DictionaryValue();

  for (NotificationInfoMap::const_iterator it = notification_info.begin();
      it != notification_info.end(); ++it) {
    const std::string& model_type_str =
        syncable::ModelTypeToString(it->first);
    value->Set(model_type_str, it->second.ToValue());
  }

  return value;
}

}  // namespace

//////////////////////////////////////////////////////////////////////////
// SyncManager's implementation: SyncManager::SyncInternal
class SyncManager::SyncInternal
    : public net::NetworkChangeNotifier::IPAddressObserver,
      public sync_notifier::SyncNotifierObserver,
      public browser_sync::JsBackend,
      public SyncEngineEventListener,
      public ServerConnectionEventListener,
      public syncable::DirectoryChangeListener {
  static const int kDefaultNudgeDelayMilliseconds;
  static const int kPreferencesNudgeDelayMilliseconds;
 public:
  explicit SyncInternal(SyncManager* sync_manager)
      : core_message_loop_(NULL),
        parent_router_(NULL),
        sync_manager_(sync_manager),
        registrar_(NULL),
        initialized_(false),
        ALLOW_THIS_IN_INITIALIZER_LIST(method_factory_(this)) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  }

  virtual ~SyncInternal() {
    CHECK(!core_message_loop_);
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  }

  bool Init(const FilePath& database_location,
            const std::string& sync_server_and_path,
            int port,
            bool use_ssl,
            HttpPostProviderFactory* post_factory,
            ModelSafeWorkerRegistrar* model_safe_worker_registrar,
            const char* user_agent,
            const SyncCredentials& credentials,
            sync_notifier::SyncNotifier* sync_notifier,
            const std::string& restored_key_for_bootstrapping,
            bool setup_for_test_mode);

  // Sign into sync with given credentials.
  // We do not verify the tokens given. After this call, the tokens are set
  // and the sync DB is open. True if successful, false if something
  // went wrong.
  bool SignIn(const SyncCredentials& credentials);

  // Update tokens that we're using in Sync. Email must stay the same.
  void UpdateCredentials(const SyncCredentials& credentials);

  // Called when the user disables or enables a sync type.
  void UpdateEnabledTypes();

  // Tell the sync engine to start the syncing process.
  void StartSyncing();

  // Whether or not the Nigori node is encrypted using an explicit passphrase.
  bool IsUsingExplicitPassphrase();

  // Set the datatypes we want to encrypt and encrypt any nodes as necessary.
  void EncryptDataTypes(const syncable::ModelTypeSet& encrypted_types);

  // Try to set the current passphrase to |passphrase|, and record whether
  // it is an explicit passphrase or implicitly using gaia in the Nigori
  // node.
  void SetPassphrase(const std::string& passphrase, bool is_explicit);

  // Call periodically from a database-safe thread to persist recent changes
  // to the syncapi model.
  void SaveChanges();

  // DirectoryChangeListener implementation.
  // This listener is called upon completion of a syncable transaction, and
  // builds the list of sync-engine initiated changes that will be forwarded to
  // the SyncManager's Observers.
  virtual void HandleTransactionCompleteChangeEvent(
      const ModelTypeBitSet& models_with_changes);
  virtual ModelTypeBitSet HandleTransactionEndingChangeEvent(
      syncable::BaseTransaction* trans);
  virtual void HandleCalculateChangesChangeEventFromSyncApi(
      const OriginalEntries& originals,
      const WriterTag& writer,
      syncable::BaseTransaction* trans);
  virtual void HandleCalculateChangesChangeEventFromSyncer(
      const OriginalEntries& originals,
      const WriterTag& writer,
      syncable::BaseTransaction* trans);

  // Listens for notifications from the ServerConnectionManager
  void HandleServerConnectionEvent(const ServerConnectionEvent& event);

  // Open the directory named with username_for_share
  bool OpenDirectory();

  // SyncNotifierObserver implementation.
  virtual void OnNotificationStateChange(
      bool notifications_enabled);

  virtual void OnIncomingNotification(
      const syncable::ModelTypePayloadMap& type_payloads);

  virtual void StoreState(const std::string& cookie);

  void AddObserver(SyncManager::Observer* observer);

  void RemoveObserver(SyncManager::Observer* observer);

  // Accessors for the private members.
  DirectoryManager* dir_manager() { return share_.dir_manager.get(); }
  SyncAPIServerConnectionManager* connection_manager() {
    return connection_manager_.get();
  }
  SyncerThread* syncer_thread() { return syncer_thread_.get(); }
  UserShare* GetUserShare() { return &share_; }

  // Return the currently active (validated) username for use with syncable
  // types.
  const std::string& username_for_share() const {
    return share_.name;
  }

  Status GetStatus();

  void RequestNudge(const tracked_objects::Location& nudge_location);

  void RequestNudgeWithDataTypes(const TimeDelta& delay,
      browser_sync::NudgeSource source, const ModelTypeBitSet& types,
      const tracked_objects::Location& nudge_location);

  // See SyncManager::Shutdown for information.
  void Shutdown();

  // Whether we're initialized to the point of being able to accept changes
  // (and hence allow transaction creation). See initialized_ for details.
  bool initialized() const {
    base::AutoLock lock(initialized_mutex_);
    return initialized_;
  }

  // If this is a deletion for a password, sets the legacy
  // ExtraPasswordChangeRecordData field of |buffer|. Otherwise sets
  // |buffer|'s specifics field to contain the unencrypted data.
  void SetExtraChangeRecordData(int64 id,
                                syncable::ModelType type,
                                ChangeReorderBuffer* buffer,
                                Cryptographer* cryptographer,
                                const syncable::EntryKernel& original,
                                bool existed_before,
                                bool exists_now);

  // Called only by our NetworkChangeNotifier.
  virtual void OnIPAddressChanged();

  bool InitialSyncEndedForAllEnabledTypes() {
    syncable::ScopedDirLookup lookup(dir_manager(), username_for_share());
    if (!lookup.good()) {
      DCHECK(false) << "ScopedDirLookup failed when checking initial sync";
      return false;
    }

    ModelSafeRoutingInfo enabled_types;
    registrar_->GetModelSafeRoutingInfo(&enabled_types);
    for (ModelSafeRoutingInfo::const_iterator i = enabled_types.begin();
        i != enabled_types.end(); ++i) {
      if (!lookup->initial_sync_ended_for_type(i->first))
        return false;
    }
    return true;
  }

  syncable::AutofillMigrationState GetAutofillMigrationState() {
    syncable::ScopedDirLookup lookup(dir_manager(), username_for_share());
    if (!lookup.good()) {
      DCHECK(false) << "ScopedDirLookup failed when checking initial sync";
      return syncable::NOT_MIGRATED;
    }

    return lookup->get_autofill_migration_state();
  }

  void SetAutofillMigrationState(syncable::AutofillMigrationState state) {
    syncable::ScopedDirLookup lookup(dir_manager(), username_for_share());
    if (!lookup.good()) {
      DCHECK(false) << "ScopedDirLookup failed when checking initial sync";
      return;
    }

    return lookup->set_autofill_migration_state(state);
  }

  void SetAutofillMigrationDebugInfo(
      syncable::AutofillMigrationDebugInfo::PropertyToSet property_to_set,
      const syncable::AutofillMigrationDebugInfo& info) {
    syncable::ScopedDirLookup lookup(dir_manager(), username_for_share());
    if (!lookup.good()) {
      DCHECK(false) << "ScopedDirLookup failed when checking initial sync";
      return;
    }

    return lookup->set_autofill_migration_state_debug_info(
        property_to_set, info);
  }

  syncable::AutofillMigrationDebugInfo
      GetAutofillMigrationDebugInfo() {
    syncable::ScopedDirLookup lookup(dir_manager(), username_for_share());
    if (!lookup.good()) {
      DCHECK(false) << "ScopedDirLookup failed when checking initial sync";
      syncable::AutofillMigrationDebugInfo null_value = {0};
      return null_value;
    }
    return lookup->get_autofill_migration_debug_info();
  }

  // SyncEngineEventListener implementation.
  virtual void OnSyncEngineEvent(const SyncEngineEvent& event);

  // ServerConnectionEventListener implementation.
  virtual void OnServerConnectionEvent(const ServerConnectionEvent2& event);

  // browser_sync::JsBackend implementation.
  virtual void SetParentJsEventRouter(browser_sync::JsEventRouter* router);
  virtual void RemoveParentJsEventRouter();
  virtual const browser_sync::JsEventRouter* GetParentJsEventRouter() const;
  virtual void ProcessMessage(const std::string& name,
                              const browser_sync::JsArgList& args,
                              const browser_sync::JsEventHandler* sender);

  ListValue* FindNodesContainingString(const std::string& query);

 private:
  // Helper to call OnAuthError when no authentication credentials are
  // available.
  void RaiseAuthNeededEvent();

  // Helper to set initialized_ to true and raise an event to clients to notify
  // that initialization is complete and it is safe to send us changes. If
  // already initialized, this is a no-op.
  void MarkAndNotifyInitializationComplete();

  // Sends notifications to peers.
  void SendNotification();

  // Determine if the parents or predecessors differ between the old and new
  // versions of an entry stored in |a| and |b|.  Note that a node's index may
  // change without its NEXT_ID changing if the node at NEXT_ID also moved (but
  // the relative order is unchanged).  To handle such cases, we rely on the
  // caller to treat a position update on any sibling as updating the positions
  // of all siblings.
  static bool VisiblePositionsDiffer(const syncable::EntryKernel& a,
                                     const syncable::Entry& b) {
    // If the datatype isn't one where the browser model cares about position,
    // don't bother notifying that data model of position-only changes.
    if (!b.ShouldMaintainPosition())
      return false;
    if (a.ref(syncable::NEXT_ID) != b.Get(syncable::NEXT_ID))
      return true;
    if (a.ref(syncable::PARENT_ID) != b.Get(syncable::PARENT_ID))
      return true;
    return false;
  }

  // Determine if any of the fields made visible to clients of the Sync API
  // differ between the versions of an entry stored in |a| and |b|. A return
  // value of false means that it should be OK to ignore this change.
  static bool VisiblePropertiesDiffer(const syncable::EntryKernel& a,
                                      const syncable::Entry& b,
                                      Cryptographer* cryptographer) {
    syncable::ModelType model_type = b.GetModelType();
    // Suppress updates to items that aren't tracked by any browser model.
    if (model_type == syncable::UNSPECIFIED ||
        model_type == syncable::TOP_LEVEL_FOLDER) {
      return false;
    }
    if (a.ref(syncable::NON_UNIQUE_NAME) != b.Get(syncable::NON_UNIQUE_NAME))
      return true;
    if (a.ref(syncable::IS_DIR) != b.Get(syncable::IS_DIR))
      return true;
    // Check if data has changed (account for encryption).
    std::string a_str, b_str;
    if (a.ref(SPECIFICS).has_encrypted()) {
      const sync_pb::EncryptedData& encrypted = a.ref(SPECIFICS).encrypted();
      a_str = cryptographer->DecryptToString(encrypted);
    } else {
      a_str = a.ref(SPECIFICS).SerializeAsString();
    }
    if (b.Get(SPECIFICS).has_encrypted()) {
      const sync_pb::EncryptedData& encrypted = b.Get(SPECIFICS).encrypted();
      b_str = cryptographer->DecryptToString(encrypted);
    } else {
      b_str = b.Get(SPECIFICS).SerializeAsString();
    }
    if (a_str != b_str) {
      return true;
    }
    if (VisiblePositionsDiffer(a, b))
      return true;
    return false;
  }

  bool ChangeBuffersAreEmpty() {
    for (int i = 0; i < syncable::MODEL_TYPE_COUNT; ++i) {
      if (!change_buffers_[i].IsEmpty())
        return false;
    }
    return true;
  }

  void CheckServerReachable() {
    if (connection_manager()) {
      connection_manager()->CheckServerReachable();
    } else {
      NOTREACHED() << "Should be valid connection manager!";
    }
  }

  void ReEncryptEverything(WriteTransaction* trans);

  // Initializes (bootstraps) the Cryptographer if NIGORI has finished
  // initial sync so that it can immediately start encrypting / decrypting.
  // If the restored key is incompatible with the current version of the NIGORI
  // node (which could happen if a restart occurred just after an update to
  // NIGORI was downloaded and the user must enter a new passphrase to decrypt)
  // then we will raise OnPassphraseRequired and set pending keys for
  // decryption.  Otherwise, the cryptographer is made ready (is_ready()).
  void BootstrapEncryption(const std::string& restored_key_for_bootstrapping);

  // Called for every notification. This updates the notification statistics
  // to be displayed in about:sync.
  void UpdateNotificationInfo(
      const syncable::ModelTypePayloadMap& type_payloads);

  // Helper for migration to new nigori proto to set
  // 'using_explicit_passphrase' in the NigoriSpecifics.
  // TODO(tim): Bug 62103.  Remove this after it has been pushed out to dev
  // channel users.
  void SetUsingExplicitPassphrasePrefForMigration(
      WriteTransaction* const trans);

  // Checks for server reachabilty and requests a nudge.
  void OnIPAddressChangedImpl();

  // Functions called by ProcessMessage().
  browser_sync::JsArgList ProcessGetNodeByIdMessage(
      const browser_sync::JsArgList& args);

  browser_sync::JsArgList ProcessFindNodesContainingString(
      const browser_sync::JsArgList& args);

  // We couple the DirectoryManager and username together in a UserShare member
  // so we can return a handle to share_ to clients of the API for use when
  // constructing any transaction type.
  UserShare share_;

  MessageLoop* core_message_loop_;

  ObserverList<SyncManager::Observer> observers_;

  browser_sync::JsEventRouter* parent_router_;

  // The ServerConnectionManager used to abstract communication between the
  // client (the Syncer) and the sync server.
  scoped_ptr<SyncAPIServerConnectionManager> connection_manager_;

  // The thread that runs the Syncer. Needs to be explicitly Start()ed.
  scoped_ptr<SyncerThread> syncer_thread_;

  // The SyncNotifier which notifies us when updates need to be downloaded.
  sync_notifier::SyncNotifier* sync_notifier_;

  // A multi-purpose status watch object that aggregates stats from various
  // sync components.
  AllStatus allstatus_;

  // Each element of this array is a store of change records produced by
  // HandleChangeEvent during the CALCULATE_CHANGES step.  The changes are
  // segregated by model type, and are stored here to be processed and
  // forwarded to the observer slightly later, at the TRANSACTION_ENDING
  // step by HandleTransactionEndingChangeEvent. The list is cleared in the
  // TRANSACTION_COMPLETE step by HandleTransactionCompleteChangeEvent.
  ChangeReorderBuffer change_buffers_[syncable::MODEL_TYPE_COUNT];

  // Event listener hookup for the ServerConnectionManager.
  scoped_ptr<EventListenerHookup> connection_manager_hookup_;

  // The sync dir_manager to which we belong.
  SyncManager* const sync_manager_;

  // The entity that provides us with information about which types to sync.
  // The instance is shared between the SyncManager and the Syncer.
  ModelSafeWorkerRegistrar* registrar_;

  // Set to true once Init has been called, and we know of an authenticated
  // valid) username either from a fresh authentication attempt (as in
  // first-use case) or from a previous attempt stored in our UserSettings
  // (as in the steady-state), and the syncable::Directory has been opened,
  // meaning we are ready to accept changes.  Protected by initialized_mutex_
  // as it can get read/set by both the SyncerThread and the AuthWatcherThread.
  bool initialized_;
  mutable base::Lock initialized_mutex_;

  // True if the SyncManager should be running in test mode (no syncer thread
  // actually communicating with the server).
  bool setup_for_test_mode_;

  ScopedRunnableMethodFactory<SyncManager::SyncInternal> method_factory_;

  // Map used to store the notification info to be displayed in about:sync page.
  // TODO(lipalani) - prefill the map with enabled data types.
  NotificationInfoMap notification_info_map_;
};
const int SyncManager::SyncInternal::kDefaultNudgeDelayMilliseconds = 200;
const int SyncManager::SyncInternal::kPreferencesNudgeDelayMilliseconds = 2000;

SyncManager::Observer::~Observer() {}

SyncManager::SyncManager() {
  data_ = new SyncInternal(this);
}

bool SyncManager::Init(const FilePath& database_location,
                       const char* sync_server_and_path,
                       int sync_server_port,
                       bool use_ssl,
                       HttpPostProviderFactory* post_factory,
                       ModelSafeWorkerRegistrar* registrar,
                       const char* user_agent,
                       const SyncCredentials& credentials,
                       sync_notifier::SyncNotifier* sync_notifier,
                       const std::string& restored_key_for_bootstrapping,
                       bool setup_for_test_mode) {
  DCHECK(post_factory);
  VLOG(1) << "SyncManager starting Init...";
  string server_string(sync_server_and_path);
  return data_->Init(database_location,
                     server_string,
                     sync_server_port,
                     use_ssl,
                     post_factory,
                     registrar,
                     user_agent,
                     credentials,
                     sync_notifier,
                     restored_key_for_bootstrapping,
                     setup_for_test_mode);
}

void SyncManager::UpdateCredentials(const SyncCredentials& credentials) {
  data_->UpdateCredentials(credentials);
}

void SyncManager::UpdateEnabledTypes() {
  data_->UpdateEnabledTypes();
}


bool SyncManager::InitialSyncEndedForAllEnabledTypes() {
  return data_->InitialSyncEndedForAllEnabledTypes();
}

void SyncManager::StartSyncing() {
  data_->StartSyncing();
}

syncable::AutofillMigrationState
    SyncManager::GetAutofillMigrationState() {
  return data_->GetAutofillMigrationState();
}

void SyncManager::SetAutofillMigrationState(
    syncable::AutofillMigrationState state) {
  return data_->SetAutofillMigrationState(state);
}

syncable::AutofillMigrationDebugInfo
    SyncManager::GetAutofillMigrationDebugInfo() {
  return data_->GetAutofillMigrationDebugInfo();
}

void SyncManager::SetAutofillMigrationDebugInfo(
    syncable::AutofillMigrationDebugInfo::PropertyToSet property_to_set,
    const syncable::AutofillMigrationDebugInfo& info) {
  return data_->SetAutofillMigrationDebugInfo(property_to_set, info);
}

void SyncManager::SetPassphrase(const std::string& passphrase,
     bool is_explicit) {
  data_->SetPassphrase(passphrase, is_explicit);
}

void SyncManager::EncryptDataTypes(
    const syncable::ModelTypeSet& encrypted_types) {
  data_->EncryptDataTypes(encrypted_types);
}

bool SyncManager::IsUsingExplicitPassphrase() {
  return data_ && data_->IsUsingExplicitPassphrase();
}

void SyncManager::RequestNudge(const tracked_objects::Location& location) {
  data_->RequestNudge(location);
}

void SyncManager::RequestClearServerData() {
  if (data_->syncer_thread())
    data_->syncer_thread()->ScheduleClearUserData();
}

void SyncManager::RequestConfig(const syncable::ModelTypeBitSet& types) {
  if (!data_->syncer_thread())
    return;
  StartConfigurationMode(NULL);
  data_->syncer_thread()->ScheduleConfig(types);
}

void SyncManager::StartConfigurationMode(ModeChangeCallback* callback) {
  if (!data_->syncer_thread())
    return;
  data_->syncer_thread()->Start(
      browser_sync::SyncerThread::CONFIGURATION_MODE, callback);
}

const std::string& SyncManager::GetAuthenticatedUsername() {
  DCHECK(data_);
  return data_->username_for_share();
}

bool SyncManager::SyncInternal::Init(
    const FilePath& database_location,
    const std::string& sync_server_and_path,
    int port,
    bool use_ssl,
    HttpPostProviderFactory* post_factory,
    ModelSafeWorkerRegistrar* model_safe_worker_registrar,
    const char* user_agent,
    const SyncCredentials& credentials,
    sync_notifier::SyncNotifier* sync_notifier,
    const std::string& restored_key_for_bootstrapping,
    bool setup_for_test_mode) {

  VLOG(1) << "Starting SyncInternal initialization.";

  core_message_loop_ = MessageLoop::current();
  DCHECK(core_message_loop_);
  registrar_ = model_safe_worker_registrar;
  setup_for_test_mode_ = setup_for_test_mode;

  sync_notifier_ = sync_notifier;
  sync_notifier_->AddObserver(this);

  share_.dir_manager.reset(new DirectoryManager(database_location));

  connection_manager_.reset(new SyncAPIServerConnectionManager(
      sync_server_and_path, port, use_ssl, user_agent, post_factory));

  net::NetworkChangeNotifier::AddIPAddressObserver(this);

  connection_manager()->AddListener(this);

  // TODO(akalin): CheckServerReachable() can block, which may cause jank if we
  // try to shut down sync.  Fix this.
  core_message_loop_->PostTask(FROM_HERE,
      method_factory_.NewRunnableMethod(&SyncInternal::CheckServerReachable));

  // Test mode does not use a syncer context or syncer thread.
  if (!setup_for_test_mode_) {
    // Build a SyncSessionContext and store the worker in it.
    VLOG(1) << "Sync is bringing up SyncSessionContext.";
    std::vector<SyncEngineEventListener*> listeners;
    listeners.push_back(&allstatus_);
    listeners.push_back(this);
    SyncSessionContext* context = new SyncSessionContext(
        connection_manager_.get(),
        dir_manager(),
        model_safe_worker_registrar,
        listeners);
    context->set_account_name(credentials.email);
    // The SyncerThread takes ownership of |context|.
    syncer_thread_.reset(new SyncerThread(context, new Syncer()));
  }

  bool signed_in = SignIn(credentials);

  if (signed_in && syncer_thread()) {
    syncer_thread()->Start(
        browser_sync::SyncerThread::CONFIGURATION_MODE, NULL);
  }

  // Do this once the directory is opened.
  BootstrapEncryption(restored_key_for_bootstrapping);
  MarkAndNotifyInitializationComplete();
  return signed_in;
}

void SyncManager::SyncInternal::BootstrapEncryption(
    const std::string& restored_key_for_bootstrapping) {
  syncable::ScopedDirLookup lookup(dir_manager(), username_for_share());
  if (!lookup.good()) {
    NOTREACHED();
    return;
  }

  if (!lookup->initial_sync_ended_for_type(syncable::NIGORI))
    return;

  sync_pb::NigoriSpecifics nigori;
  {
    // Cryptographer should only be accessed while holding a transaction.
    ReadTransaction trans(GetUserShare());
    Cryptographer* cryptographer = trans.GetCryptographer();
    cryptographer->Bootstrap(restored_key_for_bootstrapping);

    ReadNode node(&trans);
    if (!node.InitByTagLookup(kNigoriTag)) {
      NOTREACHED();
      return;
    }

    nigori.CopyFrom(node.GetNigoriSpecifics());
    if (!nigori.encrypted().blob().empty()) {
      if (cryptographer->CanDecrypt(nigori.encrypted())) {
        cryptographer->SetKeys(nigori.encrypted());
      } else {
        cryptographer->SetPendingKeys(nigori.encrypted());
        FOR_EACH_OBSERVER(SyncManager::Observer, observers_,
                          OnPassphraseRequired(true));
      }
    }
  }

  // Refresh list of encrypted datatypes.
  syncable::ModelTypeSet encrypted_types =
      syncable::GetEncryptedDataTypesFromNigori(nigori);

  // Ensure any datatypes that need encryption are encrypted.
  EncryptDataTypes(encrypted_types);
}

void SyncManager::SyncInternal::StartSyncing() {
  // Start the syncer thread. This won't actually
  // result in any syncing until at least the
  // DirectoryManager broadcasts the OPENED event,
  // and a valid server connection is detected.
  if (syncer_thread())  // NULL during certain unittests.
    syncer_thread()->Start(SyncerThread::NORMAL_MODE, NULL);
}

void SyncManager::SyncInternal::MarkAndNotifyInitializationComplete() {
  // There is only one real time we need this mutex.  If we get an auth
  // success, and before the initial sync ends we get an auth failure.  In this
  // case we'll be listening to both the AuthWatcher and Syncer, and it's a race
  // between their respective threads to call MarkAndNotify.  We need to make
  // sure the observer is notified once and only once.
  {
    base::AutoLock lock(initialized_mutex_);
    if (initialized_)
      return;
    initialized_ = true;
  }

  // Notify that initialization is complete.
  FOR_EACH_OBSERVER(SyncManager::Observer, observers_,
                    OnInitializationComplete());
}

void SyncManager::SyncInternal::SendNotification() {
  DCHECK_EQ(MessageLoop::current(), core_message_loop_);
  if (!sync_notifier_) {
    VLOG(1) << "Not sending notification: sync_notifier_ is NULL";
    return;
  }
  allstatus_.IncrementNotificationsSent();
  sync_notifier_->SendNotification();
}

bool SyncManager::SyncInternal::OpenDirectory() {
  DCHECK(!initialized()) << "Should only happen once";

  bool share_opened = dir_manager()->Open(username_for_share());
  DCHECK(share_opened);
  if (!share_opened) {
    FOR_EACH_OBSERVER(SyncManager::Observer, observers_,
                      OnStopSyncingPermanently());

    LOG(ERROR) << "Could not open share for:" << username_for_share();
    return false;
  }

  // Database has to be initialized for the guid to be available.
  syncable::ScopedDirLookup lookup(dir_manager(), username_for_share());
  if (!lookup.good()) {
    NOTREACHED();
    return false;
  }

  connection_manager()->set_client_id(lookup->cache_guid());

  lookup->SetChangeListener(this);
  return true;
}

bool SyncManager::SyncInternal::SignIn(const SyncCredentials& credentials) {
  DCHECK_EQ(MessageLoop::current(), core_message_loop_);
  DCHECK(share_.name.empty());
  share_.name = credentials.email;

  VLOG(1) << "Signing in user: " << username_for_share();
  if (!OpenDirectory())
    return false;

  // Retrieve and set the sync notifier state. This should be done
  // only after OpenDirectory is called.
  syncable::ScopedDirLookup lookup(dir_manager(), username_for_share());
  std::string state;
  if (lookup.good()) {
    state = lookup->GetAndClearNotificationState();
  } else {
    LOG(ERROR) << "Could not read notification state";
  }
  if (VLOG_IS_ON(1)) {
    std::string encoded_state;
    base::Base64Encode(state, &encoded_state);
    VLOG(1) << "Read notification state: " << encoded_state;
  }
  sync_notifier_->SetState(state);

  UpdateCredentials(credentials);
  UpdateEnabledTypes();
  return true;
}

void SyncManager::SyncInternal::UpdateCredentials(
    const SyncCredentials& credentials) {
  DCHECK_EQ(MessageLoop::current(), core_message_loop_);
  DCHECK_EQ(credentials.email, share_.name);
  DCHECK(!credentials.email.empty());
  DCHECK(!credentials.sync_token.empty());
  connection_manager()->set_auth_token(credentials.sync_token);
  sync_notifier_->UpdateCredentials(
      credentials.email, credentials.sync_token);
  if (!setup_for_test_mode_) {
    CheckServerReachable();
  }
}

void SyncManager::SyncInternal::UpdateEnabledTypes() {
  DCHECK_EQ(MessageLoop::current(), core_message_loop_);
  ModelSafeRoutingInfo routes;
  registrar_->GetModelSafeRoutingInfo(&routes);
  syncable::ModelTypeSet enabled_types;
  for (ModelSafeRoutingInfo::const_iterator it = routes.begin();
       it != routes.end(); ++it) {
    enabled_types.insert(it->first);
  }
  sync_notifier_->UpdateEnabledTypes(enabled_types);
}

void SyncManager::SyncInternal::RaiseAuthNeededEvent() {
  FOR_EACH_OBSERVER(
      SyncManager::Observer, observers_,
      OnAuthError(AuthError(AuthError::INVALID_GAIA_CREDENTIALS)));
}

void SyncManager::SyncInternal::SetUsingExplicitPassphrasePrefForMigration(
    WriteTransaction* const trans) {
  WriteNode node(trans);
  if (!node.InitByTagLookup(kNigoriTag)) {
    // TODO(albertb): Plumb an UnrecoverableError all the way back to the PSS.
    NOTREACHED();
    return;
  }
  sync_pb::NigoriSpecifics specifics(node.GetNigoriSpecifics());
  specifics.set_using_explicit_passphrase(true);
  node.SetNigoriSpecifics(specifics);
}

void SyncManager::SyncInternal::SetPassphrase(
    const std::string& passphrase, bool is_explicit) {
  // All accesses to the cryptographer are protected by a transaction.
  WriteTransaction trans(GetUserShare());
  Cryptographer* cryptographer = trans.GetCryptographer();
  KeyParams params = {"localhost", "dummy", passphrase};

  if (cryptographer->has_pending_keys()) {
    if (!cryptographer->DecryptPendingKeys(params)) {
      VLOG(1) << "Passphrase failed to decrypt pending keys.";
      FOR_EACH_OBSERVER(SyncManager::Observer, observers_,
                        OnPassphraseFailed());
      return;
    }

    // TODO(tim): If this is the first time the user has entered a passphrase
    // since the protocol changed to store passphrase preferences in the cloud,
    // make sure we update this preference. See bug 62103.
    if (is_explicit)
      SetUsingExplicitPassphrasePrefForMigration(&trans);

    // Nudge the syncer so that encrypted datatype updates that were waiting for
    // this passphrase get applied as soon as possible.
    RequestNudge(FROM_HERE);
  } else {
    VLOG(1) << "No pending keys, adding provided passphrase.";
    WriteNode node(&trans);
    if (!node.InitByTagLookup(kNigoriTag)) {
      // TODO(albertb): Plumb an UnrecoverableError all the way back to the PSS.
      NOTREACHED();
      return;
    }

    // Prevent an implicit SetPassphrase request from changing an explicitly
    // set passphrase.
    if (!is_explicit && node.GetNigoriSpecifics().using_explicit_passphrase())
      return;

    cryptographer->AddKey(params);

    // TODO(tim): Bug 58231. It would be nice if SetPassphrase didn't require
    // messing with the Nigori node, because we can't call SetPassphrase until
    // download conditions are met vs Cryptographer init.  It seems like it's
    // safe to defer this work.
    sync_pb::NigoriSpecifics specifics(node.GetNigoriSpecifics());
    specifics.clear_encrypted();
    cryptographer->GetKeys(specifics.mutable_encrypted());
    specifics.set_using_explicit_passphrase(is_explicit);
    node.SetNigoriSpecifics(specifics);
    ReEncryptEverything(&trans);
  }

  std::string bootstrap_token;
  cryptographer->GetBootstrapToken(&bootstrap_token);
  FOR_EACH_OBSERVER(SyncManager::Observer, observers_,
                    OnPassphraseAccepted(bootstrap_token));
}

bool SyncManager::SyncInternal::IsUsingExplicitPassphrase() {
  ReadTransaction trans(&share_);
  ReadNode node(&trans);
  if (!node.InitByTagLookup(kNigoriTag)) {
    // TODO(albertb): Plumb an UnrecoverableError all the way back to the PSS.
    NOTREACHED();
    return false;
  }

  return node.GetNigoriSpecifics().using_explicit_passphrase();
}

void SyncManager::SyncInternal::EncryptDataTypes(
    const syncable::ModelTypeSet& encrypted_types) {
  VLOG(1) << "Attempting to encrypt datatypes "
          << syncable::ModelTypeSetToString(encrypted_types);

  WriteTransaction trans(GetUserShare());
  WriteNode node(&trans);
  if (!node.InitByTagLookup(kNigoriTag)) {
    LOG(ERROR) << "Unable to set encrypted datatypes because Nigori node not "
               << "found.";
    NOTREACHED();
    return;
  }

  // Update the Nigori node set of encrypted datatypes so other machines notice.
  // Note, we merge the current encrypted types with those requested. Once a
  // datatypes is marked as needing encryption, it is never unmarked.
  sync_pb::NigoriSpecifics nigori;
  nigori.CopyFrom(node.GetNigoriSpecifics());
  syncable::ModelTypeSet current_encrypted_types =
      syncable::GetEncryptedDataTypesFromNigori(nigori);
  syncable::ModelTypeSet newly_encrypted_types;
  std::set_union(current_encrypted_types.begin(), current_encrypted_types.end(),
                 encrypted_types.begin(), encrypted_types.end(),
                 std::inserter(newly_encrypted_types,
                               newly_encrypted_types.begin()));
  syncable::FillNigoriEncryptedTypes(newly_encrypted_types, &nigori);
  node.SetNigoriSpecifics(nigori);

  // TODO(zea): only reencrypt this datatype? ReEncrypting everything is a
  // safer approach, and should not impact anything that is already encrypted
  // (redundant changes are ignored).
  ReEncryptEverything(&trans);
  return;
}

namespace {

void FindChildNodesContainingString(const std::string& lowercase_query,
    const ReadNode& parent_node,
    sync_api::ReadTransaction* trans,
    ListValue* result) {
  int64 child_id = parent_node.GetFirstChildId();
  while (child_id != kInvalidId) {
    ReadNode node(trans);
    if (node.InitByIdLookup(child_id)) {
      if (node.ContainsString(lowercase_query)) {
        result->Append(new StringValue(base::Int64ToString(child_id)));
      }
      FindChildNodesContainingString(lowercase_query, node, trans, result);
      child_id = node.GetSuccessorId();
    } else {
      LOG(WARNING) << "Lookup of node failed. Id: " << child_id;
      return;
    }
  }
}
}  // namespace

// Returned pointer owned by the caller.
ListValue* SyncManager::SyncInternal::FindNodesContainingString(
    const std::string& query) {
  // Convert the query string to lower case to perform case insensitive
  // searches.
  std::string lowercase_query = query;
  StringToLowerASCII(&lowercase_query);
  ReadTransaction trans(GetUserShare());
  ReadNode root(&trans);
  root.InitByRootLookup();

  ListValue* result = new ListValue();

  base::Time start_time = base::Time::Now();
  FindChildNodesContainingString(lowercase_query, root, &trans, result);
  base::Time end_time = base::Time::Now();

  base::TimeDelta delta = end_time - start_time;
  VLOG(1) << "Time taken in milliseconds to search " << delta.InMilliseconds();

  return result;
}

void SyncManager::SyncInternal::ReEncryptEverything(WriteTransaction* trans) {
  syncable::ModelTypeSet encrypted_types =
      GetEncryptedDataTypes(trans->GetWrappedTrans());
  ModelSafeRoutingInfo routes;
  registrar_->GetModelSafeRoutingInfo(&routes);
  std::string tag;
  for (syncable::ModelTypeSet::iterator iter = encrypted_types.begin();
       iter != encrypted_types.end(); ++iter) {
    if (*iter == syncable::PASSWORDS || routes.count(*iter) == 0)
      continue;
    ReadNode type_root(trans);
    tag = syncable::ModelTypeToRootTag(*iter);
    if (!type_root.InitByTagLookup(tag)) {
      NOTREACHED();
      return;
    }

    // Iterate through all children of this datatype.
    std::queue<int64> to_visit;
    int64 child_id = type_root.GetFirstChildId();
    to_visit.push(child_id);
    while (!to_visit.empty()) {
      child_id = to_visit.front();
      to_visit.pop();
      if (child_id == kInvalidId)
        continue;

      WriteNode child(trans);
      if (!child.InitByIdLookup(child_id)) {
        NOTREACHED();
        return;
      }
      if (child.GetIsFolder()) {
        to_visit.push(child.GetFirstChildId());
      } else {
        // Rewrite the specifics of the node with encrypted data if necessary.
        child.ResetFromSpecifics();
      }
      to_visit.push(child.GetSuccessorId());
    }
  }

  if (routes.count(syncable::PASSWORDS) > 0) {
    // Passwords are encrypted with their own legacy scheme.
    encrypted_types.insert(syncable::PASSWORDS);
    ReadNode passwords_root(trans);
    std::string passwords_tag =
        syncable::ModelTypeToRootTag(syncable::PASSWORDS);
    if (!passwords_root.InitByTagLookup(passwords_tag)) {
      LOG(WARNING) << "No passwords to reencrypt.";
      return;
    }

    int64 child_id = passwords_root.GetFirstChildId();
    while (child_id != kInvalidId) {
      WriteNode child(trans);
      if (!child.InitByIdLookup(child_id)) {
        NOTREACHED();
        return;
      }
      child.SetPasswordSpecifics(child.GetPasswordSpecifics());
      child_id = child.GetSuccessorId();
    }
  }

  FOR_EACH_OBSERVER(SyncManager::Observer, observers_,
                    OnEncryptionComplete(encrypted_types));
}

SyncManager::~SyncManager() {
  delete data_;
}

void SyncManager::AddObserver(Observer* observer) {
  data_->AddObserver(observer);
}

void SyncManager::RemoveObserver(Observer* observer) {
  data_->RemoveObserver(observer);
}

browser_sync::JsBackend* SyncManager::GetJsBackend() {
  return data_;
}

void SyncManager::Shutdown() {
  data_->Shutdown();
}

void SyncManager::SyncInternal::Shutdown() {
  method_factory_.RevokeAll();

  if (syncer_thread()) {
    syncer_thread()->Stop();
    syncer_thread_.reset();
  }

  // We NULL out sync_notifer_ so that any pending tasks do not
  // trigger further notifications.
  // TODO(akalin): NULL the other member variables defensively, too.
  if (sync_notifier_) {
    sync_notifier_->RemoveObserver(this);
  }

  // |this| is about to be destroyed, so we have to ensure any messages
  // that were posted to core_thread_ before or during syncer thread shutdown
  // are flushed out, else they refer to garbage memory.  SendNotification
  // is an example.
  // TODO(tim): Remove this monstrosity, perhaps with ObserverListTS once core
  // thread is removed. Bug 78190.
  {
    CHECK(core_message_loop_);
    bool old_state = core_message_loop_->NestableTasksAllowed();
    core_message_loop_->SetNestableTasksAllowed(true);
    core_message_loop_->RunAllPending();
    core_message_loop_->SetNestableTasksAllowed(old_state);
  }

  net::NetworkChangeNotifier::RemoveIPAddressObserver(this);

  connection_manager_hookup_.reset();

  if (dir_manager()) {
    dir_manager()->FinalSaveChangesForAll();
    dir_manager()->Close(username_for_share());
  }

  // Reset the DirectoryManager and UserSettings so they relinquish sqlite
  // handles to backing files.
  share_.dir_manager.reset();

  core_message_loop_ = NULL;
}

void SyncManager::SyncInternal::OnIPAddressChanged() {
  VLOG(1) << "IP address change detected";
#if defined (OS_CHROMEOS)
  // TODO(tim): This is a hack to intentionally lose a race with flimflam at
  // shutdown, so we don't cause shutdown to wait for our http request.
  // http://crosbug.com/8429
  MessageLoop::current()->PostDelayedTask(FROM_HERE,
      method_factory_.NewRunnableMethod(&SyncInternal::OnIPAddressChangedImpl),
      kChromeOSNetworkChangeReactionDelayHackMsec);
#else
  OnIPAddressChangedImpl();
#endif  // defined(OS_CHROMEOS)
}

void SyncManager::SyncInternal::OnIPAddressChangedImpl() {
  // TODO(akalin): CheckServerReachable() can block, which may cause
  // jank if we try to shut down sync.  Fix this.
  connection_manager()->CheckServerReachable();
  RequestNudge(FROM_HERE);
}

void SyncManager::SyncInternal::OnServerConnectionEvent(
    const ServerConnectionEvent2& event) {
  ServerConnectionEvent legacy;
  legacy.what_happened = ServerConnectionEvent::STATUS_CHANGED;
  legacy.connection_code = event.connection_code;
  legacy.server_reachable = event.server_reachable;
  HandleServerConnectionEvent(legacy);
}

void SyncManager::SyncInternal::HandleServerConnectionEvent(
    const ServerConnectionEvent& event) {
  allstatus_.HandleServerConnectionEvent(event);
  if (event.what_happened == ServerConnectionEvent::STATUS_CHANGED) {
    if (event.connection_code ==
        browser_sync::HttpResponse::SERVER_CONNECTION_OK) {
      FOR_EACH_OBSERVER(SyncManager::Observer, observers_,
                        OnAuthError(AuthError::None()));
    }

    if (event.connection_code == browser_sync::HttpResponse::SYNC_AUTH_ERROR) {
      FOR_EACH_OBSERVER(
          SyncManager::Observer, observers_,
          OnAuthError(AuthError(AuthError::INVALID_GAIA_CREDENTIALS)));
    }
  }
}

void SyncManager::SyncInternal::HandleTransactionCompleteChangeEvent(
    const syncable::ModelTypeBitSet& models_with_changes) {
  // This notification happens immediately after the transaction mutex is
  // released. This allows work to be performed without blocking other threads
  // from acquiring a transaction.
  if (observers_.size() <= 0)
    return;

  // Call commit.
  for (int i = 0; i < syncable::MODEL_TYPE_COUNT; ++i) {
    if (models_with_changes.test(i)) {
      FOR_EACH_OBSERVER(SyncManager::Observer, observers_,
                        OnChangesComplete(syncable::ModelTypeFromInt(i)));
    }
  }
}

ModelTypeBitSet SyncManager::SyncInternal::HandleTransactionEndingChangeEvent(
    syncable::BaseTransaction* trans) {
  // This notification happens immediately before a syncable WriteTransaction
  // falls out of scope. It happens while the channel mutex is still held,
  // and while the transaction mutex is held, so it cannot be re-entrant.
  if (observers_.size() <= 0 || ChangeBuffersAreEmpty())
    return ModelTypeBitSet();

  // This will continue the WriteTransaction using a read only wrapper.
  // This is the last chance for read to occur in the WriteTransaction
  // that's closing. This special ReadTransaction will not close the
  // underlying transaction.
  ReadTransaction read_trans(GetUserShare(), trans);

  syncable::ModelTypeBitSet models_with_changes;
  for (int i = 0; i < syncable::MODEL_TYPE_COUNT; ++i) {
    if (change_buffers_[i].IsEmpty())
      continue;

    vector<ChangeRecord> ordered_changes;
    change_buffers_[i].GetAllChangesInTreeOrder(&read_trans, &ordered_changes);
    if (!ordered_changes.empty()) {
      FOR_EACH_OBSERVER(
          SyncManager::Observer, observers_,
          OnChangesApplied(syncable::ModelTypeFromInt(i), &read_trans,
                           &ordered_changes[0], ordered_changes.size()));
      models_with_changes.set(i, true);
    }
    change_buffers_[i].Clear();
  }
  return models_with_changes;
}

void SyncManager::SyncInternal::HandleCalculateChangesChangeEventFromSyncApi(
    const OriginalEntries& originals,
    const WriterTag& writer,
    syncable::BaseTransaction* trans) {
  // We have been notified about a user action changing a sync model.
  DCHECK(writer == syncable::SYNCAPI ||
         writer == syncable::UNITTEST);
  LOG_IF(WARNING, !ChangeBuffersAreEmpty()) <<
      "CALCULATE_CHANGES called with unapplied old changes.";

  bool exists_unsynced_items = false;
  bool only_preference_changes = true;
  syncable::ModelTypeBitSet model_types;
  for (syncable::OriginalEntries::const_iterator i = originals.begin();
       i != originals.end() && !exists_unsynced_items;
       ++i) {
    int64 id = i->ref(syncable::META_HANDLE);
    syncable::Entry e(trans, syncable::GET_BY_HANDLE, id);
    DCHECK(e.good());

    syncable::ModelType model_type = e.GetModelType();

    if (e.Get(syncable::IS_UNSYNCED)) {
      if (model_type == syncable::TOP_LEVEL_FOLDER ||
          model_type == syncable::UNSPECIFIED) {
        NOTREACHED() << "Permanent or underspecified item changed via syncapi.";
        continue;
      }
      // Unsynced items will cause us to nudge the the syncer.
      exists_unsynced_items = true;

      model_types[model_type] = true;
      if (model_type != syncable::PREFERENCES)
        only_preference_changes = false;
    }
  }
  if (exists_unsynced_items && syncer_thread()) {
    int nudge_delay = only_preference_changes ?
        kPreferencesNudgeDelayMilliseconds : kDefaultNudgeDelayMilliseconds;
    core_message_loop_->PostTask(FROM_HERE,
        NewRunnableMethod(this, &SyncInternal::RequestNudgeWithDataTypes,
        TimeDelta::FromMilliseconds(nudge_delay),
        browser_sync::NUDGE_SOURCE_LOCAL,
        model_types,
        FROM_HERE));
  }
}

void SyncManager::SyncInternal::SetExtraChangeRecordData(int64 id,
    syncable::ModelType type, ChangeReorderBuffer* buffer,
    Cryptographer* cryptographer, const syncable::EntryKernel& original,
    bool existed_before, bool exists_now) {
  // If this is a deletion and the datatype was encrypted, we need to decrypt it
  // and attach it to the buffer.
  if (!exists_now && existed_before) {
    sync_pb::EntitySpecifics original_specifics(original.ref(SPECIFICS));
    if (type == syncable::PASSWORDS) {
      // Passwords must use their own legacy ExtraPasswordChangeRecordData.
      scoped_ptr<sync_pb::PasswordSpecificsData> data(
          DecryptPasswordSpecifics(original_specifics, cryptographer));
      if (!data.get()) {
        NOTREACHED();
        return;
      }
      buffer->SetExtraDataForId(id, new ExtraPasswordChangeRecordData(*data));
    } else if (original_specifics.has_encrypted()) {
      // All other datatypes can just create a new unencrypted specifics and
      // attach it.
      const sync_pb::EncryptedData& encrypted = original_specifics.encrypted();
      if (!cryptographer->Decrypt(encrypted, &original_specifics)) {
        NOTREACHED();
        return;
      }
    }
    buffer->SetSpecificsForId(id, original_specifics);
  }
}

void SyncManager::SyncInternal::HandleCalculateChangesChangeEventFromSyncer(
    const OriginalEntries& originals,
    const WriterTag& writer,
    syncable::BaseTransaction* trans) {
  // We only expect one notification per sync step, so change_buffers_ should
  // contain no pending entries.
  DCHECK(writer == syncable::SYNCER ||
         writer == syncable::UNITTEST);
  LOG_IF(WARNING, !ChangeBuffersAreEmpty()) <<
      "CALCULATE_CHANGES called with unapplied old changes.";

  Cryptographer* crypto = dir_manager()->GetCryptographer(trans);
  for (syncable::OriginalEntries::const_iterator i = originals.begin();
       i != originals.end(); ++i) {
    int64 id = i->ref(syncable::META_HANDLE);
    syncable::Entry e(trans, syncable::GET_BY_HANDLE, id);
    bool existed_before = !i->ref(syncable::IS_DEL);
    bool exists_now = e.good() && !e.Get(syncable::IS_DEL);
    DCHECK(e.good());

    // Omit items that aren't associated with a model.
    syncable::ModelType type = e.GetModelType();
    if (type == syncable::TOP_LEVEL_FOLDER || type == syncable::UNSPECIFIED)
      continue;

    if (exists_now && !existed_before)
      change_buffers_[type].PushAddedItem(id);
    else if (!exists_now && existed_before)
      change_buffers_[type].PushDeletedItem(id);
    else if (exists_now && existed_before &&
             VisiblePropertiesDiffer(*i, e, crypto)) {
      change_buffers_[type].PushUpdatedItem(id, VisiblePositionsDiffer(*i, e));
    }

    SetExtraChangeRecordData(id, type, &change_buffers_[type], crypto, *i,
                             existed_before, exists_now);
  }
}

SyncManager::Status SyncManager::SyncInternal::GetStatus() {
  return allstatus_.status();
}

void SyncManager::SyncInternal::RequestNudge(
    const tracked_objects::Location& location) {
  if (syncer_thread())
     syncer_thread()->ScheduleNudge(
        TimeDelta::FromMilliseconds(0), browser_sync::NUDGE_SOURCE_LOCAL,
        ModelTypeBitSet(), location);
}

void SyncManager::SyncInternal::RequestNudgeWithDataTypes(
    const TimeDelta& delay,
    browser_sync::NudgeSource source, const ModelTypeBitSet& types,
    const tracked_objects::Location& nudge_location) {
  if (syncer_thread())
     syncer_thread()->ScheduleNudge(delay, source, types, nudge_location);
}

void SyncManager::SyncInternal::OnSyncEngineEvent(
    const SyncEngineEvent& event) {
  if (observers_.size() <= 0)
    return;

  // Only send an event if this is due to a cycle ending and this cycle
  // concludes a canonical "sync" process; that is, based on what is known
  // locally we are "all happy" and up-to-date.  There may be new changes on
  // the server, but we'll get them on a subsequent sync.
  //
  // Notifications are sent at the end of every sync cycle, regardless of
  // whether we should sync again.
  if (event.what_happened == SyncEngineEvent::SYNC_CYCLE_ENDED) {
    ModelSafeRoutingInfo enabled_types;
    registrar_->GetModelSafeRoutingInfo(&enabled_types);
    {
      // Check to see if we need to notify the frontend that we have newly
      // encrypted types or that we require a passphrase.
      sync_api::ReadTransaction trans(GetUserShare());
      sync_api::ReadNode node(&trans);
      if (!node.InitByTagLookup(kNigoriTag)) {
        DCHECK(!event.snapshot->is_share_usable);
        return;
      }
      const sync_pb::NigoriSpecifics& nigori = node.GetNigoriSpecifics();
      syncable::ModelTypeSet encrypted_types =
          syncable::GetEncryptedDataTypesFromNigori(nigori);
      // If passwords are enabled, they're automatically considered encrypted.
      if (enabled_types.count(syncable::PASSWORDS) > 0)
        encrypted_types.insert(syncable::PASSWORDS);
      if (!encrypted_types.empty()) {
        Cryptographer* cryptographer = trans.GetCryptographer();
        if (!cryptographer->is_ready() && !cryptographer->has_pending_keys()) {
          if (!nigori.encrypted().blob().empty()) {
            DCHECK(!cryptographer->CanDecrypt(nigori.encrypted()));
            cryptographer->SetPendingKeys(nigori.encrypted());
          }
        }

        // If we've completed a sync cycle and the cryptographer isn't ready
        // yet, prompt the user for a passphrase.
        if (cryptographer->has_pending_keys()) {
          FOR_EACH_OBSERVER(SyncManager::Observer, observers_,
                            OnPassphraseRequired(true));
        } else if (!cryptographer->is_ready()) {
          FOR_EACH_OBSERVER(SyncManager::Observer, observers_,
                            OnPassphraseRequired(false));
        } else {
          FOR_EACH_OBSERVER(SyncManager::Observer, observers_,
                            OnEncryptionComplete(encrypted_types));
        }
      }
    }

    if (!initialized())
      return;

    if (!event.snapshot->has_more_to_sync) {
      FOR_EACH_OBSERVER(SyncManager::Observer, observers_,
                        OnSyncCycleCompleted(event.snapshot));
    }

    // This is here for tests, which are still using p2p notifications.
    // SendNotification does not do anything if we are using server based
    // notifications.
    // TODO(chron): Consider changing this back to track has_more_to_sync
    // only notify peers if a successful commit has occurred.
    bool new_notification =
        (event.snapshot->syncer_status.num_successful_commits > 0);
    if (new_notification) {
      core_message_loop_->PostTask(
          FROM_HERE,
          NewRunnableMethod(
              this,
              &SyncManager::SyncInternal::SendNotification));
    }
  }

  if (event.what_happened == SyncEngineEvent::STOP_SYNCING_PERMANENTLY) {
    FOR_EACH_OBSERVER(SyncManager::Observer, observers_,
                      OnStopSyncingPermanently());
    return;
  }

  if (event.what_happened == SyncEngineEvent::CLEAR_SERVER_DATA_SUCCEEDED) {
    FOR_EACH_OBSERVER(SyncManager::Observer, observers_,
                      OnClearServerDataSucceeded());
    return;
  }

  if (event.what_happened == SyncEngineEvent::CLEAR_SERVER_DATA_FAILED) {
    FOR_EACH_OBSERVER(SyncManager::Observer, observers_,
                      OnClearServerDataFailed());
    return;
  }

  if (event.what_happened == SyncEngineEvent::UPDATED_TOKEN) {
    FOR_EACH_OBSERVER(SyncManager::Observer, observers_,
                      OnUpdatedToken(event.updated_token));
    return;
  }
}

void SyncManager::SyncInternal::SetParentJsEventRouter(
    browser_sync::JsEventRouter* router) {
  DCHECK(router);
  parent_router_ = router;
}

void SyncManager::SyncInternal::RemoveParentJsEventRouter() {
  parent_router_ = NULL;
}

const browser_sync::JsEventRouter*
    SyncManager::SyncInternal::GetParentJsEventRouter() const {
  return parent_router_;
}

namespace {

void LogNoRouter(const std::string& name,
                 const browser_sync::JsArgList& args) {
  VLOG(1) << "No parent router; not replying to message " << name
          << " with args " << args.ToString();
}

}  // namespace

void SyncManager::SyncInternal::ProcessMessage(
    const std::string& name, const browser_sync::JsArgList& args,
    const browser_sync::JsEventHandler* sender) {
  DCHECK(initialized_);
  if (name == "getNotificationState") {
    if (!parent_router_) {
      LogNoRouter(name, args);
      return;
    }
    bool notifications_enabled = allstatus_.status().notifications_enabled;
    ListValue return_args;
    return_args.Append(Value::CreateBooleanValue(notifications_enabled));
    parent_router_->RouteJsEvent(
        "onGetNotificationStateFinished",
        browser_sync::JsArgList(return_args), sender);
  } else if (name == "getNotificationInfo") {
    if (!parent_router_) {
      LogNoRouter(name, args);
      return;
    }

    ListValue return_args;
    return_args.Append(NotificationInfoToValue(notification_info_map_));
    parent_router_->RouteJsEvent("onGetNotificationInfoFinished",
        browser_sync::JsArgList(return_args), sender);
  } else if (name == "getRootNode") {
    if (!parent_router_) {
      LogNoRouter(name, args);
      return;
    }
    ReadTransaction trans(GetUserShare());
    ReadNode root(&trans);
    root.InitByRootLookup();
    ListValue return_args;
    return_args.Append(root.ToValue());
    parent_router_->RouteJsEvent(
        "onGetRootNodeFinished",
        browser_sync::JsArgList(return_args), sender);
  } else if (name == "getNodeById") {
    if (!parent_router_) {
      LogNoRouter(name, args);
      return;
    }
    parent_router_->RouteJsEvent(
        "onGetNodeByIdFinished", ProcessGetNodeByIdMessage(args), sender);
  } else if (name == "findNodesContainingString") {
    if (!parent_router_) {
      LogNoRouter(name, args);
      return;
    }
    parent_router_->RouteJsEvent(
        "onFindNodesContainingStringFinished",
        ProcessFindNodesContainingString(args), sender);
  } else {
    VLOG(1) << "Dropping unknown message " << name
              << " with args " << args.ToString();
  }
}

browser_sync::JsArgList SyncManager::SyncInternal::ProcessGetNodeByIdMessage(
    const browser_sync::JsArgList& args) {
  ListValue null_return_args_list;
  null_return_args_list.Append(Value::CreateNullValue());
  browser_sync::JsArgList null_return_args(null_return_args_list);
  std::string id_str;
  if (!args.Get().GetString(0, &id_str)) {
    return null_return_args;
  }
  int64 id;
  if (!base::StringToInt64(id_str, &id)) {
    return null_return_args;
  }
  if (id == kInvalidId) {
    return null_return_args;
  }
  ReadTransaction trans(GetUserShare());
  ReadNode node(&trans);
  if (!node.InitByIdLookup(id)) {
    return null_return_args;
  }
  ListValue return_args;
  return_args.Append(node.ToValue());
  return browser_sync::JsArgList(return_args);
}

browser_sync::JsArgList SyncManager::SyncInternal::
    ProcessFindNodesContainingString(
    const browser_sync::JsArgList& args) {
  std::string query;
  ListValue return_args;
  if (!args.Get().GetString(0, &query)) {
    return_args.Append(new ListValue());
    return browser_sync::JsArgList(return_args);
  }

  ListValue* result = FindNodesContainingString(query);
  return_args.Append(result);
  return browser_sync::JsArgList(return_args);
}

void SyncManager::SyncInternal::OnNotificationStateChange(
    bool notifications_enabled) {
  VLOG(1) << "P2P: Notifications enabled = "
          << (notifications_enabled ? "true" : "false");
  allstatus_.SetNotificationsEnabled(notifications_enabled);
  if (syncer_thread()) {
    syncer_thread()->set_notifications_enabled(notifications_enabled);
  }
  if (parent_router_) {
    ListValue args;
    args.Append(Value::CreateBooleanValue(notifications_enabled));
    // TODO(akalin): Tidy up grammar in event names.
    parent_router_->RouteJsEvent("onSyncNotificationStateChange",
                                 browser_sync::JsArgList(args), NULL);
  }
}

void SyncManager::SyncInternal::UpdateNotificationInfo(
    const syncable::ModelTypePayloadMap& type_payloads) {
  for (syncable::ModelTypePayloadMap::const_iterator it = type_payloads.begin();
       it != type_payloads.end(); ++it) {
    NotificationInfo* info = &notification_info_map_[it->first];
    info->total_count++;
    info->payload = it->second;
  }
}

void SyncManager::SyncInternal::OnIncomingNotification(
    const syncable::ModelTypePayloadMap& type_payloads) {
  if (!type_payloads.empty()) {
    if (syncer_thread()) {
      syncer_thread()->ScheduleNudgeWithPayloads(
          TimeDelta::FromMilliseconds(kSyncerThreadDelayMsec),
          browser_sync::NUDGE_SOURCE_NOTIFICATION,
          type_payloads, FROM_HERE);
    }
    allstatus_.IncrementNotificationsReceived();
    UpdateNotificationInfo(type_payloads);
  } else {
    LOG(WARNING) << "Sync received notification without any type information.";
  }

  if (parent_router_) {
    ListValue args;
    ListValue* changed_types = new ListValue();
    args.Append(changed_types);
    for (syncable::ModelTypePayloadMap::const_iterator
             it = type_payloads.begin();
         it != type_payloads.end(); ++it) {
      const std::string& model_type_str =
          syncable::ModelTypeToString(it->first);
      changed_types->Append(Value::CreateStringValue(model_type_str));
    }
    parent_router_->RouteJsEvent("onSyncIncomingNotification",
                                 browser_sync::JsArgList(args), NULL);
  }
}

void SyncManager::SyncInternal::StoreState(
    const std::string& state) {
  syncable::ScopedDirLookup lookup(dir_manager(), username_for_share());
  if (!lookup.good()) {
    LOG(ERROR) << "Could not write notification state";
    // TODO(akalin): Propagate result callback all the way to this
    // function and call it with "false" to signal failure.
    return;
  }
  if (VLOG_IS_ON(1)) {
    std::string encoded_state;
    base::Base64Encode(state, &encoded_state);
    VLOG(1) << "Writing notification state: " << encoded_state;
  }
  lookup->SetNotificationState(state);
  lookup->SaveChanges();
}

void SyncManager::SyncInternal::AddObserver(
    SyncManager::Observer* observer) {
  observers_.AddObserver(observer);
}

void SyncManager::SyncInternal::RemoveObserver(
    SyncManager::Observer* observer) {
  observers_.RemoveObserver(observer);
}

SyncManager::Status::Summary SyncManager::GetStatusSummary() const {
  return data_->GetStatus().summary;
}

SyncManager::Status SyncManager::GetDetailedStatus() const {
  return data_->GetStatus();
}

SyncManager::SyncInternal* SyncManager::GetImpl() const { return data_; }

void SyncManager::SaveChanges() {
  data_->SaveChanges();
}

void SyncManager::SyncInternal::SaveChanges() {
  syncable::ScopedDirLookup lookup(dir_manager(), username_for_share());
  if (!lookup.good()) {
    DCHECK(false) << "ScopedDirLookup creation failed; Unable to SaveChanges";
    return;
  }
  lookup->SaveChanges();
}

//////////////////////////////////////////////////////////////////////////
// BaseTransaction member definitions
BaseTransaction::BaseTransaction(UserShare* share)
    : lookup_(NULL) {
  DCHECK(share && share->dir_manager.get());
  lookup_ = new syncable::ScopedDirLookup(share->dir_manager.get(),
                                          share->name);
  cryptographer_ = share->dir_manager->GetCryptographer(this);
  if (!(lookup_->good()))
    DCHECK(false) << "ScopedDirLookup failed on valid DirManager.";
}
BaseTransaction::~BaseTransaction() {
  delete lookup_;
}

UserShare* SyncManager::GetUserShare() const {
  DCHECK(data_->initialized()) << "GetUserShare requires initialization!";
  return data_->GetUserShare();
}

bool SyncManager::HasUnsyncedItems() const {
  sync_api::ReadTransaction trans(GetUserShare());
  return (trans.GetWrappedTrans()->directory()->unsynced_entity_count() != 0);
}

void SyncManager::TriggerOnNotificationStateChangeForTest(
    bool notifications_enabled) {
  data_->OnNotificationStateChange(notifications_enabled);
}

void SyncManager::TriggerOnIncomingNotificationForTest(
    const syncable::ModelTypeBitSet& model_types) {
  syncable::ModelTypePayloadMap model_types_with_payloads =
      syncable::ModelTypePayloadMapFromBitSet(model_types,
          std::string());

  data_->OnIncomingNotification(model_types_with_payloads);
}

}  // namespace sync_api
