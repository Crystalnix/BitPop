// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/abstract_profile_sync_service_test.h"

#include "chrome/browser/sync/engine/syncapi.h"
#include "chrome/browser/sync/protocol/sync.pb.h"
#include "chrome/browser/sync/syncable/directory_manager.h"
#include "chrome/browser/sync/syncable/syncable.h"
#include "chrome/browser/sync/test_profile_sync_service.h"
#include "chrome/browser/sync/util/cryptographer.h"
#include "chrome/test/sync/engine/test_id_factory.h"

using browser_sync::TestIdFactory;
using sync_api::UserShare;
using syncable::BASE_VERSION;
using syncable::CREATE;
using syncable::DirectoryManager;
using syncable::IS_DEL;
using syncable::IS_DIR;
using syncable::IS_UNAPPLIED_UPDATE;
using syncable::IS_UNSYNCED;
using syncable::ModelType;
using syncable::MutableEntry;
using syncable::SERVER_IS_DIR;
using syncable::SERVER_VERSION;
using syncable::SPECIFICS;
using syncable::ScopedDirLookup;
using syncable::UNIQUE_SERVER_TAG;
using syncable::UNITTEST;
using syncable::WriteTransaction;

const std::string ProfileSyncServiceTestHelper::GetTagForType(
    ModelType model_type) {
  return syncable::ModelTypeToRootTag(model_type);
}

bool ProfileSyncServiceTestHelper::CreateRoot(
    ModelType model_type, UserShare* user_share,
    TestIdFactory* ids) {
  DirectoryManager* dir_manager = user_share->dir_manager.get();

  ScopedDirLookup dir(dir_manager, user_share->name);
  if (!dir.good())
    return false;

  std::string tag_name = GetTagForType(model_type);

  WriteTransaction wtrans(dir, UNITTEST, __FILE__, __LINE__);
  MutableEntry node(&wtrans,
                    CREATE,
                    wtrans.root_id(),
                    tag_name);
  node.Put(UNIQUE_SERVER_TAG, tag_name);
  node.Put(IS_DIR, true);
  node.Put(SERVER_IS_DIR, false);
  node.Put(IS_UNSYNCED, false);
  node.Put(IS_UNAPPLIED_UPDATE, false);
  node.Put(SERVER_VERSION, 20);
  node.Put(BASE_VERSION, 20);
  node.Put(IS_DEL, false);
  node.Put(syncable::ID, ids->MakeServer(tag_name));
  sync_pb::EntitySpecifics specifics;
  syncable::AddDefaultExtensionValue(model_type, &specifics);
  node.Put(SPECIFICS, specifics);

  return true;
}

AbstractProfileSyncServiceTest::AbstractProfileSyncServiceTest()
    : ui_thread_(BrowserThread::UI, &ui_loop_),
      db_thread_(BrowserThread::DB),
      io_thread_(BrowserThread::IO) {}

AbstractProfileSyncServiceTest::~AbstractProfileSyncServiceTest() {}

void AbstractProfileSyncServiceTest::SetUp() {
  db_thread_.Start();
  base::Thread::Options options;
  options.message_loop_type = MessageLoop::TYPE_IO;
  io_thread_.StartWithOptions(options);
}

void AbstractProfileSyncServiceTest::TearDown() {
  // Pump messages posted by the sync core thread (which may end up
  // posting on the IO thread).
  ui_loop_.RunAllPending();
  io_thread_.Stop();
  db_thread_.Stop();
  ui_loop_.RunAllPending();
}

bool AbstractProfileSyncServiceTest::CreateRoot(ModelType model_type) {
  return ProfileSyncServiceTestHelper::CreateRoot(
      model_type,
      service_->GetUserShare(),
      service_->id_factory());
}

CreateRootTask::CreateRootTask(
    AbstractProfileSyncServiceTest* test, ModelType model_type)
    : test_(test), model_type_(model_type), success_(false) {
}

CreateRootTask::~CreateRootTask() {}
void CreateRootTask::Run() {
  success_ = test_->CreateRoot(model_type_);
}

bool CreateRootTask::success() {
  return success_;
}
