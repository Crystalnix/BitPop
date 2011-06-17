// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/bookmarks/bookmark_model.h"

#include <algorithm>
#include <functional>

#include "base/callback.h"
#include "base/memory/scoped_vector.h"
#include "build/build_config.h"
#include "chrome/browser/bookmarks/bookmark_index.h"
#include "chrome/browser/bookmarks/bookmark_storage.h"
#include "chrome/browser/bookmarks/bookmark_utils.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/history/history_notifications.h"
#include "chrome/browser/profiles/profile.h"
#include "content/common/notification_service.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/l10n/l10n_util_collator.h"
#include "ui/gfx/codec/png_codec.h"

using base::Time;

namespace {

// Helper to get a mutable bookmark node.
static BookmarkNode* AsMutable(const BookmarkNode* node) {
  return const_cast<BookmarkNode*>(node);
}

}  // anonymous namespace

// BookmarkNode ---------------------------------------------------------------

BookmarkNode::BookmarkNode(const GURL& url)
    : url_(url) {
  Initialize(0);
}

BookmarkNode::BookmarkNode(int64 id, const GURL& url)
    : url_(url) {
  Initialize(id);
}

BookmarkNode::~BookmarkNode() {
}

void BookmarkNode::Initialize(int64 id) {
  id_ = id;
  loaded_favicon_ = false;
  favicon_load_handle_ = 0;
  type_ = !url_.is_empty() ? URL : BOOKMARK_BAR;
  date_added_ = Time::Now();
}

void BookmarkNode::InvalidateFavicon() {
  loaded_favicon_ = false;
  favicon_ = SkBitmap();
}

void BookmarkNode::Reset(const history::StarredEntry& entry) {
  DCHECK(entry.type != history::StarredEntry::URL || entry.url == url_);

  favicon_ = SkBitmap();
  switch (entry.type) {
    case history::StarredEntry::URL:
      type_ = BookmarkNode::URL;
      break;
    case history::StarredEntry::USER_FOLDER:
      type_ = BookmarkNode::FOLDER;
      break;
    case history::StarredEntry::BOOKMARK_BAR:
      type_ = BookmarkNode::BOOKMARK_BAR;
      break;
    case history::StarredEntry::OTHER:
      type_ = BookmarkNode::OTHER_NODE;
      break;
    default:
      NOTREACHED();
  }
  date_added_ = entry.date_added;
  date_folder_modified_ = entry.date_folder_modified;
  set_title(entry.title);
}

// BookmarkModel --------------------------------------------------------------

namespace {

// Comparator used when sorting bookmarks. Folders are sorted first, then
// bookmarks.
class SortComparator : public std::binary_function<const BookmarkNode*,
                                                   const BookmarkNode*,
                                                   bool> {
 public:
  explicit SortComparator(icu::Collator* collator) : collator_(collator) { }

  // Returns true if lhs preceeds rhs.
  bool operator() (const BookmarkNode* n1, const BookmarkNode* n2) {
    if (n1->type() == n2->type()) {
      // Types are the same, compare the names.
      if (!collator_)
        return n1->GetTitle() < n2->GetTitle();
      return l10n_util::CompareString16WithCollator(
          collator_, n1->GetTitle(), n2->GetTitle()) == UCOL_LESS;
    }
    // Types differ, sort such that folders come first.
    return n1->is_folder();
  }

 private:
  icu::Collator* collator_;
};

}  // namespace

BookmarkModel::BookmarkModel(Profile* profile)
    : profile_(profile),
      loaded_(false),
      file_changed_(false),
      root_(GURL()),
      bookmark_bar_node_(NULL),
      other_node_(NULL),
      next_node_id_(1),
      observers_(ObserverList<BookmarkModelObserver>::NOTIFY_EXISTING_ONLY),
      loaded_signal_(TRUE, FALSE) {
  if (!profile_) {
    // Profile is null during testing.
    DoneLoading(CreateLoadDetails());
  }
}

BookmarkModel::~BookmarkModel() {
  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    BookmarkModelBeingDeleted(this));

  if (store_) {
    // The store maintains a reference back to us. We need to tell it we're gone
    // so that it doesn't try and invoke a method back on us again.
    store_->BookmarkModelDeleted();
  }
}

void BookmarkModel::Load() {
  if (store_.get()) {
    // If the store is non-null, it means Load was already invoked. Load should
    // only be invoked once.
    NOTREACHED();
    return;
  }

  // Listen for changes to favicons so that we can update the favicon of the
  // node appropriately.
  registrar_.Add(this, NotificationType::FAVICON_CHANGED,
                 Source<Profile>(profile_));

  // Load the bookmarks. BookmarkStorage notifies us when done.
  store_ = new BookmarkStorage(profile_, this);
  store_->LoadBookmarks(CreateLoadDetails());
}

const BookmarkNode* BookmarkModel::GetParentForNewNodes() {
  std::vector<const BookmarkNode*> nodes =
      bookmark_utils::GetMostRecentlyModifiedFolders(this, 1);
  return nodes.empty() ? bookmark_bar_node_ : nodes[0];
}

void BookmarkModel::Remove(const BookmarkNode* parent, int index) {
  if (!loaded_ || !IsValidIndex(parent, index, false) || is_root(parent)) {
    NOTREACHED();
    return;
  }
  RemoveAndDeleteNode(AsMutable(parent->GetChild(index)));
}

void BookmarkModel::Move(const BookmarkNode* node,
                         const BookmarkNode* new_parent,
                         int index) {
  if (!loaded_ || !node || !IsValidIndex(new_parent, index, true) ||
      is_root(new_parent) || is_permanent_node(node)) {
    NOTREACHED();
    return;
  }

  if (new_parent->HasAncestor(node)) {
    // Can't make an ancestor of the node be a child of the node.
    NOTREACHED();
    return;
  }

  SetDateFolderModified(new_parent, Time::Now());

  const BookmarkNode* old_parent = node->parent();
  int old_index = old_parent->GetIndexOf(node);

  if (old_parent == new_parent &&
      (index == old_index || index == old_index + 1)) {
    // Node is already in this position, nothing to do.
    return;
  }

  if (old_parent == new_parent && index > old_index)
    index--;
  BookmarkNode* mutable_new_parent = AsMutable(new_parent);
  mutable_new_parent->Add(AsMutable(node), index);

  if (store_.get())
    store_->ScheduleSave();

  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    BookmarkNodeMoved(this, old_parent, old_index,
                                      new_parent, index));
}

void BookmarkModel::Copy(const BookmarkNode* node,
                         const BookmarkNode* new_parent,
                         int index) {
  if (!loaded_ || !node || !IsValidIndex(new_parent, index, true) ||
      is_root(new_parent) || is_permanent_node(node)) {
    NOTREACHED();
    return;
  }

  if (new_parent->HasAncestor(node)) {
    // Can't make an ancestor of the node be a child of the node.
    NOTREACHED();
    return;
  }

  SetDateFolderModified(new_parent, Time::Now());
  BookmarkNodeData drag_data_(node);
  std::vector<BookmarkNodeData::Element> elements(drag_data_.elements);
  // CloneBookmarkNode will use BookmarkModel methods to do the job, so we
  // don't need to send notifications here.
  bookmark_utils::CloneBookmarkNode(this, elements, new_parent, index);

  if (store_.get())
    store_->ScheduleSave();
}

const SkBitmap& BookmarkModel::GetFavicon(const BookmarkNode* node) {
  DCHECK(node);
  if (!node->is_favicon_loaded()) {
    BookmarkNode* mutable_node = AsMutable(node);
    mutable_node->set_favicon_loaded(true);
    LoadFavicon(mutable_node);
  }
  return node->favicon();
}

void BookmarkModel::SetTitle(const BookmarkNode* node, const string16& title) {
  if (!node) {
    NOTREACHED();
    return;
  }
  if (node->GetTitle() == title)
    return;

  if (node == bookmark_bar_node_ || node == other_node_) {
    NOTREACHED();
    return;
  }

  // The title index doesn't support changing the title, instead we remove then
  // add it back.
  index_->Remove(node);
  AsMutable(node)->set_title(title);
  index_->Add(node);

  if (store_.get())
    store_->ScheduleSave();

  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    BookmarkNodeChanged(this, node));
}

void BookmarkModel::SetURL(const BookmarkNode* node, const GURL& url) {
  if (!node) {
    NOTREACHED();
    return;
  }

  // We cannot change the URL of a folder.
  if (node->is_folder()) {
    NOTREACHED();
    return;
  }

  if (url == node->GetURL())
    return;

  AsMutable(node)->InvalidateFavicon();
  CancelPendingFaviconLoadRequests(AsMutable(node));

  {
    base::AutoLock url_lock(url_lock_);
    NodesOrderedByURLSet::iterator i = nodes_ordered_by_url_set_.find(
        AsMutable(node));
    DCHECK(i != nodes_ordered_by_url_set_.end());
    // i points to the first node with the URL, advance until we find the
    // node we're removing.
    while (*i != node)
      ++i;
    nodes_ordered_by_url_set_.erase(i);

    AsMutable(node)->SetURL(url);
    nodes_ordered_by_url_set_.insert(AsMutable(node));
  }

  if (store_.get())
    store_->ScheduleSave();

  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    BookmarkNodeChanged(this, node));
}

bool BookmarkModel::IsLoaded() {
  return loaded_;
}

void BookmarkModel::GetNodesByURL(const GURL& url,
                                  std::vector<const BookmarkNode*>* nodes) {
  base::AutoLock url_lock(url_lock_);
  BookmarkNode tmp_node(url);
  NodesOrderedByURLSet::iterator i = nodes_ordered_by_url_set_.find(&tmp_node);
  while (i != nodes_ordered_by_url_set_.end() && (*i)->GetURL() == url) {
    nodes->push_back(*i);
    ++i;
  }
}

const BookmarkNode* BookmarkModel::GetMostRecentlyAddedNodeForURL(
    const GURL& url) {
  std::vector<const BookmarkNode*> nodes;
  GetNodesByURL(url, &nodes);
  if (nodes.empty())
    return NULL;

  std::sort(nodes.begin(), nodes.end(), &bookmark_utils::MoreRecentlyAdded);
  return nodes.front();
}

void BookmarkModel::GetBookmarks(std::vector<GURL>* urls) {
  base::AutoLock url_lock(url_lock_);
  const GURL* last_url = NULL;
  for (NodesOrderedByURLSet::iterator i = nodes_ordered_by_url_set_.begin();
       i != nodes_ordered_by_url_set_.end(); ++i) {
    const GURL* url = &((*i)->GetURL());
    // Only add unique URLs.
    if (!last_url || *url != *last_url)
      urls->push_back(*url);
    last_url = url;
  }
}

bool BookmarkModel::HasBookmarks() {
  base::AutoLock url_lock(url_lock_);
  return !nodes_ordered_by_url_set_.empty();
}

bool BookmarkModel::IsBookmarked(const GURL& url) {
  base::AutoLock url_lock(url_lock_);
  return IsBookmarkedNoLock(url);
}

const BookmarkNode* BookmarkModel::GetNodeByID(int64 id) {
  // TODO(sky): TreeNode needs a method that visits all nodes using a predicate.
  return GetNodeByID(&root_, id);
}

const BookmarkNode* BookmarkModel::AddFolder(const BookmarkNode* parent,
                                             int index,
                                             const string16& title) {
  if (!loaded_ || parent == &root_ || !IsValidIndex(parent, index, true)) {
    // Can't add to the root.
    NOTREACHED();
    return NULL;
  }

  BookmarkNode* new_node = new BookmarkNode(generate_next_node_id(),
                                            GURL());
  new_node->set_date_folder_modified(Time::Now());
  new_node->set_title(title);
  new_node->set_type(BookmarkNode::FOLDER);

  return AddNode(AsMutable(parent), index, new_node, false);
}

const BookmarkNode* BookmarkModel::AddURL(const BookmarkNode* parent,
                                          int index,
                                          const string16& title,
                                          const GURL& url) {
  return AddURLWithCreationTime(parent, index, title, url, Time::Now());
}

const BookmarkNode* BookmarkModel::AddURLWithCreationTime(
    const BookmarkNode* parent,
    int index,
    const string16& title,
    const GURL& url,
    const Time& creation_time) {
  if (!loaded_ || !url.is_valid() || is_root(parent) ||
      !IsValidIndex(parent, index, true)) {
    NOTREACHED();
    return NULL;
  }

  bool was_bookmarked = IsBookmarked(url);

  SetDateFolderModified(parent, creation_time);

  BookmarkNode* new_node = new BookmarkNode(generate_next_node_id(), url);
  new_node->set_title(title);
  new_node->set_date_added(creation_time);
  new_node->set_type(BookmarkNode::URL);

  {
    // Only hold the lock for the duration of the insert.
    base::AutoLock url_lock(url_lock_);
    nodes_ordered_by_url_set_.insert(new_node);
  }

  return AddNode(AsMutable(parent), index, new_node, was_bookmarked);
}

void BookmarkModel::SortChildren(const BookmarkNode* parent) {
  if (!parent || !parent->is_folder() || is_root(parent) ||
      parent->child_count() <= 1) {
    return;
  }

  UErrorCode error = U_ZERO_ERROR;
  scoped_ptr<icu::Collator> collator(
      icu::Collator::createInstance(
          icu::Locale(g_browser_process->GetApplicationLocale().c_str()),
          error));
  if (U_FAILURE(error))
    collator.reset(NULL);
  BookmarkNode* mutable_parent = AsMutable(parent);
  std::sort(mutable_parent->children().begin(),
            mutable_parent->children().end(),
            SortComparator(collator.get()));

  if (store_.get())
    store_->ScheduleSave();

  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    BookmarkNodeChildrenReordered(this, parent));
}

void BookmarkModel::SetURLStarred(const GURL& url,
                                  const string16& title,
                                  bool is_starred) {
  std::vector<const BookmarkNode*> bookmarks;
  GetNodesByURL(url, &bookmarks);
  bool bookmarks_exist = !bookmarks.empty();
  if (is_starred == bookmarks_exist)
    return;  // Nothing to do, state already matches.

  if (is_starred) {
    // Create a bookmark.
    const BookmarkNode* parent = GetParentForNewNodes();
    AddURL(parent, parent->child_count(), title, url);
  } else {
    // Remove all the bookmarks.
    for (size_t i = 0; i < bookmarks.size(); ++i) {
      const BookmarkNode* node = bookmarks[i];
      int index = node->parent()->GetIndexOf(node);
      if (index > -1)
        Remove(node->parent(), index);
    }
  }
}

void BookmarkModel::SetDateFolderModified(const BookmarkNode* parent,
                                          const Time time) {
  DCHECK(parent);
  AsMutable(parent)->set_date_folder_modified(time);

  if (store_.get())
    store_->ScheduleSave();
}

void BookmarkModel::ResetDateFolderModified(const BookmarkNode* node) {
  SetDateFolderModified(node, Time());
}

void BookmarkModel::GetBookmarksWithTitlesMatching(
    const string16& text,
    size_t max_count,
    std::vector<bookmark_utils::TitleMatch>* matches) {
  if (!loaded_)
    return;

  index_->GetBookmarksWithTitlesMatching(text, max_count, matches);
}

void BookmarkModel::ClearStore() {
  registrar_.RemoveAll();
  store_ = NULL;
}

bool BookmarkModel::IsBookmarkedNoLock(const GURL& url) {
  BookmarkNode tmp_node(url);
  return (nodes_ordered_by_url_set_.find(&tmp_node) !=
          nodes_ordered_by_url_set_.end());
}

void BookmarkModel::FaviconLoaded(const BookmarkNode* node) {
  // Send out notification to the observer.
  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    BookmarkNodeFaviconLoaded(this, node));
}

void BookmarkModel::RemoveNode(BookmarkNode* node,
                               std::set<GURL>* removed_urls) {
  if (!loaded_ || !node || is_permanent_node(node)) {
    NOTREACHED();
    return;
  }

  if (node->type() == BookmarkNode::URL) {
    // NOTE: this is called in such a way that url_lock_ is already held. As
    // such, this doesn't explicitly grab the lock.
    NodesOrderedByURLSet::iterator i = nodes_ordered_by_url_set_.find(node);
    DCHECK(i != nodes_ordered_by_url_set_.end());
    // i points to the first node with the URL, advance until we find the
    // node we're removing.
    while (*i != node)
      ++i;
    nodes_ordered_by_url_set_.erase(i);
    removed_urls->insert(node->GetURL());

    index_->Remove(node);
  }

  CancelPendingFaviconLoadRequests(node);

  // Recurse through children.
  for (int i = node->child_count() - 1; i >= 0; --i)
    RemoveNode(node->GetChild(i), removed_urls);
}

void BookmarkModel::DoneLoading(
    BookmarkLoadDetails* details_delete_me) {
  DCHECK(details_delete_me);
  scoped_ptr<BookmarkLoadDetails> details(details_delete_me);
  if (loaded_) {
    // We should only ever be loaded once.
    NOTREACHED();
    return;
  }

  next_node_id_ = details->max_id();
  if (details->computed_checksum() != details->stored_checksum())
    SetFileChanged();
  if (details->computed_checksum() != details->stored_checksum() ||
      details->ids_reassigned()) {
    // If bookmarks file changed externally, the IDs may have changed
    // externally. In that case, the decoder may have reassigned IDs to make
    // them unique. So when the file has changed externally, we should save the
    // bookmarks file to persist new IDs.
    if (store_.get())
      store_->ScheduleSave();
  }
  bookmark_bar_node_ = details->release_bb_node();
  other_node_ = details->release_other_folder_node();
  index_.reset(details->release_index());

  // WARNING: order is important here, various places assume bookmark bar then
  // other node.
  root_.Add(bookmark_bar_node_, 0);
  root_.Add(other_node_, 1);

  {
    base::AutoLock url_lock(url_lock_);
    // Update nodes_ordered_by_url_set_ from the nodes.
    PopulateNodesByURL(&root_);
  }

  loaded_ = true;

  loaded_signal_.Signal();

  // Notify our direct observers.
  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_, Loaded(this));

  // And generic notification.
  NotificationService::current()->Notify(
      NotificationType::BOOKMARK_MODEL_LOADED,
      Source<Profile>(profile_),
      NotificationService::NoDetails());
}

void BookmarkModel::RemoveAndDeleteNode(BookmarkNode* delete_me) {
  scoped_ptr<BookmarkNode> node(delete_me);

  BookmarkNode* parent = AsMutable(node->parent());
  DCHECK(parent);
  int index = parent->GetIndexOf(node.get());
  parent->Remove(node.get());
  history::URLsStarredDetails details(false);
  {
    base::AutoLock url_lock(url_lock_);
    RemoveNode(node.get(), &details.changed_urls);

    // RemoveNode adds an entry to changed_urls for each node of type URL. As we
    // allow duplicates we need to remove any entries that are still bookmarked.
    for (std::set<GURL>::iterator i = details.changed_urls.begin();
         i != details.changed_urls.end(); ) {
      if (IsBookmarkedNoLock(*i)) {
        // When we erase the iterator pointing at the erasee is
        // invalidated, so using i++ here within the "erase" call is
        // important as it advances the iterator before passing the
        // old value through to erase.
        details.changed_urls.erase(i++);
      } else {
        ++i;
      }
    }
  }

  if (store_.get())
    store_->ScheduleSave();

  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    BookmarkNodeRemoved(this, parent, index, node.get()));

  if (details.changed_urls.empty()) {
    // No point in sending out notification if the starred state didn't change.
    return;
  }

  if (profile_) {
    HistoryService* history =
        profile_->GetHistoryService(Profile::EXPLICIT_ACCESS);
    if (history)
      history->URLsNoLongerBookmarked(details.changed_urls);
  }

  NotificationService::current()->Notify(
      NotificationType::URLS_STARRED,
      Source<Profile>(profile_),
      Details<history::URLsStarredDetails>(&details));
}

void BookmarkModel::BeginImportMode() {
  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    BookmarkImportBeginning(this));
}

void BookmarkModel::EndImportMode() {
  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    BookmarkImportEnding(this));
}

BookmarkNode* BookmarkModel::AddNode(BookmarkNode* parent,
                                     int index,
                                     BookmarkNode* node,
                                     bool was_bookmarked) {
  parent->Add(node, index);

  if (store_.get())
    store_->ScheduleSave();

  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    BookmarkNodeAdded(this, parent, index));

  index_->Add(node);

  if (node->type() == BookmarkNode::URL && !was_bookmarked) {
    history::URLsStarredDetails details(true);
    details.changed_urls.insert(node->GetURL());
    NotificationService::current()->Notify(
        NotificationType::URLS_STARRED,
        Source<Profile>(profile_),
        Details<history::URLsStarredDetails>(&details));
  }
  return node;
}

void BookmarkModel::BlockTillLoaded() {
  loaded_signal_.Wait();
}

const BookmarkNode* BookmarkModel::GetNodeByID(const BookmarkNode* node,
                                               int64 id) {
  if (node->id() == id)
    return node;

  for (int i = 0, child_count = node->child_count(); i < child_count; ++i) {
    const BookmarkNode* result = GetNodeByID(node->GetChild(i), id);
    if (result)
      return result;
  }
  return NULL;
}

bool BookmarkModel::IsValidIndex(const BookmarkNode* parent,
                                 int index,
                                 bool allow_end) {
  return (parent && parent->is_folder() &&
          (index >= 0 && (index < parent->child_count() ||
                          (allow_end && index == parent->child_count()))));
}

BookmarkNode* BookmarkModel::CreateBookmarkNode() {
  history::StarredEntry entry;
  entry.type = history::StarredEntry::BOOKMARK_BAR;
  return CreateRootNodeFromStarredEntry(entry);
}

BookmarkNode* BookmarkModel::CreateOtherBookmarksNode() {
  history::StarredEntry entry;
  entry.type = history::StarredEntry::OTHER;
  return CreateRootNodeFromStarredEntry(entry);
}

BookmarkNode* BookmarkModel::CreateRootNodeFromStarredEntry(
    const history::StarredEntry& entry) {
  DCHECK(entry.type == history::StarredEntry::BOOKMARK_BAR ||
         entry.type == history::StarredEntry::OTHER);
  BookmarkNode* node = new BookmarkNode(generate_next_node_id(), GURL());
  node->Reset(entry);
  if (entry.type == history::StarredEntry::BOOKMARK_BAR) {
    node->set_title(l10n_util::GetStringUTF16(IDS_BOOMARK_BAR_FOLDER_NAME));
  } else {
    node->set_title(
        l10n_util::GetStringUTF16(IDS_BOOMARK_BAR_OTHER_FOLDER_NAME));
  }
  return node;
}

void BookmarkModel::OnFaviconDataAvailable(
    FaviconService::Handle handle,
    history::FaviconData favicon) {
  SkBitmap favicon_bitmap;
  BookmarkNode* node =
      load_consumer_.GetClientData(
          profile_->GetFaviconService(Profile::EXPLICIT_ACCESS), handle);
  DCHECK(node);
  node->set_favicon_load_handle(0);
  if (favicon.is_valid() && gfx::PNGCodec::Decode(favicon.image_data->front(),
                                                  favicon.image_data->size(),
                                                  &favicon_bitmap)) {
    node->set_favicon(favicon_bitmap);
    FaviconLoaded(node);
  }
}

void BookmarkModel::LoadFavicon(BookmarkNode* node) {
  if (node->type() != BookmarkNode::URL)
    return;

  DCHECK(node->GetURL().is_valid());
  FaviconService* favicon_service =
      profile_->GetFaviconService(Profile::EXPLICIT_ACCESS);
  if (!favicon_service)
    return;
  FaviconService::Handle handle = favicon_service->GetFaviconForURL(
      node->GetURL(), history::FAVICON, &load_consumer_,
      NewCallback(this, &BookmarkModel::OnFaviconDataAvailable));
  load_consumer_.SetClientData(favicon_service, handle, node);
  node->set_favicon_load_handle(handle);
}

void BookmarkModel::CancelPendingFaviconLoadRequests(BookmarkNode* node) {
  if (node->favicon_load_handle()) {
    FaviconService* favicon_service =
        profile_->GetFaviconService(Profile::EXPLICIT_ACCESS);
    if (favicon_service)
      favicon_service->CancelRequest(node->favicon_load_handle());
    node->set_favicon_load_handle(0);
  }
}

void BookmarkModel::Observe(NotificationType type,
                            const NotificationSource& source,
                            const NotificationDetails& details) {
  switch (type.value) {
    case NotificationType::FAVICON_CHANGED: {
      // Prevent the observers from getting confused for multiple favicon loads.
      Details<history::FaviconChangeDetails> favicon_details(details);
      for (std::set<GURL>::const_iterator i = favicon_details->urls.begin();
           i != favicon_details->urls.end(); ++i) {
        std::vector<const BookmarkNode*> nodes;
        GetNodesByURL(*i, &nodes);
        for (size_t i = 0; i < nodes.size(); ++i) {
          // Got an updated favicon, for a URL, do a new request.
          BookmarkNode* node = AsMutable(nodes[i]);
          node->InvalidateFavicon();
          CancelPendingFaviconLoadRequests(node);
          FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                            BookmarkNodeChanged(this, node));
        }
      }
      break;
    }

    default:
      NOTREACHED();
      break;
  }
}

void BookmarkModel::PopulateNodesByURL(BookmarkNode* node) {
  // NOTE: this is called with url_lock_ already held. As such, this doesn't
  // explicitly grab the lock.
  if (node->is_url())
    nodes_ordered_by_url_set_.insert(node);
  for (int i = 0; i < node->child_count(); ++i)
    PopulateNodesByURL(node->GetChild(i));
}

int64 BookmarkModel::generate_next_node_id() {
  return next_node_id_++;
}

void BookmarkModel::SetFileChanged() {
  file_changed_ = true;
}

BookmarkLoadDetails* BookmarkModel::CreateLoadDetails() {
  BookmarkNode* bb_node = CreateBookmarkNode();
  BookmarkNode* other_folder_node = CreateOtherBookmarksNode();
  return new BookmarkLoadDetails(
      bb_node, other_folder_node, new BookmarkIndex(profile()), next_node_id_);
}
