// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sync/profile_sync_service_harness.h"
#include "chrome/browser/sync/test/integration/bookmarks_helper.h"
#include "chrome/browser/sync/test/integration/sync_test.h"

using bookmarks_helper::AddFolder;
using bookmarks_helper::AddURL;
using bookmarks_helper::GetBookmarkBarNode;
using bookmarks_helper::GetOtherNode;
using bookmarks_helper::ModelMatchesVerifier;
using bookmarks_helper::Move;
using bookmarks_helper::Remove;
using bookmarks_helper::SetTitle;

class SingleClientBookmarksSyncTest : public SyncTest {
 public:
  SingleClientBookmarksSyncTest() : SyncTest(SINGLE_CLIENT) {}
  virtual ~SingleClientBookmarksSyncTest() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(SingleClientBookmarksSyncTest);
};

IN_PROC_BROWSER_TEST_F(SingleClientBookmarksSyncTest, OfflineToOnline) {
  ASSERT_TRUE(SetupSync()) << "SetupSync() failed.";

  DisableNetwork(GetProfile(0));
  const BookmarkNode* node = AddFolder(0, L"title");
  SetTitle(0, node, L"new_title");
  // Expect that we backoff exponentially while we are unable to contact the
  // server.
  ASSERT_TRUE(GetClient(0)->AwaitExponentialBackoffVerification());

  EnableNetwork(GetProfile(0));
  ASSERT_TRUE(GetClient(0)->AwaitFullSyncCompletion("Commit changes."));
  ASSERT_TRUE(ModelMatchesVerifier(0));
}

IN_PROC_BROWSER_TEST_F(SingleClientBookmarksSyncTest, Sanity) {
  ASSERT_TRUE(SetupClients()) << "SetupClients() failed.";

  // Starting state:
  // other_node
  //    -> top
  //      -> tier1_a
  //        -> http://mail.google.com  "tier1_a_url0"
  //        -> http://www.pandora.com  "tier1_a_url1"
  //        -> http://www.facebook.com "tier1_a_url2"
  //      -> tier1_b
  //        -> http://www.nhl.com "tier1_b_url0"
  const BookmarkNode* top = AddFolder(0, GetOtherNode(0), 0, L"top");
  const BookmarkNode* tier1_a = AddFolder(0, top, 0, L"tier1_a");
  const BookmarkNode* tier1_b = AddFolder(0, top, 1, L"tier1_b");
  const BookmarkNode* tier1_a_url0 = AddURL(
      0, tier1_a, 0, L"tier1_a_url0", GURL("http://mail.google.com"));
  const BookmarkNode* tier1_a_url1 = AddURL(
      0, tier1_a, 1, L"tier1_a_url1", GURL("http://www.pandora.com"));
  const BookmarkNode* tier1_a_url2 = AddURL(
      0, tier1_a, 2, L"tier1_a_url2", GURL("http://www.facebook.com"));
  const BookmarkNode* tier1_b_url0 = AddURL(
      0, tier1_b, 0, L"tier1_b_url0", GURL("http://www.nhl.com"));

  ASSERT_TRUE(SetupSync()) << "SetupSync() failed.";
  ASSERT_TRUE(GetClient(0)->AwaitFullSyncCompletion(
      "Waiting for initial sync completed."));
  ASSERT_TRUE(ModelMatchesVerifier(0));

  //  Ultimately we want to end up with the following model; but this test is
  //  more about the journey than the destination.
  //
  //  bookmark_bar
  //    -> CNN (www.cnn.com)
  //    -> tier1_a
  //      -> tier1_a_url2 (www.facebook.com)
  //      -> tier1_a_url1 (www.pandora.com)
  //    -> Porsche (www.porsche.com)
  //    -> Bank of America (www.bankofamerica.com)
  //    -> Seattle Bubble
  //  other_node
  //    -> top
  //      -> tier1_b
  //        -> Wired News (www.wired.com)
  //        -> tier2_b
  //          -> tier1_b_url0
  //          -> tier3_b
  //            -> Toronto Maple Leafs (mapleleafs.nhl.com)
  //            -> Wynn (www.wynnlasvegas.com)
  //      -> tier1_a_url0
  const BookmarkNode* bar = GetBookmarkBarNode(0);
  const BookmarkNode* cnn = AddURL(0, bar, 0, L"CNN",
      GURL("http://www.cnn.com"));
  ASSERT_TRUE(cnn != NULL);
  Move(0, tier1_a, bar, 1);
  ASSERT_TRUE(GetClient(0)->AwaitFullSyncCompletion("Bookmark moved."));
  ASSERT_TRUE(ModelMatchesVerifier(0));

  const BookmarkNode* porsche = AddURL(0, bar, 2, L"Porsche",
      GURL("http://www.porsche.com"));
  // Rearrange stuff in tier1_a.
  ASSERT_EQ(tier1_a, tier1_a_url2->parent());
  ASSERT_EQ(tier1_a, tier1_a_url1->parent());
  Move(0, tier1_a_url2, tier1_a, 0);
  Move(0, tier1_a_url1, tier1_a, 2);
  ASSERT_TRUE(GetClient(0)->AwaitFullSyncCompletion(
      "Rearrange stuff in tier1_a"));
  ASSERT_TRUE(ModelMatchesVerifier(0));

  ASSERT_EQ(1, tier1_a_url0->parent()->GetIndexOf(tier1_a_url0));
  Move(0, tier1_a_url0, bar, bar->child_count());
  const BookmarkNode* boa = AddURL(0, bar, bar->child_count(),
      L"Bank of America", GURL("https://www.bankofamerica.com"));
  ASSERT_TRUE(boa != NULL);
  Move(0, tier1_a_url0, top, top->child_count());
  const BookmarkNode* bubble = AddURL(
      0, bar, bar->child_count(), L"Seattle Bubble",
          GURL("http://seattlebubble.com"));
  ASSERT_TRUE(bubble != NULL);
  const BookmarkNode* wired = AddURL(0, bar, 2, L"Wired News",
      GURL("http://www.wired.com"));
  const BookmarkNode* tier2_b = AddFolder(
      0, tier1_b, 0, L"tier2_b");
  Move(0, tier1_b_url0, tier2_b, 0);
  Move(0, porsche, bar, 0);
  SetTitle(0, wired, L"News Wired");
  SetTitle(0, porsche, L"ICanHazPorsche?");
  ASSERT_TRUE(GetClient(0)->AwaitFullSyncCompletion("Change title."));
  ASSERT_TRUE(ModelMatchesVerifier(0));

  ASSERT_EQ(tier1_a_url0->id(), top->GetChild(top->child_count() - 1)->id());
  Remove(0, top, top->child_count() - 1);
  Move(0, wired, tier1_b, 0);
  Move(0, porsche, bar, 3);
  const BookmarkNode* tier3_b = AddFolder(0, tier2_b, 1, L"tier3_b");
  const BookmarkNode* leafs = AddURL(
      0, tier1_a, 0, L"Toronto Maple Leafs", GURL("http://mapleleafs.nhl.com"));
  const BookmarkNode* wynn = AddURL(0, bar, 1, L"Wynn",
      GURL("http://www.wynnlasvegas.com"));

  Move(0, wynn, tier3_b, 0);
  Move(0, leafs, tier3_b, 0);
  ASSERT_TRUE(GetClient(0)->AwaitFullSyncCompletion(
      "Move after addition of bookmarks."));
  ASSERT_TRUE(ModelMatchesVerifier(0));
}

// Restart the sync service on a client and make sure sync is up and running.
IN_PROC_BROWSER_TEST_F(SingleClientBookmarksSyncTest,
                       DISABLED_RestartSyncService) {
  ASSERT_TRUE(SetupSync()) << "SetupSync() failed.";

  ASSERT_TRUE(AddURL(0, L"Google", GURL("http://www.google.com")));
  ASSERT_TRUE(GetClient(0)->AwaitFullSyncCompletion("Added a bookmark."));
  ASSERT_TRUE(ModelMatchesVerifier(0));

  RestartSyncService(0);
  ASSERT_TRUE(GetClient(0)->AwaitFullSyncCompletion("Restarted sync."));
  ASSERT_TRUE(ModelMatchesVerifier(0));
}
