// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/indexed_db/proxy_webidbobjectstore_impl.h"

#include "content/common/indexed_db/indexed_db_messages.h"
#include "content/public/common/serialized_script_value.h"
#include "content/common/indexed_db/indexed_db_dispatcher.h"
#include "content/common/indexed_db/proxy_webidbindex_impl.h"
#include "content/common/indexed_db/proxy_webidbtransaction_impl.h"
#include "content/common/child_thread.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebDOMStringList.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebIDBKey.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebIDBKeyPath.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebIDBKeyRange.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebIDBTransaction.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebSerializedScriptValue.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebString.h"

using content::IndexedDBKey;
using content::IndexedDBKeyPath;
using content::IndexedDBKeyRange;
using content::SerializedScriptValue;
using WebKit::WebDOMStringList;
using WebKit::WebExceptionCode;
using WebKit::WebFrame;
using WebKit::WebIDBCallbacks;
using WebKit::WebIDBKeyPath;
using WebKit::WebIDBKeyRange;
using WebKit::WebIDBIndex;
using WebKit::WebIDBKey;
using WebKit::WebIDBTransaction;
using WebKit::WebSerializedScriptValue;
using WebKit::WebString;
using WebKit::WebVector;

RendererWebIDBObjectStoreImpl::RendererWebIDBObjectStoreImpl(
    int32 idb_object_store_id)
    : idb_object_store_id_(idb_object_store_id) {
}

RendererWebIDBObjectStoreImpl::~RendererWebIDBObjectStoreImpl() {
  // It's not possible for there to be pending callbacks that address this
  // object since inside WebKit, they hold a reference to the object wich owns
  // this object. But, if that ever changed, then we'd need to invalidate
  // any such pointers.
  IndexedDBDispatcher::Send(
      new IndexedDBHostMsg_ObjectStoreDestroyed(idb_object_store_id_));
}

void RendererWebIDBObjectStoreImpl::get(
    const WebIDBKeyRange& key_range,
    WebIDBCallbacks* callbacks,
    const WebIDBTransaction& transaction,
    WebExceptionCode& ec) {
  IndexedDBDispatcher* dispatcher =
      IndexedDBDispatcher::ThreadSpecificInstance();
  dispatcher->RequestIDBObjectStoreGet(
      IndexedDBKeyRange(key_range), callbacks,
      idb_object_store_id_, transaction, &ec);
}

void RendererWebIDBObjectStoreImpl::putWithIndexKeys(
    const WebSerializedScriptValue& value,
    const WebIDBKey& key,
    PutMode put_mode,
    WebIDBCallbacks* callbacks,
    const WebIDBTransaction& transaction,
    const WebVector<WebString>& indexNames,
    const WebVector<WebVector<WebIDBKey> >& indexKeys,
    WebExceptionCode& ec) {
  IndexedDBDispatcher* dispatcher =
      IndexedDBDispatcher::ThreadSpecificInstance();
  dispatcher->RequestIDBObjectStorePut(
      SerializedScriptValue(value), IndexedDBKey(key),
      put_mode, callbacks, idb_object_store_id_, transaction,
      indexNames, indexKeys, &ec);
}

void RendererWebIDBObjectStoreImpl::deleteFunction(
    const WebIDBKeyRange& key_range,
    WebIDBCallbacks* callbacks,
    const WebIDBTransaction& transaction,
    WebExceptionCode& ec) {
  IndexedDBDispatcher* dispatcher =
      IndexedDBDispatcher::ThreadSpecificInstance();
  dispatcher->RequestIDBObjectStoreDelete(
      IndexedDBKeyRange(key_range), callbacks, idb_object_store_id_,
      transaction, &ec);
}

void RendererWebIDBObjectStoreImpl::clear(
    WebIDBCallbacks* callbacks,
    const WebIDBTransaction& transaction,
    WebExceptionCode& ec) {
  IndexedDBDispatcher* dispatcher =
      IndexedDBDispatcher::ThreadSpecificInstance();
  dispatcher->RequestIDBObjectStoreClear(
      callbacks, idb_object_store_id_, transaction, &ec);
}

WebIDBIndex* RendererWebIDBObjectStoreImpl::createIndex(
    const WebString& name,
    const WebIDBKeyPath& key_path,
    bool unique,
    bool multi_entry,
    const WebIDBTransaction& transaction,
    WebExceptionCode& ec) {
  IndexedDBHostMsg_ObjectStoreCreateIndex_Params params;
  params.name = name;
  params.key_path = IndexedDBKeyPath(key_path);
  params.unique = unique;
  params.multi_entry = multi_entry;
  params.transaction_id = IndexedDBDispatcher::TransactionId(transaction);
  params.idb_object_store_id = idb_object_store_id_;

  int32 index_id;
  IndexedDBDispatcher::Send(
      new IndexedDBHostMsg_ObjectStoreCreateIndex(params, &index_id, &ec));
  if (!index_id)
    return NULL;
  return new RendererWebIDBIndexImpl(index_id);
}

WebIDBIndex* RendererWebIDBObjectStoreImpl::index(
    const WebString& name,
    WebExceptionCode& ec) {
  int32 idb_index_id;
  IndexedDBDispatcher::Send(
      new IndexedDBHostMsg_ObjectStoreIndex(idb_object_store_id_, name,
                                            &idb_index_id, &ec));
  if (!idb_index_id)
      return NULL;
  return new RendererWebIDBIndexImpl(idb_index_id);
}

void RendererWebIDBObjectStoreImpl::deleteIndex(
    const WebString& name,
    const WebIDBTransaction& transaction,
    WebExceptionCode& ec) {
  IndexedDBDispatcher::Send(
      new IndexedDBHostMsg_ObjectStoreDeleteIndex(
          idb_object_store_id_, name,
          IndexedDBDispatcher::TransactionId(transaction), &ec));
}

void RendererWebIDBObjectStoreImpl::openCursor(
    const WebIDBKeyRange& idb_key_range,
    unsigned short direction, WebIDBCallbacks* callbacks,
    const WebIDBTransaction& transaction,
    WebExceptionCode& ec) {
  IndexedDBDispatcher* dispatcher =
      IndexedDBDispatcher::ThreadSpecificInstance();
  dispatcher->RequestIDBObjectStoreOpenCursor(
      idb_key_range, direction, callbacks,  idb_object_store_id_,
      transaction, &ec);
}

void RendererWebIDBObjectStoreImpl::count(
    const WebIDBKeyRange& idb_key_range,
    WebIDBCallbacks* callbacks,
    const WebIDBTransaction& transaction,
    WebExceptionCode& ec) {
  IndexedDBDispatcher* dispatcher =
      IndexedDBDispatcher::ThreadSpecificInstance();
  dispatcher->RequestIDBObjectStoreCount(
      idb_key_range, callbacks,  idb_object_store_id_,
      transaction, &ec);
}
