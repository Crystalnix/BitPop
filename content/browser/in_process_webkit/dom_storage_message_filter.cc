// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/in_process_webkit/dom_storage_message_filter.h"

#include "base/bind.h"
#include "base/nullable_string16.h"
#include "content/browser/in_process_webkit/dom_storage_area.h"
#include "content/browser/in_process_webkit/dom_storage_context.h"
#include "content/browser/in_process_webkit/dom_storage_namespace.h"
#include "content/common/dom_storage_messages.h"
#include "content/public/browser/browser_thread.h"
#include "googleurl/src/gurl.h"

using content::BrowserMessageFilter;
using content::BrowserThread;
using WebKit::WebStorageArea;

DOMStorageMessageFilter* DOMStorageMessageFilter::storage_event_message_filter =
    NULL;
const GURL* DOMStorageMessageFilter::storage_event_url_ = NULL;

DOMStorageMessageFilter::
ScopedStorageEventContext::ScopedStorageEventContext(
    DOMStorageMessageFilter* dispatcher_message_filter, const GURL* url) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::WEBKIT_DEPRECATED));
  DCHECK(!storage_event_message_filter);
  DCHECK(!storage_event_url_);
  storage_event_message_filter = dispatcher_message_filter;
  storage_event_url_ = url;
  DCHECK(storage_event_message_filter);
  DCHECK(storage_event_url_);
}

DOMStorageMessageFilter::
ScopedStorageEventContext::~ScopedStorageEventContext() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::WEBKIT_DEPRECATED));
  DCHECK(storage_event_message_filter);
  DCHECK(storage_event_url_);
  storage_event_message_filter = NULL;
  storage_event_url_ = NULL;
}

DOMStorageMessageFilter::DOMStorageMessageFilter(
    int process_id, WebKitContext* webkit_context)
    : webkit_context_(webkit_context),
      process_id_(process_id) {
}

DOMStorageMessageFilter::~DOMStorageMessageFilter() {
  if (peer_handle())
    Context()->UnregisterMessageFilter(this);
}

void DOMStorageMessageFilter::OnChannelConnected(int32 peer_pid) {
  BrowserMessageFilter::OnChannelConnected(peer_pid);
  if (peer_handle())
    Context()->RegisterMessageFilter(this);
}

/* static */
void DOMStorageMessageFilter::DispatchStorageEvent(const NullableString16& key,
    const NullableString16& old_value, const NullableString16& new_value,
    const string16& origin, const GURL& url, bool is_local_storage) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::WEBKIT_DEPRECATED));
  DCHECK(is_local_storage);  // Only LocalStorage is implemented right now.
  DCHECK(storage_event_message_filter);
  DOMStorageMsg_Event_Params params;
  params.key = key;
  params.old_value = old_value;
  params.new_value = new_value;
  params.origin = origin;
  params.url = *storage_event_url_;  // The url passed in is junk.
  params.storage_type = is_local_storage ? DOM_STORAGE_LOCAL
                                         : DOM_STORAGE_SESSION;
  // The storage_event_message_filter is the DOMStorageMessageFilter that is up
  // in the current call stack since it caused the storage event to fire.
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&DOMStorageMessageFilter::OnStorageEvent,
                 storage_event_message_filter, params));
}

bool DOMStorageMessageFilter::OnMessageReceived(const IPC::Message& message,
                                                bool* message_was_ok) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP_EX(DOMStorageMessageFilter, message, *message_was_ok)
    IPC_MESSAGE_HANDLER(DOMStorageHostMsg_StorageAreaId, OnStorageAreaId)
    IPC_MESSAGE_HANDLER(DOMStorageHostMsg_Length, OnLength)
    IPC_MESSAGE_HANDLER(DOMStorageHostMsg_Key, OnKey)
    IPC_MESSAGE_HANDLER(DOMStorageHostMsg_GetItem, OnGetItem)
    IPC_MESSAGE_HANDLER(DOMStorageHostMsg_SetItem, OnSetItem)
    IPC_MESSAGE_HANDLER(DOMStorageHostMsg_RemoveItem, OnRemoveItem)
    IPC_MESSAGE_HANDLER(DOMStorageHostMsg_Clear, OnClear)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()

  return handled;
}

void DOMStorageMessageFilter::OnDestruct() const {
  BrowserThread::DeleteOnIOThread::Destruct(this);
}

void DOMStorageMessageFilter::OverrideThreadForMessage(
    const IPC::Message& message,
    BrowserThread::ID* thread) {
  if (IPC_MESSAGE_CLASS(message) == DOMStorageMsgStart)
    *thread = BrowserThread::WEBKIT_DEPRECATED;
}

void DOMStorageMessageFilter::OnStorageAreaId(int64 namespace_id,
                                              const string16& origin,
                                              int64* storage_area_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::WEBKIT_DEPRECATED));

  DOMStorageNamespace* storage_namespace =
      Context()->GetStorageNamespace(namespace_id, true);
  if (!storage_namespace) {
    *storage_area_id = DOMStorageContext::kInvalidStorageId;
    return;
  }
  DOMStorageArea* storage_area = storage_namespace->GetStorageArea(origin);
  *storage_area_id = storage_area->id();
}

void DOMStorageMessageFilter::OnLength(int64 storage_area_id,
                                       unsigned* length) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::WEBKIT_DEPRECATED));
  DOMStorageArea* storage_area = Context()->GetStorageArea(storage_area_id);
  if (!storage_area) {
    *length = 0;
  } else {
    *length = storage_area->Length();
  }
}

void DOMStorageMessageFilter::OnKey(int64 storage_area_id, unsigned index,
                                    NullableString16* key) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::WEBKIT_DEPRECATED));
  DOMStorageArea* storage_area = Context()->GetStorageArea(storage_area_id);
  if (!storage_area) {
    *key = NullableString16(true);
  } else {
    *key = storage_area->Key(index);
  }
}

void DOMStorageMessageFilter::OnGetItem(int64 storage_area_id,
                                        const string16& key,
                                        NullableString16* value) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::WEBKIT_DEPRECATED));
  DOMStorageArea* storage_area = Context()->GetStorageArea(storage_area_id);
  if (!storage_area) {
    *value = NullableString16(true);
  } else {
    *value = storage_area->GetItem(key);
  }
}

void DOMStorageMessageFilter::OnSetItem(
    int64 storage_area_id, const string16& key,
    const string16& value, const GURL& url,
    WebKit::WebStorageArea::Result* result, NullableString16* old_value) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::WEBKIT_DEPRECATED));
  DOMStorageArea* storage_area = Context()->GetStorageArea(storage_area_id);
  if (!storage_area) {
    *old_value = NullableString16(true);
    *result = WebKit::WebStorageArea::ResultOK;
    return;
  }

  ScopedStorageEventContext scope(this, &url);
  *old_value = storage_area->SetItem(key, value, result);
}

void DOMStorageMessageFilter::OnRemoveItem(
    int64 storage_area_id, const string16& key, const GURL& url,
    NullableString16* old_value) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::WEBKIT_DEPRECATED));
  DOMStorageArea* storage_area = Context()->GetStorageArea(storage_area_id);
  if (!storage_area) {
    *old_value = NullableString16(true);
    return;
  }

  ScopedStorageEventContext scope(this, &url);
  *old_value = storage_area->RemoveItem(key);
}

void DOMStorageMessageFilter::OnClear(int64 storage_area_id, const GURL& url,
                                      bool* something_cleared) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::WEBKIT_DEPRECATED));
  DOMStorageArea* storage_area = Context()->GetStorageArea(storage_area_id);
  if (!storage_area) {
    *something_cleared = false;
    return;
  }

  ScopedStorageEventContext scope(this, &url);
  *something_cleared = storage_area->Clear();
}

void DOMStorageMessageFilter::OnStorageEvent(
    const DOMStorageMsg_Event_Params& params) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  const DOMStorageContext::MessageFilterSet* set =
      Context()->GetMessageFilterSet();
  DOMStorageContext::MessageFilterSet::const_iterator cur = set->begin();
  while (cur != set->end()) {
    // The renderer that generates the event handles it itself.
    if (*cur != this)
      (*cur)->Send(new DOMStorageMsg_Event(params));
    ++cur;
  }
}
