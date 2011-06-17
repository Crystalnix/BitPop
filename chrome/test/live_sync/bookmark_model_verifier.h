// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_TEST_LIVE_SYNC_BOOKMARK_MODEL_VERIFIER_H_
#define CHROME_TEST_LIVE_SYNC_BOOKMARK_MODEL_VERIFIER_H_
#pragma once

#include <set>
#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "googleurl/src/gurl.h"
#include "third_party/skia/include/core/SkBitmap.h"

// Helper class that performs operations on a bookmark model and echoes the
// changes in a verifier model that can be used as an expected hierarchy to
// compare against.
// Note: When we refer to the "same" node in |model| and |verifier_model_|,
// we mean the same canonical bookmark entity, because |verifier_model_| is
// expected to be a replica of |model|.
class BookmarkModelVerifier {
 public:
  explicit BookmarkModelVerifier(BookmarkModel* model);
  ~BookmarkModelVerifier();

  // Checks if the hierarchies in |model_a| and |model_b| are equivalent in
  // terms of the data model and favicon. Returns true if they both match.
  // Note: Some peripheral fields like creation times are allowed to mismatch.
  bool ModelsMatch(BookmarkModel* model_a,
                   BookmarkModel* model_b) const WARN_UNUSED_RESULT;

  // Checks if |model| contains any instances of two bookmarks with the same URL
  // under the same parent folder. Returns true if even one instance is found.
  bool ContainsDuplicateBookmarks(BookmarkModel* model) const;

  // Checks if the favicon in |node_a| from |model_a| matches that of |node_b|
  // from |model_b|. Returns true if they match.
  bool FaviconsMatch(BookmarkModel* model_a,
                     BookmarkModel* model_b,
                     const BookmarkNode* node_a,
                     const BookmarkNode* node_b) const;

  // Checks if the favicon data in |bitmap_a| and |bitmap_b| are equivalent.
  // Returns true if they match.
  bool FaviconBitmapsMatch(const SkBitmap& bitmap_a,
                           const SkBitmap& bitmap_b) const;

  // Adds the same bookmark to |model| and |verifier_model_|. See
  // BookmarkModel::AddURL for details.
  const BookmarkNode* AddURL(BookmarkModel* model,
                             const BookmarkNode* parent,
                             int index,
                             const string16& title,
                             const GURL& url);

  // Adds the same folder to |model| and |verifier_model_|. See
  // BookmarkModel::AddFolder for details.
  const BookmarkNode* AddFolder(BookmarkModel* model,
                                const BookmarkNode* parent,
                                int index,
                                const string16& title);

  // Sets the title of the same node in |model| and |verifier_model_|. See
  // BookmarkModel::SetTitle for details.
  void SetTitle(BookmarkModel* model,
                const BookmarkNode* node,
                const string16& title);

  // Sets the favicon of the same node in |model| and |verifier_model_| using
  // the data in |icon_bytes_vector|.
  // See BookmarkChangeProcessor::ApplyBookmarkFavicon for details.
  void SetFavicon(BookmarkModel* model,
                  const BookmarkNode* node,
                  const std::vector<unsigned char>& icon_bytes_vector);

  // Gets the favicon associated with |node| in |model|.
  const SkBitmap& GetFavicon(BookmarkModel* model,
                             const BookmarkNode* node) const;

  // Moves the same node to the same position in both |model| and
  // |verifier_model_|. See BookmarkModel::Move for details.
  void Move(BookmarkModel* model,
            const BookmarkNode* node,
            const BookmarkNode* new_parent,
            int index);

  // Removes the same node from |model| and |verifier_model_|. See
  // BookmarkModel::Remove for details.
  void Remove(BookmarkModel* model, const BookmarkNode* parent, int index);

  // Sorts children of the same parent node in |model| and |verifier_model_|.
  // See BookmarkModel::SortChildren for details.
  void SortChildren(BookmarkModel* model, const BookmarkNode* parent);

  // Reverses the order of children of the same parent node in |model|
  // and |verifier_model_|.
  void ReverseChildOrder(BookmarkModel* model, const BookmarkNode* parent);

  // Modifies the url contained in |node| to |new_url|.
  const BookmarkNode* SetURL(BookmarkModel* model,
                             const BookmarkNode* node,
                             const GURL& new_url);

  void FindNodeInVerifier(BookmarkModel* foreign_model,
                          const BookmarkNode* foreign_node,
                          const BookmarkNode** result);

  // Does a deep comparison of BookmarkNode fields in |model_a| and |model_b|.
  // Returns true if they are all equal.
  bool NodesMatch(const BookmarkNode* model_a,
                  const BookmarkNode* model_b) const;

  bool use_verifier_model() const { return use_verifier_model_; }

  void set_use_verifier_model(bool use_verifier_model) {
    use_verifier_model_ = use_verifier_model;
  }

  // Returns the number of nodes of node type |node_type| in |model| whose
  // titles match the string |title|.
  int CountNodesWithTitlesMatching(BookmarkModel* model,
                                   BookmarkNode::Type node_type,
                                   const string16& title) const;

 private:
  // A pointer to the BookmarkModel object within the verifier_profile_ object
  // in class LiveSyncTest. All verifications are done against this object.
  BookmarkModel* verifier_model_;

  // A flag that indicates whether bookmark operations should also update the
  // verifier model or not.
  bool use_verifier_model_;

  // A collection of URLs for which we have added favicons. Since loading a
  // favicon is an asynchronous operation and doesn't necessarily invoke a
  // callback, this collection is used to determine if we must wait for a URL's
  // favicon to load or not.
  std::set<GURL> urls_with_favicons_;

  DISALLOW_COPY_AND_ASSIGN(BookmarkModelVerifier);
};

#endif  // CHROME_TEST_LIVE_SYNC_BOOKMARK_MODEL_VERIFIER_H_
