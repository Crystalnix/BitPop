// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/indexed_db/indexed_db_dispatcher.h"

#include "base/lazy_instance.h"
#include "base/threading/thread_local.h"
#include "content/common/child_thread.h"
#include "content/common/indexed_db/indexed_db_messages.h"
#include "content/common/indexed_db/proxy_webidbcursor_impl.h"
#include "content/common/indexed_db/proxy_webidbdatabase_impl.h"
#include "content/common/indexed_db/proxy_webidbindex_impl.h"
#include "content/common/indexed_db/proxy_webidbobjectstore_impl.h"
#include "content/common/indexed_db/proxy_webidbtransaction_impl.h"
#include "ipc/ipc_channel.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebFrame.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebIDBDatabaseCallbacks.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebIDBDatabaseError.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebIDBDatabaseException.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebIDBKeyRange.h"

using base::ThreadLocalPointer;
using WebKit::WebDOMStringList;
using WebKit::WebExceptionCode;
using WebKit::WebFrame;
using WebKit::WebIDBCallbacks;
using WebKit::WebIDBKeyRange;
using WebKit::WebIDBDatabase;
using WebKit::WebIDBDatabaseCallbacks;
using WebKit::WebIDBDatabaseError;
using WebKit::WebIDBTransaction;
using WebKit::WebIDBTransactionCallbacks;
using webkit_glue::WorkerTaskRunner;

namespace content {
static base::LazyInstance<ThreadLocalPointer<IndexedDBDispatcher> >::Leaky
    g_idb_dispatcher_tls = LAZY_INSTANCE_INITIALIZER;

namespace {

IndexedDBDispatcher* const kHasBeenDeleted =
    reinterpret_cast<IndexedDBDispatcher*>(0x1);

int32 CurrentWorkerId() {
  return WorkerTaskRunner::Instance()->CurrentWorkerId();
}
}  // unnamed namespace

const size_t kMaxIDBValueSizeInBytes = 64 * 1024 * 1024;

IndexedDBDispatcher::IndexedDBDispatcher() {
  g_idb_dispatcher_tls.Pointer()->Set(this);
}

IndexedDBDispatcher::~IndexedDBDispatcher() {
  // Clear any pending callbacks - which may result in dispatch requests -
  // before marking the dispatcher as deleted.
  pending_callbacks_.Clear();
  pending_database_callbacks_.Clear();
  pending_transaction_callbacks_.Clear();

  DCHECK(pending_callbacks_.IsEmpty());
  DCHECK(pending_database_callbacks_.IsEmpty());
  DCHECK(pending_transaction_callbacks_.IsEmpty());

  g_idb_dispatcher_tls.Pointer()->Set(kHasBeenDeleted);
}

IndexedDBDispatcher* IndexedDBDispatcher::ThreadSpecificInstance() {
  if (g_idb_dispatcher_tls.Pointer()->Get() == kHasBeenDeleted) {
    NOTREACHED() << "Re-instantiating TLS IndexedDBDispatcher.";
    g_idb_dispatcher_tls.Pointer()->Set(NULL);
  }
  if (g_idb_dispatcher_tls.Pointer()->Get())
    return g_idb_dispatcher_tls.Pointer()->Get();

  IndexedDBDispatcher* dispatcher = new IndexedDBDispatcher;
  if (WorkerTaskRunner::Instance()->CurrentWorkerId())
    webkit_glue::WorkerTaskRunner::Instance()->AddStopObserver(dispatcher);
  return dispatcher;
}

void IndexedDBDispatcher::OnWorkerRunLoopStopped() {
  delete this;
}

void IndexedDBDispatcher::OnMessageReceived(const IPC::Message& msg) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(IndexedDBDispatcher, msg)
    IPC_MESSAGE_HANDLER(IndexedDBMsg_CallbacksSuccessIDBCursor,
                        OnSuccessOpenCursor)
    IPC_MESSAGE_HANDLER(IndexedDBMsg_CallbacksSuccessCursorAdvance,
                        OnSuccessCursorContinue)
    IPC_MESSAGE_HANDLER(IndexedDBMsg_CallbacksSuccessCursorContinue,
                        OnSuccessCursorContinue)
    IPC_MESSAGE_HANDLER(IndexedDBMsg_CallbacksSuccessCursorPrefetch,
                        OnSuccessCursorPrefetch)
    IPC_MESSAGE_HANDLER(IndexedDBMsg_CallbacksSuccessIDBDatabase,
                        OnSuccessIDBDatabase)
    IPC_MESSAGE_HANDLER(IndexedDBMsg_CallbacksSuccessIndexedDBKey,
                        OnSuccessIndexedDBKey)
    IPC_MESSAGE_HANDLER(IndexedDBMsg_CallbacksSuccessStringList,
                        OnSuccessStringList)
    IPC_MESSAGE_HANDLER(IndexedDBMsg_CallbacksSuccessSerializedScriptValue,
                        OnSuccessSerializedScriptValue)
    IPC_MESSAGE_HANDLER(IndexedDBMsg_CallbacksSuccessSerializedScriptValueWithKey,
                        OnSuccessSerializedScriptValueWithKey)
    IPC_MESSAGE_HANDLER(IndexedDBMsg_CallbacksSuccessInteger,
                        OnSuccessInteger)
    IPC_MESSAGE_HANDLER(IndexedDBMsg_CallbacksSuccessUndefined,
                        OnSuccessUndefined)
    IPC_MESSAGE_HANDLER(IndexedDBMsg_CallbacksError, OnError)
    IPC_MESSAGE_HANDLER(IndexedDBMsg_CallbacksBlocked, OnBlocked)
    IPC_MESSAGE_HANDLER(IndexedDBMsg_CallbacksIntBlocked, OnIntBlocked)
    IPC_MESSAGE_HANDLER(IndexedDBMsg_CallbacksUpgradeNeeded, OnUpgradeNeeded)
    IPC_MESSAGE_HANDLER(IndexedDBMsg_TransactionCallbacksAbort, OnAbort)
    IPC_MESSAGE_HANDLER(IndexedDBMsg_TransactionCallbacksComplete, OnComplete)
    IPC_MESSAGE_HANDLER(IndexedDBMsg_DatabaseCallbacksForcedClose,
                        OnForcedClose)
    IPC_MESSAGE_HANDLER(IndexedDBMsg_DatabaseCallbacksIntVersionChange,
                        OnIntVersionChange)
    IPC_MESSAGE_HANDLER(IndexedDBMsg_DatabaseCallbacksVersionChange,
                        OnVersionChange)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  // If a message gets here, IndexedDBMessageFilter already determined that it
  // is an IndexedDB message.
  DCHECK(handled) << "Didn't handle a message defined at line "
                  << IPC_MESSAGE_ID_LINE(msg.type());
}

bool IndexedDBDispatcher::Send(IPC::Message* msg) {
  if (CurrentWorkerId()) {
    scoped_refptr<IPC::SyncMessageFilter> filter(
        ChildThread::current()->sync_message_filter());
    return filter->Send(msg);
  }
  return ChildThread::current()->Send(msg);
}

void IndexedDBDispatcher::RequestIDBCursorAdvance(
    unsigned long count,
    WebIDBCallbacks* callbacks_ptr,
    int32 ipc_cursor_id,
    WebExceptionCode* ec) {
  // Reset all cursor prefetch caches except for this cursor.
  ResetCursorPrefetchCaches(ipc_cursor_id);

  scoped_ptr<WebIDBCallbacks> callbacks(callbacks_ptr);

  int32 ipc_response_id = pending_callbacks_.Add(callbacks.release());
  Send(new IndexedDBHostMsg_CursorAdvance(ipc_cursor_id, CurrentWorkerId(),
                                          ipc_response_id, count));
}

void IndexedDBDispatcher::RequestIDBCursorContinue(
    const IndexedDBKey& key,
    WebIDBCallbacks* callbacks_ptr,
    int32 ipc_cursor_id,
    WebExceptionCode* ec) {
  // Reset all cursor prefetch caches except for this cursor.
  ResetCursorPrefetchCaches(ipc_cursor_id);

  scoped_ptr<WebIDBCallbacks> callbacks(callbacks_ptr);

  int32 ipc_response_id = pending_callbacks_.Add(callbacks.release());
  Send(
      new IndexedDBHostMsg_CursorContinue(ipc_cursor_id, CurrentWorkerId(),
                                          ipc_response_id, key));
}

void IndexedDBDispatcher::RequestIDBCursorPrefetch(
    int n,
    WebIDBCallbacks* callbacks_ptr,
    int32 ipc_cursor_id,
    WebExceptionCode* ec) {
  scoped_ptr<WebIDBCallbacks> callbacks(callbacks_ptr);

  int32 ipc_response_id = pending_callbacks_.Add(callbacks.release());
  Send(new IndexedDBHostMsg_CursorPrefetch(ipc_cursor_id, CurrentWorkerId(),
                                           ipc_response_id, n));
}

void IndexedDBDispatcher::RequestIDBCursorPrefetchReset(
    int used_prefetches, int unused_prefetches, int32 ipc_cursor_id) {
  Send(new IndexedDBHostMsg_CursorPrefetchReset(ipc_cursor_id,
                                                used_prefetches,
                                                unused_prefetches));
}

void IndexedDBDispatcher::RequestIDBCursorDelete(
    WebIDBCallbacks* callbacks_ptr,
    int32 ipc_cursor_id,
    WebExceptionCode* ec) {
  ResetCursorPrefetchCaches();
  scoped_ptr<WebIDBCallbacks> callbacks(callbacks_ptr);

  int32 ipc_response_id = pending_callbacks_.Add(callbacks.release());
  Send(new IndexedDBHostMsg_CursorDelete(ipc_cursor_id, CurrentWorkerId(),
                                         ipc_response_id));
}

void IndexedDBDispatcher::RequestIDBFactoryOpen(
    const string16& name,
    int64 version,
    WebIDBCallbacks* callbacks_ptr,
    WebIDBDatabaseCallbacks* database_callbacks_ptr,
    const string16& origin,
    WebFrame* web_frame) {
  ResetCursorPrefetchCaches();
  scoped_ptr<WebIDBCallbacks> callbacks(callbacks_ptr);
  scoped_ptr<WebIDBDatabaseCallbacks>
      database_callbacks(database_callbacks_ptr);

  if (!CurrentWorkerId() &&
      !ChildThread::current()->IsWebFrameValid(web_frame))
    return;

  IndexedDBHostMsg_FactoryOpen_Params params;
  params.ipc_thread_id = CurrentWorkerId();
  params.ipc_response_id = pending_callbacks_.Add(callbacks.release());
  params.ipc_database_response_id = pending_database_callbacks_.Add(
      database_callbacks.release());
  params.origin = origin;
  params.name = name;
  params.transaction_id = 0;
  params.version = version;
  Send(new IndexedDBHostMsg_FactoryOpen(params));
}

void IndexedDBDispatcher::RequestIDBFactoryOpen(
    const string16& name,
    int64 version,
    int64 transaction_id,
    WebIDBCallbacks* callbacks_ptr,
    WebIDBDatabaseCallbacks* database_callbacks_ptr,
    const string16& origin,
    WebFrame* web_frame) {
  ResetCursorPrefetchCaches();
  scoped_ptr<WebIDBCallbacks> callbacks(callbacks_ptr);
  scoped_ptr<WebIDBDatabaseCallbacks>
      database_callbacks(database_callbacks_ptr);

  if (!CurrentWorkerId() &&
      !ChildThread::current()->IsWebFrameValid(web_frame))
    return;

  IndexedDBHostMsg_FactoryOpen_Params params;
  params.ipc_thread_id = CurrentWorkerId();
  params.ipc_response_id = pending_callbacks_.Add(callbacks.release());
  params.ipc_database_response_id = pending_database_callbacks_.Add(
      database_callbacks.release());
  params.origin = origin;
  params.name = name;
  params.transaction_id = transaction_id;
  params.version = version;
  Send(new IndexedDBHostMsg_FactoryOpen(params));
}

void IndexedDBDispatcher::RequestIDBFactoryGetDatabaseNames(
    WebIDBCallbacks* callbacks_ptr,
    const string16& origin,
    WebFrame* web_frame) {
  ResetCursorPrefetchCaches();
  scoped_ptr<WebIDBCallbacks> callbacks(callbacks_ptr);

  if (!CurrentWorkerId() &&
      !ChildThread::current()->IsWebFrameValid(web_frame))
    return;

  IndexedDBHostMsg_FactoryGetDatabaseNames_Params params;
  params.ipc_thread_id = CurrentWorkerId();
  params.ipc_response_id = pending_callbacks_.Add(callbacks.release());
  params.origin = origin;
  Send(new IndexedDBHostMsg_FactoryGetDatabaseNames(params));
}

void IndexedDBDispatcher::RequestIDBFactoryDeleteDatabase(
    const string16& name,
    WebIDBCallbacks* callbacks_ptr,
    const string16& origin,
    WebFrame* web_frame) {
  ResetCursorPrefetchCaches();
  scoped_ptr<WebIDBCallbacks> callbacks(callbacks_ptr);

  if (!CurrentWorkerId() &&
      !ChildThread::current()->IsWebFrameValid(web_frame))
    return;

  IndexedDBHostMsg_FactoryDeleteDatabase_Params params;
  params.ipc_thread_id = CurrentWorkerId();
  params.ipc_response_id = pending_callbacks_.Add(callbacks.release());
  params.origin = origin;
  params.name = name;
  Send(new IndexedDBHostMsg_FactoryDeleteDatabase(params));
}

void IndexedDBDispatcher::RequestIDBDatabaseClose(int32 ipc_database_id) {
  ResetCursorPrefetchCaches();
  Send(new IndexedDBHostMsg_DatabaseClose(ipc_database_id));
  // There won't be pending database callbacks if the transaction was aborted in
  // the initial upgradeneeded event handler.
  if (pending_database_callbacks_.Lookup(ipc_database_id))
    pending_database_callbacks_.Remove(ipc_database_id);
}

void IndexedDBDispatcher::RequestIDBIndexOpenObjectCursor(
    const WebIDBKeyRange& idb_key_range,
    unsigned short direction,
    WebIDBCallbacks* callbacks_ptr,
    int32 ipc_index_id,
    const WebIDBTransaction& transaction,
    WebExceptionCode* ec) {
  ResetCursorPrefetchCaches();
  scoped_ptr<WebIDBCallbacks> callbacks(callbacks_ptr);
  IndexedDBHostMsg_IndexOpenCursor_Params params;
  params.ipc_thread_id = CurrentWorkerId();
  params.ipc_response_id = pending_callbacks_.Add(callbacks.release());
  params.key_range = IndexedDBKeyRange(idb_key_range);
  params.direction = direction;
  params.ipc_index_id = ipc_index_id;
  params.ipc_transaction_id = TransactionId(transaction);
  Send(new IndexedDBHostMsg_IndexOpenObjectCursor(params));
}

void IndexedDBDispatcher::RequestIDBIndexOpenKeyCursor(
    const WebIDBKeyRange& idb_key_range,
    unsigned short direction,
    WebIDBCallbacks* callbacks_ptr,
    int32 ipc_index_id,
    const WebIDBTransaction& transaction,
    WebExceptionCode* ec) {
  ResetCursorPrefetchCaches();
  scoped_ptr<WebIDBCallbacks> callbacks(callbacks_ptr);
  IndexedDBHostMsg_IndexOpenCursor_Params params;
  params.ipc_thread_id = CurrentWorkerId();
  params.ipc_response_id = pending_callbacks_.Add(callbacks.release());
  params.key_range = IndexedDBKeyRange(idb_key_range);
  params.direction = direction;
  params.ipc_index_id = ipc_index_id;
  params.ipc_transaction_id = TransactionId(transaction);
  Send(new IndexedDBHostMsg_IndexOpenKeyCursor(params));
}

void IndexedDBDispatcher::RequestIDBIndexCount(
    const WebIDBKeyRange& idb_key_range,
    WebIDBCallbacks* callbacks_ptr,
    int32 ipc_index_id,
    const WebIDBTransaction& transaction,
    WebExceptionCode* ec) {
  ResetCursorPrefetchCaches();
  scoped_ptr<WebIDBCallbacks> callbacks(callbacks_ptr);
  IndexedDBHostMsg_IndexCount_Params params;
  params.ipc_thread_id = CurrentWorkerId();
  params.ipc_response_id = pending_callbacks_.Add(callbacks.release());
  params.key_range = IndexedDBKeyRange(idb_key_range);
  params.ipc_index_id = ipc_index_id;
  params.ipc_transaction_id = TransactionId(transaction);
  Send(new IndexedDBHostMsg_IndexCount(params));
}

void IndexedDBDispatcher::RequestIDBIndexGetObject(
    const IndexedDBKeyRange& key_range,
    WebIDBCallbacks* callbacks_ptr,
    int32 ipc_index_id,
    const WebIDBTransaction& transaction,
    WebExceptionCode* ec) {
  ResetCursorPrefetchCaches();
  scoped_ptr<WebIDBCallbacks> callbacks(callbacks_ptr);
  int32 ipc_response_id = pending_callbacks_.Add(callbacks.release());
  Send(new IndexedDBHostMsg_IndexGetObject(
           ipc_index_id, CurrentWorkerId(),
           ipc_response_id, key_range,
           TransactionId(transaction)));
}

void IndexedDBDispatcher::RequestIDBIndexGetKey(
    const IndexedDBKeyRange& key_range,
    WebIDBCallbacks* callbacks_ptr,
    int32 ipc_index_id,
    const WebIDBTransaction& transaction,
    WebExceptionCode* ec) {
  ResetCursorPrefetchCaches();
  scoped_ptr<WebIDBCallbacks> callbacks(callbacks_ptr);
  int32 ipc_response_id = pending_callbacks_.Add(callbacks.release());
  Send(new IndexedDBHostMsg_IndexGetKey(
           ipc_index_id, CurrentWorkerId(), ipc_response_id, key_range,
           TransactionId(transaction)));
}

void IndexedDBDispatcher::RequestIDBObjectStoreGet(
    const IndexedDBKeyRange& key_range,
    WebIDBCallbacks* callbacks_ptr,
    int32 ipc_object_store_id,
    const WebIDBTransaction& transaction,
    WebExceptionCode* ec) {
  ResetCursorPrefetchCaches();
  scoped_ptr<WebIDBCallbacks> callbacks(callbacks_ptr);

  int32 ipc_response_id = pending_callbacks_.Add(callbacks.release());
  Send(new IndexedDBHostMsg_ObjectStoreGet(
           ipc_object_store_id, CurrentWorkerId(), ipc_response_id,
           key_range, TransactionId(transaction)));
}

void IndexedDBDispatcher::RequestIDBObjectStorePut(
    const SerializedScriptValue& value,
    const IndexedDBKey& key,
    WebKit::WebIDBObjectStore::PutMode put_mode,
    WebIDBCallbacks* callbacks_ptr,
    int32 ipc_object_store_id,
    const WebIDBTransaction& transaction,
    const WebKit::WebVector<long long>& index_ids,
    const WebKit::WebVector<WebKit::WebVector<
        WebKit::WebIDBKey> >& index_keys) {
  ResetCursorPrefetchCaches();
  scoped_ptr<WebIDBCallbacks> callbacks(callbacks_ptr);
  IndexedDBHostMsg_ObjectStorePut_Params params;
  params.ipc_thread_id = CurrentWorkerId();
  params.ipc_object_store_id = ipc_object_store_id;
  params.ipc_response_id = pending_callbacks_.Add(callbacks.release());
  params.serialized_value = value;
  params.key = key;
  params.put_mode = put_mode;
  params.ipc_transaction_id = TransactionId(transaction);
  params.index_ids.resize(index_ids.size());
  for (size_t i = 0; i < index_ids.size(); ++i) {
      params.index_ids[i] = index_ids[i];
  }

  params.index_keys.resize(index_keys.size());
  for (size_t i = 0; i < index_keys.size(); ++i) {
      params.index_keys[i].resize(index_keys[i].size());
      for (size_t j = 0; j < index_keys[i].size(); ++j) {
          params.index_keys[i][j] = IndexedDBKey(index_keys[i][j]);
      }
  }
  Send(new IndexedDBHostMsg_ObjectStorePut(params));
}

void IndexedDBDispatcher::RequestIDBObjectStoreDelete(
    const IndexedDBKeyRange& key_range,
    WebIDBCallbacks* callbacks_ptr,
    int32 ipc_object_store_id,
    const WebIDBTransaction& transaction,
    WebExceptionCode* ec) {
  ResetCursorPrefetchCaches();
  scoped_ptr<WebIDBCallbacks> callbacks(callbacks_ptr);

  int32 ipc_response_id = pending_callbacks_.Add(callbacks.release());
  Send(new IndexedDBHostMsg_ObjectStoreDelete(
      ipc_object_store_id, CurrentWorkerId(), ipc_response_id, key_range,
      TransactionId(transaction)));
}

void IndexedDBDispatcher::RequestIDBObjectStoreClear(
    WebIDBCallbacks* callbacks_ptr,
    int32 ipc_object_store_id,
    const WebIDBTransaction& transaction,
    WebExceptionCode* ec) {
  ResetCursorPrefetchCaches();
  scoped_ptr<WebIDBCallbacks> callbacks(callbacks_ptr);

  int32 ipc_response_id = pending_callbacks_.Add(callbacks.release());
  Send(new IndexedDBHostMsg_ObjectStoreClear(
      ipc_object_store_id, CurrentWorkerId(), ipc_response_id,
      TransactionId(transaction)));
}

void IndexedDBDispatcher::RequestIDBObjectStoreOpenCursor(
    const WebIDBKeyRange& idb_key_range,
    WebKit::WebIDBCursor::Direction direction,
    WebIDBCallbacks* callbacks_ptr,
    int32 ipc_object_store_id,
    WebKit::WebIDBTransaction::TaskType task_type,
    const WebIDBTransaction& transaction,
    WebExceptionCode* ec) {
  ResetCursorPrefetchCaches();
  scoped_ptr<WebIDBCallbacks> callbacks(callbacks_ptr);
  IndexedDBHostMsg_ObjectStoreOpenCursor_Params params;
  params.ipc_thread_id = CurrentWorkerId();
  params.ipc_response_id = pending_callbacks_.Add(callbacks.release());
  params.key_range = IndexedDBKeyRange(idb_key_range);
  params.direction = direction;
  params.ipc_object_store_id = ipc_object_store_id;
  params.task_type = task_type;
  params.ipc_transaction_id = TransactionId(transaction);
  Send(new IndexedDBHostMsg_ObjectStoreOpenCursor(params));
}

void IndexedDBDispatcher::RequestIDBObjectStoreCount(
    const WebIDBKeyRange& idb_key_range,
    WebIDBCallbacks* callbacks_ptr,
    int32 ipc_object_store_id,
    const WebIDBTransaction& transaction,
    WebExceptionCode* ec) {
  ResetCursorPrefetchCaches();
  scoped_ptr<WebIDBCallbacks> callbacks(callbacks_ptr);
  IndexedDBHostMsg_ObjectStoreCount_Params params;
  params.ipc_thread_id = CurrentWorkerId();
  params.ipc_response_id = pending_callbacks_.Add(callbacks.release());
  params.key_range = IndexedDBKeyRange(idb_key_range);
  params.ipc_object_store_id = ipc_object_store_id;
  params.ipc_transaction_id = TransactionId(transaction);
  Send(new IndexedDBHostMsg_ObjectStoreCount(params));
}

void IndexedDBDispatcher::RegisterWebIDBTransactionCallbacks(
    WebIDBTransactionCallbacks* callbacks,
    int32 id) {
  pending_transaction_callbacks_.AddWithID(callbacks, id);
}

void IndexedDBDispatcher::CursorDestroyed(int32 ipc_cursor_id) {
  cursors_.erase(ipc_cursor_id);
}

void IndexedDBDispatcher::DatabaseDestroyed(int32 ipc_database_id) {
  DCHECK_EQ(databases_.count(ipc_database_id), 1u);
  databases_.erase(ipc_database_id);
}

int32 IndexedDBDispatcher::TransactionId(
    const WebIDBTransaction& transaction) {
  const RendererWebIDBTransactionImpl* impl =
      static_cast<const RendererWebIDBTransactionImpl*>(&transaction);
  return impl->ipc_id();
}

void IndexedDBDispatcher::OnSuccessIDBDatabase(int32 ipc_thread_id,
                                               int32 ipc_response_id,
                                               int32 ipc_object_id) {
  DCHECK_EQ(ipc_thread_id, CurrentWorkerId());
  WebIDBCallbacks* callbacks = pending_callbacks_.Lookup(ipc_response_id);
  if (!callbacks)
    return;
  // If an upgrade was performed, count will be non-zero.
  if (!databases_.count(ipc_object_id))
    databases_[ipc_object_id] = new RendererWebIDBDatabaseImpl(ipc_object_id);
  DCHECK_EQ(databases_.count(ipc_object_id), 1u);
  callbacks->onSuccess(databases_[ipc_object_id]);
  pending_callbacks_.Remove(ipc_response_id);
}

void IndexedDBDispatcher::OnSuccessIndexedDBKey(
    int32 ipc_thread_id,
    int32 ipc_response_id,
    const IndexedDBKey& key) {
  DCHECK_EQ(ipc_thread_id, CurrentWorkerId());
  WebIDBCallbacks* callbacks = pending_callbacks_.Lookup(ipc_response_id);
  if (!callbacks)
    return;
  callbacks->onSuccess(key);
  pending_callbacks_.Remove(ipc_response_id);
}

void IndexedDBDispatcher::OnSuccessStringList(
    int32 ipc_thread_id, int32 ipc_response_id,
    const std::vector<string16>& value) {
  DCHECK_EQ(ipc_thread_id, CurrentWorkerId());
  WebIDBCallbacks* callbacks = pending_callbacks_.Lookup(ipc_response_id);
  if (!callbacks)
    return;
  WebDOMStringList string_list;
  for (std::vector<string16>::const_iterator it = value.begin();
       it != value.end(); ++it)
      string_list.append(*it);
  callbacks->onSuccess(string_list);
  pending_callbacks_.Remove(ipc_response_id);
}

void IndexedDBDispatcher::OnSuccessSerializedScriptValue(
    int32 ipc_thread_id, int32 ipc_response_id,
    const SerializedScriptValue& value) {
  DCHECK_EQ(ipc_thread_id, CurrentWorkerId());
  WebIDBCallbacks* callbacks = pending_callbacks_.Lookup(ipc_response_id);
  if (!callbacks)
    return;
  callbacks->onSuccess(value);
  pending_callbacks_.Remove(ipc_response_id);
}

void IndexedDBDispatcher::OnSuccessSerializedScriptValueWithKey(
    int32 ipc_thread_id, int32 ipc_response_id,
    const SerializedScriptValue& value,
    const IndexedDBKey& primary_key,
    const IndexedDBKeyPath& key_path) {
  DCHECK_EQ(ipc_thread_id, CurrentWorkerId());
  WebIDBCallbacks* callbacks = pending_callbacks_.Lookup(ipc_response_id);
  if (!callbacks)
    return;
  callbacks->onSuccess(value, primary_key, key_path);
  pending_callbacks_.Remove(ipc_response_id);
}

void IndexedDBDispatcher::OnSuccessInteger(
    int32 ipc_thread_id, int32 ipc_response_id, int64 value) {
  DCHECK_EQ(ipc_thread_id, CurrentWorkerId());
  WebIDBCallbacks* callbacks = pending_callbacks_.Lookup(ipc_response_id);
  if (!callbacks)
    return;
  callbacks->onSuccess(value);
  pending_callbacks_.Remove(ipc_response_id);
}

void IndexedDBDispatcher::OnSuccessUndefined(
    int32 ipc_thread_id, int32 ipc_response_id) {
  DCHECK_EQ(ipc_thread_id, CurrentWorkerId());
  WebIDBCallbacks* callbacks = pending_callbacks_.Lookup(ipc_response_id);
  if (!callbacks)
    return;
  callbacks->onSuccess();
  pending_callbacks_.Remove(ipc_response_id);
}

void IndexedDBDispatcher::OnSuccessOpenCursor(
    const IndexedDBMsg_CallbacksSuccessIDBCursor_Params& p) {
  DCHECK_EQ(p.ipc_thread_id, CurrentWorkerId());
  int32 ipc_response_id = p.ipc_response_id;
  int32 ipc_object_id = p.ipc_cursor_id;
  const IndexedDBKey& key = p.key;
  const IndexedDBKey& primary_key = p.primary_key;
  const SerializedScriptValue& value = p.serialized_value;

  WebIDBCallbacks* callbacks =
      pending_callbacks_.Lookup(ipc_response_id);
  if (!callbacks)
    return;

  RendererWebIDBCursorImpl* cursor =
          new RendererWebIDBCursorImpl(ipc_object_id);
  cursors_[ipc_object_id] = cursor;
  callbacks->onSuccess(cursor, key, primary_key, value);

  pending_callbacks_.Remove(ipc_response_id);
}

void IndexedDBDispatcher::OnSuccessCursorContinue(
    const IndexedDBMsg_CallbacksSuccessCursorContinue_Params& p) {
  DCHECK_EQ(p.ipc_thread_id, CurrentWorkerId());
  int32 ipc_response_id = p.ipc_response_id;
  int32 ipc_cursor_id = p.ipc_cursor_id;
  const IndexedDBKey& key = p.key;
  const IndexedDBKey& primary_key = p.primary_key;
  const SerializedScriptValue& value = p.serialized_value;

  RendererWebIDBCursorImpl* cursor = cursors_[ipc_cursor_id];
  DCHECK(cursor);

  WebIDBCallbacks* callbacks = pending_callbacks_.Lookup(ipc_response_id);
  if (!callbacks)
    return;

  callbacks->onSuccess(key, primary_key, value);

  pending_callbacks_.Remove(ipc_response_id);
}

void IndexedDBDispatcher::OnSuccessCursorPrefetch(
    const IndexedDBMsg_CallbacksSuccessCursorPrefetch_Params& p) {
  DCHECK_EQ(p.ipc_thread_id, CurrentWorkerId());
  int32 ipc_response_id = p.ipc_response_id;
  int32 ipc_cursor_id = p.ipc_cursor_id;
  const std::vector<IndexedDBKey>& keys = p.keys;
  const std::vector<IndexedDBKey>& primary_keys = p.primary_keys;
  const std::vector<SerializedScriptValue>& values = p.values;
  RendererWebIDBCursorImpl* cursor = cursors_[ipc_cursor_id];
  DCHECK(cursor);
  cursor->SetPrefetchData(keys, primary_keys, values);

  WebIDBCallbacks* callbacks = pending_callbacks_.Lookup(ipc_response_id);
  DCHECK(callbacks);
  cursor->CachedContinue(callbacks);
  pending_callbacks_.Remove(ipc_response_id);
}

void IndexedDBDispatcher::OnBlocked(int32 ipc_thread_id,
                                    int32 ipc_response_id) {
  DCHECK_EQ(ipc_thread_id, CurrentWorkerId());
  WebIDBCallbacks* callbacks = pending_callbacks_.Lookup(ipc_response_id);
  DCHECK(callbacks);
  callbacks->onBlocked();
}

void IndexedDBDispatcher::OnIntBlocked(int32 ipc_thread_id,
                                       int32 ipc_response_id,
                                       int64 existing_version) {
  DCHECK_EQ(ipc_thread_id, CurrentWorkerId());
  WebIDBCallbacks* callbacks = pending_callbacks_.Lookup(ipc_response_id);
  DCHECK(callbacks);
  callbacks->onBlocked(existing_version);
}

void IndexedDBDispatcher::OnUpgradeNeeded(int32 ipc_thread_id,
                                          int32 ipc_response_id,
                                          int32 ipc_transaction_id,
                                          int32 ipc_database_id,
                                          int64 old_version) {
  DCHECK_EQ(ipc_thread_id, CurrentWorkerId());
  WebIDBCallbacks* callbacks = pending_callbacks_.Lookup(ipc_response_id);
  DCHECK(callbacks);
  DCHECK(!databases_.count(ipc_database_id));
  databases_[ipc_database_id] = new RendererWebIDBDatabaseImpl(ipc_database_id);
  callbacks->onUpgradeNeeded(
      old_version,
      new RendererWebIDBTransactionImpl(ipc_transaction_id),
      databases_[ipc_database_id]);
}

void IndexedDBDispatcher::OnError(int32 ipc_thread_id, int32 ipc_response_id,
                                  int code,
                                  const string16& message) {
  DCHECK_EQ(ipc_thread_id, CurrentWorkerId());
  WebIDBCallbacks* callbacks = pending_callbacks_.Lookup(ipc_response_id);
  if (!callbacks)
    return;
  callbacks->onError(WebIDBDatabaseError(code, message));
  pending_callbacks_.Remove(ipc_response_id);
}

void IndexedDBDispatcher::OnAbort(int32 ipc_thread_id, int32 ipc_transaction_id,
                                  int code, const string16& message) {
  DCHECK_EQ(ipc_thread_id, CurrentWorkerId());
  WebIDBTransactionCallbacks* callbacks =
      pending_transaction_callbacks_.Lookup(ipc_transaction_id);
  if (!callbacks)
    return;
  callbacks->onAbort(WebIDBDatabaseError(code, message));
  pending_transaction_callbacks_.Remove(ipc_transaction_id);
}

void IndexedDBDispatcher::OnComplete(int32 ipc_thread_id,
                                     int32 ipc_transaction_id) {
  DCHECK_EQ(ipc_thread_id, CurrentWorkerId());
  WebIDBTransactionCallbacks* callbacks =
      pending_transaction_callbacks_.Lookup(ipc_transaction_id);
  if (!callbacks)
    return;
  callbacks->onComplete();
  pending_transaction_callbacks_.Remove(ipc_transaction_id);
}

void IndexedDBDispatcher::OnForcedClose(int32 ipc_thread_id,
                                        int32 ipc_database_id) {
  DCHECK_EQ(ipc_thread_id, CurrentWorkerId());
  WebIDBDatabaseCallbacks* callbacks =
      pending_database_callbacks_.Lookup(ipc_database_id);
  if (!callbacks)
    return;
  callbacks->onForcedClose();
}

void IndexedDBDispatcher::OnIntVersionChange(int32 ipc_thread_id,
                                             int32 ipc_database_id,
                                             int64 old_version,
                                             int64 new_version) {
  DCHECK_EQ(ipc_thread_id, CurrentWorkerId());
  WebIDBDatabaseCallbacks* callbacks =
      pending_database_callbacks_.Lookup(ipc_database_id);
  // callbacks would be NULL if a versionchange event is received after close
  // has been called.
  if (!callbacks)
    return;
  callbacks->onVersionChange(old_version, new_version);
}

void IndexedDBDispatcher::OnVersionChange(int32 ipc_thread_id,
                                          int32 ipc_database_id,
                                          const string16& newVersion) {
  DCHECK_EQ(ipc_thread_id, CurrentWorkerId());
  WebIDBDatabaseCallbacks* callbacks =
      pending_database_callbacks_.Lookup(ipc_database_id);
  // callbacks would be NULL if a versionchange event is received after close
  // has been called.
  if (!callbacks)
    return;
  callbacks->onVersionChange(newVersion);
}

void IndexedDBDispatcher::ResetCursorPrefetchCaches(
    int32 ipc_exception_cursor_id) {
  typedef std::map<int32, RendererWebIDBCursorImpl*>::iterator Iterator;
  for (Iterator i = cursors_.begin(); i != cursors_.end(); ++i) {
    if (i->first == ipc_exception_cursor_id)
      continue;
    i->second->ResetPrefetchCache();
  }
}

}  // namespace content
