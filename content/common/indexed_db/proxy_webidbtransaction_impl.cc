// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/indexed_db/proxy_webidbtransaction_impl.h"

#include "content/common/indexed_db/indexed_db_messages.h"
#include "content/common/indexed_db/indexed_db_dispatcher.h"
#include "content/common/indexed_db/proxy_webidbobjectstore_impl.h"
#include "content/common/child_thread.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebIDBObjectStore.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebIDBTransactionCallbacks.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebString.h"

using WebKit::WebIDBObjectStore;
using WebKit::WebIDBTransactionCallbacks;
using WebKit::WebString;

RendererWebIDBTransactionImpl::RendererWebIDBTransactionImpl(
    int32 idb_transaction_id)
    : idb_transaction_id_(idb_transaction_id) {
}

RendererWebIDBTransactionImpl::~RendererWebIDBTransactionImpl() {
  // It's not possible for there to be pending callbacks that address this
  // object since inside WebKit, they hold a reference to the object wich owns
  // this object. But, if that ever changed, then we'd need to invalidate
  // any such pointers.
  IndexedDBDispatcher::Send(new IndexedDBHostMsg_TransactionDestroyed(
      idb_transaction_id_));
}

WebIDBObjectStore* RendererWebIDBTransactionImpl::objectStore(
    const WebString& name,
    WebKit::WebExceptionCode& ec) {
  int object_store_id;
  IndexedDBDispatcher::Send(
      new IndexedDBHostMsg_TransactionObjectStore(
          idb_transaction_id_, name, &object_store_id, &ec));
  if (!object_store_id)
    return NULL;
  return new RendererWebIDBObjectStoreImpl(object_store_id);
}

void RendererWebIDBTransactionImpl::commit() {
  IndexedDBDispatcher::Send(new IndexedDBHostMsg_TransactionCommit(
      idb_transaction_id_));
}

void RendererWebIDBTransactionImpl::abort() {
  IndexedDBDispatcher::Send(new IndexedDBHostMsg_TransactionAbort(
      idb_transaction_id_));
}

void RendererWebIDBTransactionImpl::didCompleteTaskEvents() {
  IndexedDBDispatcher::Send(
      new IndexedDBHostMsg_TransactionDidCompleteTaskEvents(
          idb_transaction_id_));
}

void RendererWebIDBTransactionImpl::setCallbacks(
    WebIDBTransactionCallbacks* callbacks) {
  IndexedDBDispatcher* dispatcher =
      IndexedDBDispatcher::ThreadSpecificInstance();
  dispatcher->RegisterWebIDBTransactionCallbacks(callbacks,
                                                 idb_transaction_id_);
}
