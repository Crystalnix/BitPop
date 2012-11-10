// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/dom_storage/dom_storage_cached_area.h"

#include "base/basictypes.h"
#include "webkit/dom_storage/dom_storage_map.h"
#include "webkit/dom_storage/dom_storage_proxy.h"

namespace dom_storage {

DomStorageCachedArea::DomStorageCachedArea(
    int64 namespace_id, const GURL& origin, DomStorageProxy* proxy)
    : ignore_all_mutations_(false),
      namespace_id_(namespace_id), origin_(origin),
      proxy_(proxy), weak_factory_(ALLOW_THIS_IN_INITIALIZER_LIST(this)) {
}

DomStorageCachedArea::~DomStorageCachedArea() {
}

unsigned DomStorageCachedArea::GetLength(int connection_id) {
  PrimeIfNeeded(connection_id);
  return map_->Length();
}

NullableString16 DomStorageCachedArea::GetKey(
    int connection_id, unsigned index) {
  PrimeIfNeeded(connection_id);
  return map_->Key(index);
}

NullableString16 DomStorageCachedArea::GetItem(
    int connection_id, const string16& key) {
  PrimeIfNeeded(connection_id);
  return map_->GetItem(key);
}

bool DomStorageCachedArea::SetItem(
    int connection_id, const string16& key,
    const string16& value, const GURL& page_url) {
  // A quick check to reject obviously overbudget items to avoid
  // the priming the cache.
  if (key.length() + value.length() > dom_storage::kPerAreaQuota)
    return false;

  PrimeIfNeeded(connection_id);
  NullableString16 unused;
  if (!map_->SetItem(key, value, &unused))
    return false;

  // Ignore mutations to 'key' until OnSetItemComplete.
  ignore_key_mutations_[key]++;
  proxy_->SetItem(
      connection_id, key, value, page_url,
      base::Bind(&DomStorageCachedArea::OnSetItemComplete,
                 weak_factory_.GetWeakPtr(), key));
  return true;
}

void DomStorageCachedArea::RemoveItem(
    int connection_id, const string16& key, const GURL& page_url) {
  PrimeIfNeeded(connection_id);
  string16 unused;
  if (!map_->RemoveItem(key, &unused))
    return;

  // Ignore mutations to 'key' until OnRemoveItemComplete.
  ignore_key_mutations_[key]++;
  proxy_->RemoveItem(
      connection_id, key, page_url,
      base::Bind(&DomStorageCachedArea::OnRemoveItemComplete,
                 weak_factory_.GetWeakPtr(), key));
}

void DomStorageCachedArea::Clear(int connection_id, const GURL& page_url) {
  // No need to prime the cache in this case.
  Reset();
  map_ = new DomStorageMap(dom_storage::kPerAreaQuota);

  // Ignore all mutations until OnClearComplete time.
  ignore_all_mutations_ = true;
  proxy_->ClearArea(
      connection_id, page_url,
      base::Bind(&DomStorageCachedArea::OnClearComplete,
                 weak_factory_.GetWeakPtr()));
}

void DomStorageCachedArea::ApplyMutation(
    const NullableString16& key, const NullableString16& new_value) {
  if (!map_ || ignore_all_mutations_)
    return;

  if (key.is_null()) {
    // It's a clear event.
    scoped_refptr<DomStorageMap> old = map_;
    map_ = new DomStorageMap(dom_storage::kPerAreaQuota);

    // We have to retain local additions which happened after this
    // clear operation from another process.
    std::map<string16, int>::iterator iter = ignore_key_mutations_.begin();
    while (iter != ignore_key_mutations_.end()) {
      NullableString16 value = old->GetItem(iter->first);
      if (!value.is_null()) {
        NullableString16 unused;
        map_->SetItem(iter->first, value.string(), &unused);
      }
      ++iter;
    }
    return;
  }

  // We have to retain local changes.
  if (should_ignore_key_mutation(key.string()))
    return;

  if (new_value.is_null()) {
    // It's a remove item event.
    string16 unused;
    map_->RemoveItem(key.string(), &unused);
    return;
  }

  // It's a set item event.
  // We turn off quota checking here to accomodate the over budget
  // allowance that's provided in the browser process.
  NullableString16 unused;
  map_->set_quota(kint32max);
  map_->SetItem(key.string(), new_value.string(), &unused);
  map_->set_quota(dom_storage::kPerAreaQuota);
}

size_t DomStorageCachedArea::MemoryBytesUsedByCache() const {
  return map_ ? map_->bytes_used() : 0;
}

void DomStorageCachedArea::Prime(int connection_id) {
  DCHECK(!map_);

  // The LoadArea method is actually synchronous, but we have to
  // wait for an asyncly delivered message to know when incoming
  // mutation events should be applied. Our valuemap is plucked
  // from ipc stream out of order, mutations in front if it need
  // to be ignored.

  // Ignore all mutations until OnLoadComplete time.
  ignore_all_mutations_ = true;
  ValuesMap values;
  proxy_->LoadArea(
      connection_id, &values,
      base::Bind(&DomStorageCachedArea::OnLoadComplete,
                 weak_factory_.GetWeakPtr()));
  map_ = new DomStorageMap(dom_storage::kPerAreaQuota);
  map_->SwapValues(&values);
}

void DomStorageCachedArea::Reset() {
  map_ = NULL;
  weak_factory_.InvalidateWeakPtrs();
  ignore_key_mutations_.clear();
  ignore_all_mutations_ = false;
}

void DomStorageCachedArea::OnLoadComplete(bool success) {
  DCHECK(success);
  DCHECK(ignore_all_mutations_);
  ignore_all_mutations_ = false;
}

void DomStorageCachedArea::OnSetItemComplete(
    const string16& key, bool success) {
  if (!success) {
    Reset();
    return;
  }
  std::map<string16, int>::iterator found =  ignore_key_mutations_.find(key);
  DCHECK(found != ignore_key_mutations_.end());
  if (--found->second == 0)
    ignore_key_mutations_.erase(found);
}

void DomStorageCachedArea::OnRemoveItemComplete(
    const string16& key, bool success) {
  DCHECK(success);
  std::map<string16, int>::iterator found =  ignore_key_mutations_.find(key);
  DCHECK(found != ignore_key_mutations_.end());
  if (--found->second == 0)
    ignore_key_mutations_.erase(found);
}

void DomStorageCachedArea::OnClearComplete(bool success) {
  DCHECK(success);
  DCHECK(ignore_all_mutations_);
  ignore_all_mutations_ = false;
}

}  // namespace dom_storage
