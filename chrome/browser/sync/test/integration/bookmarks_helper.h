// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_TEST_INTEGRATION_BOOKMARKS_HELPER_H_
#define CHROME_BROWSER_SYNC_TEST_INTEGRATION_BOOKMARKS_HELPER_H_

#include <set>
#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "googleurl/src/gurl.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"

class GURL;

namespace bookmarks_helper {

// Used to access the bookmark model within a particular sync profile.
BookmarkModel* GetBookmarkModel(int index) WARN_UNUSED_RESULT;

// Used to access the bookmark bar within a particular sync profile.
const BookmarkNode* GetBookmarkBarNode(int index) WARN_UNUSED_RESULT;

// Used to access the "other bookmarks" node within a particular sync profile.
const BookmarkNode* GetOtherNode(int index) WARN_UNUSED_RESULT;

// Used to access the "Synced Bookmarks" node within a particular sync profile.
const BookmarkNode* GetSyncedBookmarksNode(int index) WARN_UNUSED_RESULT;

// Used to access the bookmarks within the verifier sync profile.
BookmarkModel* GetVerifierBookmarkModel() WARN_UNUSED_RESULT;

// Adds a URL with address |url| and title |title| to the bookmark bar of
// profile |profile|. Returns a pointer to the node that was added.
const BookmarkNode* AddURL(
    int profile,
    const std::wstring& title,
    const GURL& url) WARN_UNUSED_RESULT;

// Adds a URL with address |url| and title |title| to the bookmark bar of
// profile |profile| at position |index|. Returns a pointer to the node that
// was added.
const BookmarkNode* AddURL(
    int profile,
    int index,
    const std::wstring& title,
    const GURL& url) WARN_UNUSED_RESULT;

// Adds a URL with address |url| and title |title| under the node |parent| of
// profile |profile| at position |index|. Returns a pointer to the node that
// was added.
const BookmarkNode* AddURL(
    int profile,
    const BookmarkNode* parent,
    int index,
    const std::wstring& title,
    const GURL& url) WARN_UNUSED_RESULT;

// Adds a folder named |title| to the bookmark bar of profile |profile|.
// Returns a pointer to the folder that was added.
const BookmarkNode* AddFolder(
    int profile,
    const std::wstring& title) WARN_UNUSED_RESULT;

// Adds a folder named |title| to the bookmark bar of profile |profile| at
// position |index|. Returns a pointer to the folder that was added.
const BookmarkNode* AddFolder(
    int profile,
    int index,
    const std::wstring& title) WARN_UNUSED_RESULT;

// Adds a folder named |title| to the node |parent| in the bookmark model of
// profile |profile| at position |index|. Returns a pointer to the node that
// was added.
const BookmarkNode* AddFolder(
    int profile,
    const BookmarkNode* parent,
    int index,
    const std::wstring& title) WARN_UNUSED_RESULT;

// Changes the title of the node |node| in the bookmark model of profile
// |profile| to |new_title|.
void SetTitle(int profile,
                     const BookmarkNode* node,
                     const std::wstring& new_title);

// Sets the favicon of the node |node| (of type BookmarkNode::URL) in the
// bookmark model of profile |profile| using the data in |icon_bytes_vector|.
void SetFavicon(
    int profile,
    const BookmarkNode* node,
    const std::vector<unsigned char>& icon_bytes_vector);

// Changes the url of the node |node| in the bookmark model of profile
// |profile| to |new_url|. Returns a pointer to the node with the changed url.
const BookmarkNode* SetURL(
    int profile,
    const BookmarkNode* node,
    const GURL& new_url) WARN_UNUSED_RESULT;

// Moves the node |node| in the bookmark model of profile |profile| so it ends
// up under the node |new_parent| at position |index|.
void Move(
    int profile,
    const BookmarkNode* node,
    const BookmarkNode* new_parent,
    int index);

// Removes the node in the bookmark model of profile |profile| under the node
// |parent| at position |index|.
void Remove(int profile, const BookmarkNode* parent, int index);

// Sorts the children of the node |parent| in the bookmark model of profile
// |profile|.
void SortChildren(int profile, const BookmarkNode* parent);

// Reverses the order of the children of the node |parent| in the bookmark
// model of profile |profile|.
void ReverseChildOrder(int profile, const BookmarkNode* parent);

// Checks if the bookmark model of profile |profile| matches the verifier
// bookmark model. Returns true if they match.
bool ModelMatchesVerifier(int profile) WARN_UNUSED_RESULT;

// Checks if the bookmark models of all sync profiles match the verifier
// bookmark model. Returns true if they match.
bool AllModelsMatchVerifier() WARN_UNUSED_RESULT;

// Checks if the bookmark models of |profile_a| and |profile_b| match each
// other. Returns true if they match.
bool ModelsMatch(int profile_a, int profile_b) WARN_UNUSED_RESULT;

// Checks if the bookmark models of all sync profiles match each other. Does
// not compare them with the verifier bookmark model. Returns true if they
// match.
bool AllModelsMatch() WARN_UNUSED_RESULT;

// Checks if the bookmark model of profile |profile| contains any instances of
// two bookmarks with the same URL under the same parent folder. Returns true
// if even one instance is found.
bool ContainsDuplicateBookmarks(int profile);

// Returns whether a node exists with the specified url.
bool HasNodeWithURL(int profile, const GURL& url);

// Gets the node in the bookmark model of profile |profile| that has the url
// |url|. Note: Only one instance of |url| is assumed to be present.
const BookmarkNode* GetUniqueNodeByURL(
    int profile,
    const GURL& url) WARN_UNUSED_RESULT;

// Returns the number of bookmarks in bookmark model of profile |profile|
// whose titles match the string |title|.
int CountBookmarksWithTitlesMatching(
    int profile,
    const std::wstring& title) WARN_UNUSED_RESULT;

// Returns the number of bookmark folders in the bookmark model of profile
// |profile| whose titles contain the query string |title|.
int CountFoldersWithTitlesMatching(
    int profile,
    const std::wstring& title) WARN_UNUSED_RESULT;

// Creates a unique favicon using |seed|.
std::vector<unsigned char> CreateFavicon(int seed);

// Returns a URL identifiable by |i|.
std::string IndexedURL(int i);

// Returns a URL title identifiable by |i|.
std::wstring IndexedURLTitle(int i);

// Returns a folder name identifiable by |i|.
std::wstring IndexedFolderName(int i);

// Returns a subfolder name identifiable by |i|.
std::wstring IndexedSubfolderName(int i);

// Returns a subsubfolder name identifiable by |i|.
std::wstring IndexedSubsubfolderName(int i);

}  // namespace bookmarks_helper

#endif  // CHROME_BROWSER_SYNC_TEST_INTEGRATION_BOOKMARKS_HELPER_H_
