// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/basictypes.h"
#include "base/memory/ref_counted.h"
#include "sync/engine/resolve_conflicts_command.h"
#include "sync/internal_api/public/base/model_type.h"
#include "sync/sessions/sync_session.h"
#include "sync/syncable/syncable_id.h"
#include "sync/test/engine/fake_model_worker.h"
#include "sync/test/engine/syncer_command_test.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace syncer {

namespace {

class ResolveConflictsCommandTest : public SyncerCommandTest {
 protected:
  ResolveConflictsCommandTest() {}
  virtual ~ResolveConflictsCommandTest() {}

  virtual void SetUp() {
    workers()->push_back(
        make_scoped_refptr(new FakeModelWorker(GROUP_UI)));
    workers()->push_back(
        make_scoped_refptr(new FakeModelWorker(GROUP_PASSWORD)));
    (*mutable_routing_info())[BOOKMARKS] = GROUP_UI;
    (*mutable_routing_info())[PASSWORDS] = GROUP_PASSWORD;
    SyncerCommandTest::SetUp();
  }

  ResolveConflictsCommand command_;

 private:
  DISALLOW_COPY_AND_ASSIGN(ResolveConflictsCommandTest);
};

TEST_F(ResolveConflictsCommandTest, GetGroupsToChange) {
  ExpectNoGroupsToChange(command_);
  // Put GROUP_PASSWORD in conflict.
  session()->mutable_status_controller()->
      GetUnrestrictedMutableConflictProgressForTest(GROUP_PASSWORD)->
      AddSimpleConflictingItemById(syncable::Id());
  ExpectGroupToChange(command_, GROUP_PASSWORD);
}

}  // namespace

}  // namespace syncer
