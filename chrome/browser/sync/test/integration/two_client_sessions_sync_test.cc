// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/scoped_vector.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sync/profile_sync_service_harness.h"
#include "chrome/browser/sync/sessions/session_state.h"
#include "chrome/browser/sync/test/integration/sync_test.h"
#include "chrome/browser/sync/test/integration/sessions_helper.h"

using sessions_helper::CheckInitialState;
using sessions_helper::DeleteForeignSession;
using sessions_helper::GetLocalWindows;
using sessions_helper::GetSessionData;
using sessions_helper::OpenTabAndGetLocalWindows;
using sessions_helper::ScopedWindowMap;
using sessions_helper::SyncedSessionVector;
using sessions_helper::WindowsMatch;

class TwoClientSessionsSyncTest : public SyncTest {
 public:
  TwoClientSessionsSyncTest() : SyncTest(TWO_CLIENT) {}
  virtual ~TwoClientSessionsSyncTest() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(TwoClientSessionsSyncTest);
};

static const char* kValidPassphrase = "passphrase!";
static const char* kURL1 = "http://127.0.0.1/bubba1";
static const char* kURL2 = "http://127.0.0.1/bubba2";

// TODO(zea): Test each individual session command we care about separately.
// (as well as multi-window). We're currently only checking basic single-window/
// single-tab functionality.

IN_PROC_BROWSER_TEST_F(TwoClientSessionsSyncTest, SingleClientChanged) {
  ASSERT_TRUE(SetupSync()) << "SetupSync() failed.";

  ASSERT_TRUE(CheckInitialState(0));
  ASSERT_TRUE(CheckInitialState(1));

  ScopedWindowMap client0_windows;
  ASSERT_TRUE(OpenTabAndGetLocalWindows(0, GURL(kURL1),
      client0_windows.GetMutable()));

  ASSERT_TRUE(GetClient(0)->AwaitMutualSyncCycleCompletion(GetClient(1)));

  // Get foreign session data from client 1.
  SyncedSessionVector sessions1;
  ASSERT_TRUE(GetSessionData(1, &sessions1));

  // Verify client 1's foreign session matches client 0 current window.
  ASSERT_EQ(1U, sessions1.size());
  ASSERT_TRUE(WindowsMatch(sessions1[0]->windows, *client0_windows.Get()));
}

IN_PROC_BROWSER_TEST_F(TwoClientSessionsSyncTest,
                       SingleClientEnabledEncryption) {
  ASSERT_TRUE(SetupSync()) << "SetupSync() failed.";

  ASSERT_TRUE(CheckInitialState(0));
  ASSERT_TRUE(CheckInitialState(1));

  ASSERT_TRUE(EnableEncryption(0, syncable::SESSIONS));
  ASSERT_TRUE(GetClient(0)->AwaitMutualSyncCycleCompletion(GetClient(1)));
  ASSERT_TRUE(IsEncrypted(0, syncable::SESSIONS));
  ASSERT_TRUE(IsEncrypted(1, syncable::SESSIONS));

  // Should enable encryption for all other types as well. Just check a subset.
  ASSERT_TRUE(IsEncrypted(1, syncable::PREFERENCES));
  ASSERT_TRUE(IsEncrypted(1, syncable::BOOKMARKS));
  ASSERT_TRUE(IsEncrypted(1, syncable::APPS));
}

IN_PROC_BROWSER_TEST_F(TwoClientSessionsSyncTest,
                       SingleClientEnabledEncryptionAndChanged) {
  ASSERT_TRUE(SetupSync()) << "SetupSync() failed.";

  ASSERT_TRUE(CheckInitialState(0));
  ASSERT_TRUE(CheckInitialState(1));

  ScopedWindowMap client0_windows;
  ASSERT_TRUE(OpenTabAndGetLocalWindows(0, GURL(kURL1),
      client0_windows.GetMutable()));
  ASSERT_TRUE(EnableEncryption(0, syncable::SESSIONS));
  ASSERT_TRUE(GetClient(0)->AwaitMutualSyncCycleCompletion(GetClient(1)));

  // Get foreign session data from client 1.
  ASSERT_TRUE(IsEncrypted(1, syncable::SESSIONS));
  SyncedSessionVector sessions1;
  ASSERT_TRUE(GetSessionData(1, &sessions1));

  // Verify client 1's foreign session matches client 0 current window.
  ASSERT_EQ(1U, sessions1.size());
  ASSERT_TRUE(WindowsMatch(sessions1[0]->windows, *client0_windows.Get()));
}

IN_PROC_BROWSER_TEST_F(TwoClientSessionsSyncTest,
                       BothClientsEnabledEncryption) {
  ASSERT_TRUE(SetupSync()) << "SetupSync() failed.";

  ASSERT_TRUE(CheckInitialState(0));
  ASSERT_TRUE(CheckInitialState(1));

  ASSERT_TRUE(EnableEncryption(0, syncable::SESSIONS));
  ASSERT_TRUE(EnableEncryption(1, syncable::SESSIONS));
  ASSERT_TRUE(AwaitQuiescence());
  ASSERT_TRUE(IsEncrypted(0, syncable::SESSIONS));
  ASSERT_TRUE(IsEncrypted(1, syncable::SESSIONS));
}

IN_PROC_BROWSER_TEST_F(TwoClientSessionsSyncTest, BothChanged) {
  ASSERT_TRUE(SetupSync()) << "SetupSync() failed.";

  ASSERT_TRUE(CheckInitialState(0));
  ASSERT_TRUE(CheckInitialState(1));

  // Open tabs on both clients and retain window information.
  ScopedWindowMap client0_windows;
  ASSERT_TRUE(OpenTabAndGetLocalWindows(0, GURL(kURL2),
      client0_windows.GetMutable()));
  ScopedWindowMap client1_windows;
  ASSERT_TRUE(OpenTabAndGetLocalWindows(1, GURL(kURL1),
      client1_windows.GetMutable()));

  // Wait for sync.
  ASSERT_TRUE(AwaitQuiescence());

  // Get foreign session data from client 0 and 1.
  SyncedSessionVector sessions0;
  SyncedSessionVector sessions1;
  ASSERT_TRUE(GetSessionData(0, &sessions0));
  ASSERT_TRUE(GetSessionData(1, &sessions1));

  // Verify client 1's foreign session matches client 0's current window and
  // vice versa.
  ASSERT_EQ(1U, sessions0.size());
  ASSERT_EQ(1U, sessions1.size());
  ASSERT_TRUE(WindowsMatch(sessions1[0]->windows, *client0_windows.Get()));
  ASSERT_TRUE(WindowsMatch(sessions0[0]->windows, *client1_windows.Get()));
}

// Flaky on OSX (number of conflicting nodes is off). http://crbug.com/85294.
#if defined(OS_MACOSX)
#define MAYBE_FirstChangesAndSetsPassphrase FLAKY_FirstChangesAndSetsPassphrase
#else
#define MAYBE_FirstChangesAndSetsPassphrase FirstChangesAndSetsPassphrase
#endif
IN_PROC_BROWSER_TEST_F(TwoClientSessionsSyncTest,
                       MAYBE_FirstChangesAndSetsPassphrase) {
  ASSERT_TRUE(SetupSync()) << "SetupSync() failed.";

  ASSERT_TRUE(CheckInitialState(0));
  ASSERT_TRUE(CheckInitialState(1));

  ScopedWindowMap client0_windows;
  ASSERT_TRUE(OpenTabAndGetLocalWindows(0, GURL(kURL1),
      client0_windows.GetMutable()));

  ASSERT_TRUE(EnableEncryption(0, syncable::SESSIONS));
  GetClient(0)->service()->SetPassphrase(
      kValidPassphrase,
      ProfileSyncService::EXPLICIT,
      ProfileSyncService::USER_PROVIDED);
  ASSERT_TRUE(GetClient(0)->AwaitPassphraseAccepted());
  ASSERT_TRUE(GetClient(0)->AwaitMutualSyncCycleCompletion(GetClient(1)));
  ASSERT_TRUE(GetClient(1)->AwaitPassphraseRequired());
  ASSERT_EQ(0, GetClient(1)->GetLastSessionSnapshot()->
      num_blocking_conflicting_updates);
  // We have two meta nodes (one for each client), the one tab node, plus the
  // basic preference/themes/search engines items.
  ASSERT_EQ(NumberOfDefaultSyncItems() + 3,
      GetClient(1)->GetLastSessionSnapshot()->
      num_conflicting_updates);  // The encrypted nodes.

  GetClient(1)->service()->SetPassphrase(
      kValidPassphrase,
      ProfileSyncService::EXPLICIT,
      ProfileSyncService::USER_PROVIDED);
  ASSERT_TRUE(GetClient(1)->AwaitPassphraseAccepted());
  ASSERT_TRUE(GetClient(1)->WaitForTypeEncryption(syncable::SESSIONS));

  ASSERT_TRUE(IsEncrypted(0, syncable::SESSIONS));
  ASSERT_TRUE(IsEncrypted(1, syncable::SESSIONS));
  // Get foreign session data from client 0 and 1.
  SyncedSessionVector sessions1;
  ASSERT_TRUE(GetSessionData(1, &sessions1));

  // Verify client 1's foreign session matches client 0's current window and
  // vice versa.
  ASSERT_EQ(1U, sessions1.size());
  ASSERT_TRUE(WindowsMatch(sessions1[0]->windows, *client0_windows.Get()));
}

// Flaky on OSX (number of conflicting nodes is off). http://crbug.com/85294.
#if defined(OS_MACOSX)
#define MAYBE_FirstChangesWhileSecondWaitingForPassphrase \
        FLAKY_FirstChangesWhileSecondWaitingForPassphrase
#else
#define MAYBE_FirstChangesWhileSecondWaitingForPassphrase \
        FirstChangesWhileSecondWaitingForPassphrase
#endif
IN_PROC_BROWSER_TEST_F(TwoClientSessionsSyncTest,
                       MAYBE_FirstChangesWhileSecondWaitingForPassphrase) {
  ASSERT_TRUE(SetupSync()) << "SetupSync() failed.";

  ASSERT_TRUE(CheckInitialState(0));
  ASSERT_TRUE(CheckInitialState(1));

  ASSERT_TRUE(EnableEncryption(0, syncable::SESSIONS));
  GetClient(0)->service()->SetPassphrase(
      kValidPassphrase,
      ProfileSyncService::EXPLICIT,
      ProfileSyncService::USER_PROVIDED);
  ASSERT_TRUE(GetClient(0)->AwaitPassphraseAccepted());
  ASSERT_TRUE(GetClient(0)->AwaitMutualSyncCycleCompletion(GetClient(1)));
  ASSERT_TRUE(GetClient(1)->AwaitPassphraseRequired());
  ASSERT_EQ(0, GetClient(1)->GetLastSessionSnapshot()->
      num_blocking_conflicting_updates);
  // We have nine non-blocking conflicts due to the two meta nodes (one for
  // each client), plus the basic preference/themes/search engines nodes.
  ASSERT_EQ(NumberOfDefaultSyncItems() + 2,
      GetClient(1)->GetLastSessionSnapshot()->
      num_conflicting_updates);  // The encrypted nodes.

  ScopedWindowMap client0_windows;
  ASSERT_TRUE(OpenTabAndGetLocalWindows(0, GURL(kURL1),
      client0_windows.GetMutable()));
  ASSERT_TRUE(GetClient(0)->AwaitMutualSyncCycleCompletion(GetClient(1)));
  ASSERT_EQ(0, GetClient(1)->GetLastSessionSnapshot()->
      num_blocking_conflicting_updates);
  ASSERT_EQ(NumberOfDefaultSyncItems() + 3,
      GetClient(1)->GetLastSessionSnapshot()->
      num_conflicting_updates);  // The encrypted nodes.

  GetClient(1)->service()->SetPassphrase(
      kValidPassphrase,
      ProfileSyncService::EXPLICIT,
      ProfileSyncService::USER_PROVIDED);
  ASSERT_TRUE(GetClient(1)->AwaitPassphraseAccepted());
  ASSERT_TRUE(GetClient(1)->WaitForTypeEncryption(syncable::SESSIONS));

  ASSERT_TRUE(IsEncrypted(0, syncable::SESSIONS));
  ASSERT_TRUE(IsEncrypted(1, syncable::SESSIONS));
  // Get foreign session data from client 0 and 1.
  SyncedSessionVector sessions1;
  ASSERT_TRUE(GetSessionData(1, &sessions1));

  // Verify client 1's foreign session matches client 0's current window and
  // vice versa.
  ASSERT_EQ(1U, sessions1.size());
  ASSERT_TRUE(WindowsMatch(sessions1[0]->windows, *client0_windows.Get()));
}

IN_PROC_BROWSER_TEST_F(TwoClientSessionsSyncTest,
                       SecondChangesAfterEncrAndPassphraseChange) {
  ASSERT_TRUE(SetupSync()) << "SetupSync() failed.";

  ASSERT_TRUE(CheckInitialState(0));
  ASSERT_TRUE(CheckInitialState(1));

  ASSERT_TRUE(EnableEncryption(0, syncable::SESSIONS));
  GetClient(0)->service()->SetPassphrase(
      kValidPassphrase,
      ProfileSyncService::EXPLICIT,
      ProfileSyncService::USER_PROVIDED);
  ASSERT_TRUE(GetClient(0)->AwaitPassphraseAccepted());
  ASSERT_TRUE(GetClient(0)->AwaitMutualSyncCycleCompletion(GetClient(1)));
  ASSERT_TRUE(GetClient(1)->AwaitPassphraseRequired());
  ASSERT_EQ(0, GetClient(1)->GetLastSessionSnapshot()->
      num_blocking_conflicting_updates);
  // We have two non-blocking conflicts due to the two meta nodes (one for each
  // client), plus the basic preference/themes/search engines nodes.
  ASSERT_EQ(NumberOfDefaultSyncItems() + 2,
      GetClient(1)->GetLastSessionSnapshot()->
      num_conflicting_updates);  // The encrypted nodes.

  // These changes are either made with the old passphrase or not encrypted at
  // all depending on when client 0's changes are propagated.
  ScopedWindowMap client1_windows;
  ASSERT_TRUE(OpenTabAndGetLocalWindows(1, GURL(kURL1),
      client1_windows.GetMutable()));

  // At this point we enter the passphrase, triggering a resync, in which the
  // local changes of client 1 get sent to client 0.
  GetClient(1)->service()->SetPassphrase(
      kValidPassphrase,
      ProfileSyncService::EXPLICIT,
      ProfileSyncService::USER_PROVIDED);
  ASSERT_TRUE(GetClient(1)->AwaitPassphraseAccepted());
  ASSERT_TRUE(GetClient(1)->WaitForTypeEncryption(syncable::SESSIONS));
  ASSERT_TRUE(GetClient(1)->AwaitMutualSyncCycleCompletion(GetClient(0)));
  ASSERT_EQ(0, GetClient(1)->GetLastSessionSnapshot()->
      num_conflicting_updates);

  ASSERT_TRUE(IsEncrypted(0, syncable::SESSIONS));
  ASSERT_TRUE(IsEncrypted(1, syncable::SESSIONS));
  SyncedSessionVector sessions0;
  SyncedSessionVector sessions1;
  ASSERT_TRUE(GetSessionData(0, &sessions0));
  ASSERT_FALSE(GetSessionData(1, &sessions1));
}

// Flaky on OSX (number of conflicting nodes is off). http://crbug.com/85294.
#if defined(OS_MACOSX)
#define MAYBE_SecondChangesBeforeEncrAndPassphraseChange \
        FLAKY_SecondChangesBeforeEncrAndPassphraseChange
#else
#define MAYBE_SecondChangesBeforeEncrAndPassphraseChange \
        SecondChangesBeforeEncrAndPassphraseChange
#endif
IN_PROC_BROWSER_TEST_F(TwoClientSessionsSyncTest,
                       MAYBE_SecondChangesBeforeEncrAndPassphraseChange) {
  ASSERT_TRUE(SetupSync()) << "SetupSync() failed.";

  ASSERT_TRUE(CheckInitialState(0));
  ASSERT_TRUE(CheckInitialState(1));

  // These changes are either made on client 1 without encryption.
  ScopedWindowMap client1_windows;
  ASSERT_TRUE(OpenTabAndGetLocalWindows(1, GURL(kURL1),
      client1_windows.GetMutable()));
  ASSERT_TRUE(GetClient(1)->AwaitMutualSyncCycleCompletion(GetClient(0)));

  // Turn encryption on client 0. Client 1's foreign will be encrypted with the
  // new passphrase and synced back. It will be unable to decrypt it yet.
  ASSERT_TRUE(EnableEncryption(0, syncable::SESSIONS));
  GetClient(0)->service()->SetPassphrase(
      kValidPassphrase,
      ProfileSyncService::EXPLICIT,
      ProfileSyncService::USER_PROVIDED);
  ASSERT_TRUE(GetClient(0)->AwaitPassphraseAccepted());
  ASSERT_TRUE(AwaitQuiescence());
  ASSERT_TRUE(GetClient(1)->AwaitPassphraseRequired());
  ASSERT_EQ(0, GetClient(1)->GetLastSessionSnapshot()->
      num_blocking_conflicting_updates);
  // We have three non-blocking conflicts due to the two meta nodes (one for
  // each client), the one tab node, plus the basic preference/themes/search
  // engines nodes.
  ASSERT_GE(NumberOfDefaultSyncItems() + 3,
      GetClient(1)->GetLastSessionSnapshot()->
      num_conflicting_updates);  // The encrypted nodes.

  // At this point we enter the passphrase, triggering a resync.
  GetClient(1)->service()->SetPassphrase(
      kValidPassphrase,
      ProfileSyncService::EXPLICIT,
      ProfileSyncService::USER_PROVIDED);
  ASSERT_TRUE(GetClient(1)->AwaitPassphraseAccepted());
  ASSERT_TRUE(GetClient(1)->WaitForTypeEncryption(syncable::SESSIONS));

  ASSERT_TRUE(IsEncrypted(0, syncable::SESSIONS));
  ASSERT_TRUE(IsEncrypted(1, syncable::SESSIONS));
  // Client 0's foreign data should match client 1's local data. Client 1's
  // foreign data is empty because client 0 did not open any tabs.
  SyncedSessionVector sessions0;
  SyncedSessionVector sessions1;
  ASSERT_TRUE(GetSessionData(0, &sessions0));
  ASSERT_FALSE(GetSessionData(1, &sessions1));
  ASSERT_EQ(1U, sessions0.size());
  ASSERT_TRUE(WindowsMatch(sessions0[0]->windows, *client1_windows.Get()));
}

// Flaky on OSX (number of conflicting nodes is off). http://crbug.com/85294.
#if defined(OS_MACOSX)
#define MAYBE_BothChangeWithEncryptionAndPassphrase \
        FLAKY_BothChangeWithEncryptionAndPassphrase
#else
#define MAYBE_BothChangeWithEncryptionAndPassphrase \
        BothChangeWithEncryptionAndPassphrase
#endif
IN_PROC_BROWSER_TEST_F(TwoClientSessionsSyncTest,
                       MAYBE_BothChangeWithEncryptionAndPassphrase) {
  ASSERT_TRUE(SetupSync()) << "SetupSync() failed.";

  ASSERT_TRUE(CheckInitialState(0));
  ASSERT_TRUE(CheckInitialState(1));

  GetClient(0)->service()->SetPassphrase(
      kValidPassphrase,
      ProfileSyncService::EXPLICIT,
      ProfileSyncService::USER_PROVIDED);
  ASSERT_TRUE(GetClient(0)->AwaitPassphraseAccepted());
  ASSERT_TRUE(GetClient(0)->AwaitMutualSyncCycleCompletion(GetClient(1)));
  ASSERT_TRUE(GetClient(1)->AwaitPassphraseRequired());

  // These changes will sync over to client 1, who will be unable to decrypt
  // them due to the missing passphrase.
  ScopedWindowMap client0_windows;
  ASSERT_TRUE(OpenTabAndGetLocalWindows(0, GURL(kURL1),
      client0_windows.GetMutable()));
  ASSERT_TRUE(EnableEncryption(0, syncable::SESSIONS));
  ASSERT_TRUE(GetClient(0)->AwaitMutualSyncCycleCompletion(GetClient(1)));
  ASSERT_EQ(0, GetClient(1)->GetLastSessionSnapshot()->
      num_blocking_conflicting_updates);
  // We have three non-blocking conflicts due to the two meta nodes (one for
  // each client), the one tab node, plus the basic preference/themes/search
  // engines nodes.
  ASSERT_EQ(NumberOfDefaultSyncItems() + 3,
      GetClient(1)->GetLastSessionSnapshot()->
      num_conflicting_updates);  // The encrypted nodes.

  GetClient(1)->service()->SetPassphrase(
      kValidPassphrase,
      ProfileSyncService::EXPLICIT,
      ProfileSyncService::USER_PROVIDED);
  ASSERT_TRUE(GetClient(1)->AwaitPassphraseAccepted());
  ASSERT_FALSE(GetClient(1)->service()->IsPassphraseRequired());
  ASSERT_TRUE(GetClient(1)->WaitForTypeEncryption(syncable::SESSIONS));

  // Open windows on client 1, which should automatically be encrypted.
  ScopedWindowMap client1_windows;
  ASSERT_TRUE(OpenTabAndGetLocalWindows(1, GURL(kURL2),
      client1_windows.GetMutable()));
  ASSERT_TRUE(GetClient(1)->AwaitMutualSyncCycleCompletion(GetClient(0)));

  ASSERT_TRUE(IsEncrypted(0, syncable::SESSIONS));
  ASSERT_TRUE(IsEncrypted(1, syncable::SESSIONS));
  // Get foreign session data from client 0 and 1.
  SyncedSessionVector sessions0;
  SyncedSessionVector sessions1;
  ASSERT_TRUE(GetSessionData(0, &sessions0));
  ASSERT_TRUE(GetSessionData(1, &sessions1));

  // Verify client 1's foreign session matches client 0's current window and
  // vice versa.
  ASSERT_EQ(1U, sessions0.size());
  ASSERT_EQ(1U, sessions1.size());
  ASSERT_TRUE(WindowsMatch(sessions1[0]->windows, *client0_windows.Get()));
  ASSERT_TRUE(WindowsMatch(sessions0[0]->windows, *client1_windows.Get()));
}

IN_PROC_BROWSER_TEST_F(TwoClientSessionsSyncTest, DeleteIdleSession) {
  ASSERT_TRUE(SetupSync()) << "SetupSync() failed.";

  ASSERT_TRUE(CheckInitialState(0));
  ASSERT_TRUE(CheckInitialState(1));

  // Client 0 opened some tabs then went idle.
  ScopedWindowMap client0_windows;
  ASSERT_TRUE(OpenTabAndGetLocalWindows(0, GURL(kURL1),
      client0_windows.GetMutable()));

  ASSERT_TRUE(GetClient(0)->AwaitMutualSyncCycleCompletion(GetClient(1)));

  // Get foreign session data from client 1.
  SyncedSessionVector sessions1;
  ASSERT_TRUE(GetSessionData(1, &sessions1));

  // Verify client 1's foreign session matches client 0 current window.
  ASSERT_EQ(1U, sessions1.size());
  ASSERT_TRUE(WindowsMatch(sessions1[0]->windows, *client0_windows.Get()));

  // Client 1 now deletes client 0's tabs. This frees the memory of sessions1.
  DeleteForeignSession(1, sessions1[0]->session_tag);
  ASSERT_TRUE(GetClient(1)->AwaitMutualSyncCycleCompletion(GetClient(0)));
  ASSERT_FALSE(GetSessionData(1, &sessions1));
}

IN_PROC_BROWSER_TEST_F(TwoClientSessionsSyncTest, DeleteActiveSession) {
  ASSERT_TRUE(SetupSync()) << "SetupSync() failed.";

  ASSERT_TRUE(CheckInitialState(0));
  ASSERT_TRUE(CheckInitialState(1));

  // Client 0 opened some tabs then went idle.
  ScopedWindowMap client0_windows;
  ASSERT_TRUE(OpenTabAndGetLocalWindows(0, GURL(kURL1),
      client0_windows.GetMutable()));

  ASSERT_TRUE(GetClient(0)->AwaitMutualSyncCycleCompletion(GetClient(1)));
  SyncedSessionVector sessions1;
  ASSERT_TRUE(GetSessionData(1, &sessions1));
  ASSERT_EQ(1U, sessions1.size());
  ASSERT_TRUE(WindowsMatch(sessions1[0]->windows, *client0_windows.Get()));

  // Client 1 now deletes client 0's tabs. This frees the memory of sessions1.
  DeleteForeignSession(1, sessions1[0]->session_tag);
  ASSERT_TRUE(GetClient(1)->AwaitMutualSyncCycleCompletion(GetClient(0)));
  ASSERT_FALSE(GetSessionData(1, &sessions1));

  // Client 0 becomes active again with a new tab.
  ASSERT_TRUE(OpenTabAndGetLocalWindows(0, GURL(kURL2),
      client0_windows.GetMutable()));
  ASSERT_TRUE(GetClient(0)->AwaitMutualSyncCycleCompletion(GetClient(1)));
  ASSERT_TRUE(GetSessionData(1, &sessions1));
  ASSERT_EQ(1U, sessions1.size());
  ASSERT_TRUE(WindowsMatch(sessions1[0]->windows, *client0_windows.Get()));
}
